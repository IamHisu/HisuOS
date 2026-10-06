#include "hisu_ui.h"
#include "hisu_ui_model.h"
#include "hisu_board.h"
#include "hisu_settings.h"
#include "hisu_network.h"
#include "hisu_power.h"
#include "hisu_clock.h"
#include "hisu_calendar.h"
#include "hisu_media.h"

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"

_Static_assert((int)UI_THEME == (int)HISU_SETTING_THEME, "Editable menu items must match settings keys");

LV_FONT_DECLARE(hisu_font_vi_semibold_16);
/* ESP-IDF EMBED_FILES symbols use the filename, without its directory. */
extern const uint8_t idle_cat_dark_start[] asm("_binary_idle_cat_dark_gif_start");
extern const uint8_t idle_cat_dark_end[] asm("_binary_idle_cat_dark_gif_end");
extern const uint8_t idle_cat_light_start[] asm("_binary_idle_cat_light_gif_start");
extern const uint8_t idle_cat_light_end[] asm("_binary_idle_cat_light_gif_end");
extern const uint8_t boot_windows_xp_start[] asm("_binary_boot_windows_xp_gif_start");
extern const uint8_t boot_windows_xp_end[] asm("_binary_boot_windows_xp_gif_end");
static lv_image_dsc_t s_boot_image;
#define HISU_BOOT_SPLASH_MS 3000
static lv_image_dsc_t s_idle_cat[2];
static lv_obj_t *s_idle_gif;
static lv_obj_t *s_idle_clock;
static lv_obj_t *s_clock_outline[8];
static int s_overlay_ink[3] = {-1, -1, -1};
static uint8_t *s_wallpaper_pixels;
static lv_image_dsc_t s_wallpaper_image;
static uint32_t s_wallpaper_revision;
static bool s_wallpaper_available;
static bool s_setup_url;
static bool s_light_theme;

static uint32_t theme_color(uint32_t dark)
{
    if (!s_light_theme) return dark;
    switch (dark) {
    case 0x000000: return 0xffffff;
    case 0xeeeeee: return 0x17231e;
    case 0xa1a9a5: return 0x58645e;
    case 0x8c9690: return 0x58645e;
    case 0x86dbaf: return 0x176742;
    case 0x224b3d: return 0xd9eee2;
    case 0x34423e: return 0xc6d1cb;
    case 0xf0a0a0: return 0xa52222;
    default: return dark;
    }
}

typedef struct { hisu_ui_status_t status; char text[256]; } status_event_t;
static QueueHandle_t s_events;
static EventGroupHandle_t s_startup;
static esp_err_t s_startup_result;
static esp_err_t s_first_frame_error;
static hisu_ui_model_t s_model;
static status_event_t s_status;
static bool s_asleep;
static int s_applied_brightness = -1;
static int64_t s_activity_ms;
static const char *s_error;
static lv_obj_t *s_subtitle_body;
static hisu_network_status_t s_network;
static hisu_power_status_t s_power;
static lv_obj_t *s_battery_body, *s_battery_bars[5], *s_battery_tip, *s_battery_charge;
static lv_obj_t *s_battery_outline, *s_battery_tip_outline, *s_charge_outline;
typedef struct { lv_obj_t *root, *arcs[3], *dot, *outline[3], *dot_outline; } wifi_icon_t;
static wifi_icon_t s_idle_wifi;
static int s_wifi_selected, s_wifi_first;
static bool s_wifi_select_after_scan;
static char s_join_ssid[33], s_password[65];
static int s_pass_length, s_pass_mode, s_pass_choice;
static int s_calendar_day; /* Zero follows today; otherwise a selected Julian day. */
static bool s_flashlight_enabled;
static unsigned s_flashlight_mode; /* Steady, blink, SOS. */
static int64_t s_flashlight_start_ms;
/* 8x5 keypad flow adapted from Muse's button menu (Apache-2.0).
 * Copyright (c) Meta Platforms, Inc. and affiliates. See licenses/NOTICE.md.
 */
static const char *const s_keysets[] = {
    "abcdefghijklmnopqrstuvwxyz0123456789",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789",
    "!@#$%^&*()-_=+[]{};:,.?/\\|\"'<>`~ 012",
};
static void render(void);
static const char *const s_items[] = {
    "Độ sáng", "Âm lượng", "Loa", "Thời gian chờ", "Theme", "Wi-Fi",
    "Pin", "Lịch", "Đèn pin", "Setting", "Tắt màn hình", "Đóng menu",
};
static const char *const s_states[] = {"", "Đang nghe", "Đang suy nghĩ", "Đang nói", "Lỗi"};
_Static_assert(sizeof(s_items)/sizeof(s_items[0]) == UI_ITEM_COUNT, "Menu labels must match model");
static const char *const s_demo[] = {
    "Hôm nay bạn muốn làm gì?",
    "Mình đang chuẩn bị câu trả lời.",
    "Chào bạn!\nHôm nay trời trong xanh. Một ngày mới bắt đầu với những ý tưởng mới.",
};

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void wallpaper_descriptor(lv_image_dsc_t *image, uint8_t *pixels)
{
    *image = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                   .w = 240, .h = 240, .stride = 480},
        .data_size = HISU_MEDIA_BYTES, .data = pixels,
    };
}

static void refresh_wallpaper(void)
{
    hisu_media_status_t media; hisu_media_get(&media);
    if (media.revision == s_wallpaper_revision && media.has_image == s_wallpaper_available) return;
    if (media.has_image && !s_wallpaper_pixels)
        s_wallpaper_pixels = heap_caps_malloc(HISU_MEDIA_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (media.has_image && !s_wallpaper_pixels) return;
    lv_image_cache_drop(&s_wallpaper_image);
    if (media.has_image) {
        s_wallpaper_available = hisu_media_copy_image(s_wallpaper_pixels, HISU_MEDIA_BYTES, &s_wallpaper_revision);
        if (s_wallpaper_available) wallpaper_descriptor(&s_wallpaper_image, s_wallpaper_pixels);
    } else {
        s_wallpaper_available = false;
        s_wallpaper_revision = media.revision;
    }
}

static void usage_text(char *out, size_t size, const char *name, uint64_t seconds)
{
    snprintf(out, size, "%s: %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
             name, seconds / 3600, seconds / 60 % 60, seconds % 60);
}

static void lcd_done(void *context)
{
    // LVGL's flush completion is the one permitted ISR-side LVGL call.
    lv_display_flush_ready(context);
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    lv_draw_sw_rgb565_swap(pixels, lv_area_get_size(area));
    esp_err_t err = hisu_board_display_write(area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels);
    if (err != ESP_OK) {
        ESP_LOGE("UI", "LCD transfer: %s", esp_err_to_name(err));
        s_first_frame_error = err;
        lv_display_flush_ready(display);
    }
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, &hisu_font_vi_semibold_16, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(theme_color(color)), 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    lv_label_set_text(obj, text);
    return obj;
}

static void header(const char *title)
{
    lv_obj_t *root = lv_screen_active();
    label(root, title, 12, 10, 216, 0xeeeeee);
    lv_obj_t *line = lv_obj_create(root);
    lv_obj_remove_style_all(line);
    lv_obj_set_pos(line, 12, 39);
    lv_obj_set_size(line, 216, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(theme_color(0x34423e)), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
}

static int wifi_signal_bars(int rssi)
{
    return rssi >= -60 ? 3 : rssi >= -75 ? 2 : 1;
}

static void update_wifi_icon(wifi_icon_t *icon, int bars, bool visible, lv_color_t color)
{
    if (visible) lv_obj_remove_flag(icon->root, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(icon->root, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; ++i) {
        lv_obj_set_style_arc_color(icon->arcs[i], color, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(icon->arcs[i], i < bars ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
        if (icon->outline[i]) {
            lv_obj_set_style_arc_color(icon->outline[i], lv_color_hex(color.red ? 0x000000 : 0xffffff), LV_PART_MAIN);
            lv_obj_set_style_arc_opa(icon->outline[i], i < bars ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
        }
    }
    lv_obj_set_style_bg_color(icon->dot, color, 0);
    lv_obj_set_style_bg_opa(icon->dot, bars ? LV_OPA_COVER : LV_OPA_30, 0);
    if (icon->dot_outline) {
        lv_obj_set_style_bg_color(icon->dot_outline, lv_color_hex(color.red ? 0x000000 : 0xffffff), 0);
        lv_obj_set_style_bg_opa(icon->dot_outline, bars ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}

static wifi_icon_t create_wifi_icon(lv_obj_t *parent, int x, int y, int bars, bool outlined)
{
    wifi_icon_t icon = {0};
    icon.root = lv_obj_create(parent);
    lv_obj_remove_style_all(icon.root);
    lv_obj_set_pos(icon.root, x, y);
    lv_obj_set_size(icon.root, 20, 16);
    lv_obj_remove_flag(icon.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(icon.root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    for (int i = 0; i < 3; ++i) {
        if (outlined) {
            icon.outline[i] = lv_arc_create(icon.root);
            lv_obj_remove_style_all(icon.outline[i]);
            lv_obj_set_pos(icon.outline[i], (2 - i) * 3, (2 - i) * 3);
            lv_obj_set_size(icon.outline[i], 20 - (2 - i) * 6, 20 - (2 - i) * 6);
            lv_arc_set_bg_angles(icon.outline[i], 215, 325);
            lv_arc_set_angles(icon.outline[i], 215, 215);
            lv_obj_set_style_arc_width(icon.outline[i], 3, LV_PART_MAIN);
            lv_obj_set_style_arc_rounded(icon.outline[i], true, LV_PART_MAIN);
            lv_obj_remove_flag(icon.outline[i], LV_OBJ_FLAG_CLICKABLE);
        }
        icon.arcs[i] = lv_arc_create(icon.root);
        lv_obj_remove_style_all(icon.arcs[i]);
        lv_obj_set_pos(icon.arcs[i], (2 - i) * 3, (2 - i) * 3);
        lv_obj_set_size(icon.arcs[i], 20 - (2 - i) * 6, 20 - (2 - i) * 6);
        lv_arc_set_bg_angles(icon.arcs[i], 215, 325);
        lv_arc_set_angles(icon.arcs[i], 215, 215);
        lv_obj_set_style_arc_width(icon.arcs[i], 1, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(icon.arcs[i], true, LV_PART_MAIN);
        lv_obj_remove_flag(icon.arcs[i], LV_OBJ_FLAG_CLICKABLE);
    }
    if (outlined) {
        icon.dot_outline = lv_obj_create(icon.root);
        lv_obj_remove_style_all(icon.dot_outline);
        lv_obj_set_pos(icon.dot_outline, 8, 9);
        lv_obj_set_size(icon.dot_outline, 4, 4);
        lv_obj_set_style_radius(icon.dot_outline, LV_RADIUS_CIRCLE, 0);
    }
    icon.dot = lv_obj_create(icon.root);
    lv_obj_remove_style_all(icon.dot);
    lv_obj_set_pos(icon.dot, 9, 10);
    lv_obj_set_size(icon.dot, 2, 2);
    lv_obj_set_style_radius(icon.dot, LV_RADIUS_CIRCLE, 0);
    update_wifi_icon(&icon, bars, true, lv_color_hex(theme_color(0x86dbaf)));
    return icon;
}

static void create_theme_icon(lv_obj_t *parent, bool light, bool selected)
{
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_remove_style_all(icon);
    lv_obj_set_pos(icon, 196, 4);
    lv_obj_set_size(icon, 20, 20);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_color_t color = lv_color_hex(theme_color(0x86dbaf));
    lv_obj_t *disc = lv_obj_create(icon);
    lv_obj_remove_style_all(disc);
    lv_obj_set_pos(disc, light ? 6 : 1, light ? 6 : 1);
    lv_obj_set_size(disc, light ? 8 : 18, light ? 8 : 18);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, color, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    if (light) {
        static const lv_point_precise_t rays[8][2] = {
            {{10, 1}, {10, 3}}, {{10, 17}, {10, 19}},
            {{1, 10}, {3, 10}}, {{17, 10}, {19, 10}},
            {{3, 3}, {5, 5}}, {{15, 15}, {17, 17}},
            {{3, 17}, {5, 15}}, {{15, 5}, {17, 3}},
        };
        for (int i = 0; i < 8; ++i) {
            lv_obj_t *ray = lv_line_create(icon);
            lv_obj_remove_style_all(ray);
            lv_line_set_points(ray, rays[i], 2);
            lv_obj_set_style_line_color(ray, color, 0);
            lv_obj_set_style_line_width(ray, 1, 0);
        }
    } else {
        lv_obj_t *cutout = lv_obj_create(icon);
        lv_obj_remove_style_all(cutout);
        lv_obj_set_pos(cutout, 7, -3);
        lv_obj_set_size(cutout, 18, 18);
        lv_obj_set_style_radius(cutout, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(cutout, lv_color_hex(theme_color(selected ? 0x224b3d : 0x000000)), 0);
        lv_obj_set_style_bg_opa(cutout, LV_OPA_COVER, 0);
    }
}

static lv_color_t overlay_color(unsigned index, int x, int y, int w, int h)
{
    const lv_image_dsc_t *image = s_wallpaper_available ? &s_wallpaper_image : NULL;
    if (!image && s_idle_gif && lv_gif_is_loaded(s_idle_gif)) image = lv_image_get_src(s_idle_gif);
    if (image && image->header.cf == LV_COLOR_FORMAT_RGB565) {
        int left = (HISU_DISPLAY_WIDTH - (int)image->header.w) / 2;
        int top = (HISU_DISPLAY_HEIGHT - (int)image->header.h) / 2;
        s_overlay_ink[index] = hisu_ui_overlay_dark_ink(image->data, image->header.w, image->header.h,
            image->header.stride, x - left, y - top, w, h, s_overlay_ink[index]);
    } else s_overlay_ink[index] = s_light_theme;
    return lv_color_hex(s_overlay_ink[index] ? 0x000000 : 0xffffff);
}

/* Update the status overlay independently so polling never restarts the GIF. */
static void update_idle_status(void)
{
    if (!s_battery_body) return;
    if (s_idle_clock) {
        char clock[6]; hisu_clock_read(clock); lv_label_set_text(s_idle_clock, clock);
        lv_color_t ink = overlay_color(0, 8, 10, 64, 16);
        lv_obj_set_style_text_color(s_idle_clock, ink, 0);
        for (int i = 0; i < 8; ++i) {
            lv_label_set_text(s_clock_outline[i], clock);
            lv_obj_set_style_text_color(s_clock_outline[i], lv_color_hex(ink.red ? 0x000000 : 0xffffff), 0);
        }
    }
    hisu_power_get(&s_power);
    hisu_network_get(&s_network);
    int percent = s_power.percent < 0 ? 0 : s_power.percent > 100 ? 100 : s_power.percent;
    lv_color_t color = overlay_color(2, 200, 10, 28, 16);
    lv_color_t outline = lv_color_hex(color.red ? 0x000000 : 0xffffff);
    lv_obj_set_style_border_color(s_battery_outline, outline, 0);
    lv_obj_set_style_bg_color(s_battery_tip_outline, outline, 0);
    lv_obj_set_style_border_color(s_battery_body, color, 0);
    lv_obj_set_style_bg_color(s_battery_tip, color, 0);
    int bars = s_power.valid ? (percent + 19) / 20 : 0;
    for (int i = 0; i < 5; ++i) {
        lv_obj_set_style_bg_color(s_battery_bars[i], color, 0);
        if (i < bars && !s_power.external_power) lv_obj_remove_flag(s_battery_bars[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_battery_bars[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_line_color(s_battery_charge, color, 0);
    lv_obj_set_style_line_color(s_charge_outline, outline, 0);
    if (s_power.valid && s_power.external_power) {
        lv_obj_remove_flag(s_battery_charge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_charge_outline, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_battery_charge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_charge_outline, LV_OBJ_FLAG_HIDDEN);
    }
    update_wifi_icon(&s_idle_wifi, s_network.connected ? wifi_signal_bars(s_network.rssi) : 0,
                     s_network.enabled, overlay_color(1, 176, 10, 22, 18));
}

static void render_idle_status(lv_obj_t *root)
{
    char clock[6]; hisu_clock_read(clock);
    s_overlay_ink[0] = s_overlay_ink[1] = s_overlay_ink[2] = -1;
    static const int offsets[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
    for (int i = 0; i < 8; ++i) {
        s_clock_outline[i] = label(root, clock, 8 + offsets[i][0], 10 + offsets[i][1], 64, 0x000000);
        lv_obj_set_style_text_font(s_clock_outline[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(s_clock_outline[i], LV_OPA_TRANSP, 0);
    }
    s_idle_clock = label(root, clock, 8, 10, 64, 0x86dbaf);
    lv_obj_set_style_text_font(s_idle_clock, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(s_idle_clock, LV_OPA_TRANSP, 0);
    lv_obj_t *panel = lv_obj_create(root);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, 156, 6);
    lv_obj_set_size(panel, 78, 22);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    /* Visible Wi-Fi pixels span 12 px; center them on the 14 px battery. */
    s_idle_wifi = create_wifi_icon(panel, 21, 6, 0, true);
    s_battery_outline = lv_obj_create(panel);
    lv_obj_remove_style_all(s_battery_outline);
    lv_obj_set_pos(s_battery_outline, 44, 4);
    lv_obj_set_size(s_battery_outline, 26, 16);
    lv_obj_set_style_border_width(s_battery_outline, 1, 0);
    lv_obj_set_style_radius(s_battery_outline, 4, 0);
    s_battery_body = lv_obj_create(panel);
    lv_obj_remove_style_all(s_battery_body);
    lv_obj_set_pos(s_battery_body, 45, 5);
    lv_obj_set_size(s_battery_body, 24, 14);
    lv_obj_set_style_border_width(s_battery_body, 2, 0);
    lv_obj_set_style_radius(s_battery_body, 3, 0);
    lv_obj_remove_flag(s_battery_body, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 5; ++i) {
        s_battery_bars[i] = lv_obj_create(panel);
        lv_obj_remove_style_all(s_battery_bars[i]);
        lv_obj_set_pos(s_battery_bars[i], 50 + i * 3, 9);
        lv_obj_set_size(s_battery_bars[i], 2, 6);
        lv_obj_set_style_radius(s_battery_bars[i], 1, 0);
        lv_obj_set_style_bg_opa(s_battery_bars[i], LV_OPA_COVER, 0);
    }
    s_battery_tip_outline = lv_obj_create(panel);
    lv_obj_remove_style_all(s_battery_tip_outline);
    lv_obj_set_pos(s_battery_tip_outline, 68, 9);
    lv_obj_set_size(s_battery_tip_outline, 4, 6);
    lv_obj_set_style_bg_opa(s_battery_tip_outline, LV_OPA_COVER, 0);
    s_battery_tip = lv_obj_create(panel);
    lv_obj_remove_style_all(s_battery_tip);
    lv_obj_set_pos(s_battery_tip, 69, 10);
    lv_obj_set_size(s_battery_tip, 2, 4);
    lv_obj_set_style_bg_opa(s_battery_tip, LV_OPA_COVER, 0);
    static const lv_point_precise_t bolt[] = {{3, 0}, {1, 4}, {5, 4}, {3, 8}};
    s_charge_outline = lv_line_create(panel);
    lv_obj_remove_style_all(s_charge_outline);
    lv_line_set_points(s_charge_outline, bolt, 4);
    lv_obj_set_pos(s_charge_outline, 54, 8);
    lv_obj_set_style_line_width(s_charge_outline, 4, 0);
    lv_obj_set_style_line_rounded(s_charge_outline, true, 0);
    s_battery_charge = lv_line_create(panel);
    lv_obj_remove_style_all(s_battery_charge);
    lv_line_set_points(s_battery_charge, bolt, 4);
    lv_obj_set_pos(s_battery_charge, 54, 8);
    lv_obj_set_style_line_width(s_battery_charge, 2, 0);
    lv_obj_set_style_line_rounded(s_battery_charge, true, 0);
    update_idle_status();
}

static void value_text(int item, int value, char *out, size_t size)
{
    if (item == UI_SPEAKER) snprintf(out, size, "%s", value ? "Bật" : "Tắt");
    else if (item == UI_THEME) snprintf(out, size, "%s", value ? "Sáng" : "Tối");
    else if (item == UI_TIMEOUT && value == 0) snprintf(out, size, "Tắt");
    else if (item == UI_TIMEOUT && value > 60) snprintf(out, size, "%d phút", value / 60);
    else if (item == UI_TIMEOUT) snprintf(out, size, "%ds", value);
    else snprintf(out, size, "%d%%", value);
}

static void footer_back_icon(lv_obj_t *parent)
{
    static const lv_point_precise_t path[] = {
        {1, 3}, {6, 3}, {8, 4}, {9, 6}, {9, 8}, {8, 10}, {6, 11}, {3, 11},
    };
    static const lv_point_precise_t head[] = {{4, 0}, {1, 3}, {4, 6}};
    const lv_point_precise_t *paths[] = {path, head};
    const uint32_t lengths[] = {8, 3};
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *line = lv_line_create(parent);
        lv_obj_remove_style_all(line);
        lv_line_set_points(line, paths[i], lengths[i]);
        lv_obj_set_style_line_width(line, 1, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(theme_color(0x86dbaf)), 0);
    }
}

/* Left-to-right labels match the physical Vol-, BOOT, Vol+ buttons. */
static void footer(const char *left, const char *middle, const char *right)
{
    if (!strcmp(left, "circle") || !strcmp(left, "filled-circle")) {
        lv_obj_t *row = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(row);
        lv_obj_set_pos(row, 8, 215);
        lv_obj_set_size(row, 224, 22);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(theme_color(0x86dbaf)), 0);
        if (!strcmp(left, "filled-circle")) {
            lv_obj_set_style_bg_color(dot, lv_color_hex(theme_color(0x86dbaf)), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        }
        label(row, middle, 0, 0, LV_SIZE_CONTENT, 0x8c9690);
        lv_obj_t *back = lv_obj_create(row);
        lv_obj_remove_style_all(back);
        lv_obj_set_size(back, 12, 12);
        lv_obj_remove_flag(back, LV_OBJ_FLAG_SCROLLABLE);
        footer_back_icon(back);
        return;
    }
    static const lv_point_precise_t down[] = {{0, 0}, {6, 6}, {12, 0}};
    static const lv_point_precise_t up[] = {{0, 6}, {6, 0}, {12, 6}};
    static const lv_point_precise_t next[] = {{0, 0}, {6, 6}, {0, 12}};
    static const lv_point_precise_t previous[] = {{6, 0}, {0, 6}, {6, 12}};
    const char *texts[] = {left, middle, right};
    for (int i = 0; i < 3; ++i) {
        if (!strcmp(texts[i], "back")) {
            lv_obj_t *back = lv_obj_create(lv_screen_active());
            lv_obj_remove_style_all(back);
            lv_obj_set_size(back, 12, 12);
            lv_obj_set_pos(back, 36 + i * 78, 220);
            lv_obj_remove_flag(back, LV_OBJ_FLAG_SCROLLABLE);
            footer_back_icon(back);
            continue;
        }
        if (!strcmp(texts[i], "v") || !strcmp(texts[i], "^") || !strcmp(texts[i], ">") || !strcmp(texts[i], "<")) {
            lv_obj_t *chevron = lv_line_create(lv_screen_active());
            lv_obj_remove_style_all(chevron);
            bool horizontal = texts[i][0] == '>' || texts[i][0] == '<';
            lv_line_set_points(chevron, horizontal ? (texts[i][0] == '>' ? next : previous) : texts[i][0] == 'v' ? down : up, 3);
            lv_obj_set_pos(chevron, (horizontal ? 39 : 36) + i * 78, horizontal ? 220 : 223);
            lv_obj_set_style_line_width(chevron, 2, 0);
            lv_obj_set_style_line_rounded(chevron, true, 0);
            lv_obj_set_style_line_color(chevron, lv_color_hex(theme_color(0x8c9690)), 0);
            continue;
        }
        lv_obj_t *hint = label(lv_screen_active(), texts[i], 4 + i * 78, 215, 76, 0x8c9690);
        if (!strcmp(texts[i], LV_SYMBOL_LIST) || !strcmp(texts[i], LV_SYMBOL_CLOSE) ||
            !strcmp(texts[i], LV_SYMBOL_SAVE) || !strcmp(texts[i], LV_SYMBOL_OK) ||
            !strcmp(texts[i], LV_SYMBOL_REFRESH) || !strcmp(texts[i], LV_SYMBOL_POWER))
            lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void render_menu(void)
{
    header("Hisu");
    int start = s_model.first_visible;
    for (int row = 0; row < 5; ++row) {
        int item = start + row;
        lv_obj_t *obj = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(obj);
        lv_obj_set_pos(obj, 8, 47 + row * 31);
        lv_obj_set_size(obj, 224, 30);
        lv_obj_set_style_radius(obj, 4, 0);
        if (item == s_model.selected) {
            lv_obj_set_style_bg_color(obj, lv_color_hex(theme_color(0x224b3d)), 0);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        }
        label(obj, s_items[item], 7, 3, item == UI_TIMEOUT ? 130 : 158,
              0xeeeeee);
        if (item == UI_THEME) {
            create_theme_icon(obj, hisu_settings_get(HISU_SETTING_THEME) == 1, item == s_model.selected);
            continue;
        }
        if (item == UI_WIFI) {
            wifi_icon_t icon = create_wifi_icon(obj, 196, 8,
                s_network.enabled && s_network.connected ? wifi_signal_bars(s_network.rssi) : 0, false);
            if (!s_network.enabled) {
                static const lv_point_precise_t slash[] = {{0, 0}, {14, 14}};
                lv_obj_t *line = lv_line_create(icon.root);
                lv_obj_remove_style_all(line);
                lv_line_set_points(line, slash, 2);
                lv_obj_set_pos(line, 3, 0);
                lv_obj_set_style_line_width(line, 1, 0);
                lv_obj_set_style_line_color(line, lv_color_hex(theme_color(0x8c9690)), 0);
            }
            continue;
        }
        char value[16] = "";
        if (item <= UI_THEME) value_text(item, hisu_settings_get((hisu_setting_t)item), value, sizeof(value));
        else if (item == UI_BATTERY && s_power.valid) snprintf(value, sizeof(value), "~%d%%", s_power.percent);
        else if (item <= UI_BATTERY) snprintf(value, sizeof(value), "--");
        else snprintf(value, sizeof(value), "%s", LV_SYMBOL_RIGHT);
        lv_obj_t *v = label(obj, value, item == UI_TIMEOUT ? 142 : 164, 3,
                            item == UI_TIMEOUT ? 76 : 54, 0x86dbaf);
        if (item > UI_BATTERY) lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    }
    char position[24];
    snprintf(position, sizeof(position), "%d / %d", s_model.selected + 1, UI_ITEM_COUNT);
    lv_obj_t *count = label(lv_screen_active(), position, 158, 10, 70, 0x8c9690);
    lv_obj_set_style_text_align(count, LV_TEXT_ALIGN_RIGHT, 0);
    footer("v", "Chọn", "^");
    if (s_error) label(lv_screen_active(), s_error, 12, 195, 216, 0xf0a0a0);
}

static void setup_line(lv_obj_t *root, const char *text, int y, uint32_t color)
{
    lv_obj_t *row = label(root, text, 12, y, 216, color);
    lv_obj_set_height(row, 22);
    lv_label_set_long_mode(row, LV_LABEL_LONG_CLIP);
}

static void render_setup(void)
{
    hisu_media_status_t media; hisu_media_get(&media);
    hisu_network_status_t network; hisu_network_get(&network);
    s_setup_url = media.enabled && network.setup_client_connected;
    lv_obj_t *root = lv_screen_active();
    header(media.enabled && s_model.preview == UI_SETUP_QR ?
        (s_setup_url ? "Trang Setting" : "Hotspot") : "Setting");
    if (!media.ready) {
        label(root,"Không khả dụng",12,80,216,0xf0a0a0); footer("","","back"); return;
    }
    if (s_model.preview == UI_SETUP_INFO) {
        char line[64];
        setup_line(root,network.setup_ssid,52,0x86dbaf);
        snprintf(line,sizeof(line),"MK: %s",network.setup_password);
        setup_line(root,line,88,0xeeeeee);
        setup_line(root,"http://",124,0xeeeeee);
        setup_line(root,network.setup_ip,152,0xeeeeee);
        footer(LV_SYMBOL_LIST,"back",LV_SYMBOL_CLOSE); return;
    }
    if (s_model.preview == UI_SETUP_RESET) {
        label(root,"Khôi phục nền mặc định?",12,76,216,0xeeeeee);
        label(root,"Ảnh đã lưu sẽ được bỏ",12,114,216,0x8c9690);
        footer("back",LV_SYMBOL_OK,LV_SYMBOL_CLOSE); return;
    }
    if (s_model.preview == UI_SETUP_STOP) {
        label(root,"Dừng chế độ Setting?",12,76,216,0xeeeeee);
        label(root,"Hotspot và web sẽ đóng",12,114,216,0x8c9690);
        footer(LV_SYMBOL_LIST,LV_SYMBOL_STOP,"back"); return;
    }
    char text[112];
    if (media.enabled && network.setup_enabled) {
        if (s_setup_url) snprintf(text,sizeof(text),"http://%s/",network.setup_ip);
        else snprintf(text,sizeof(text),"WIFI:T:WPA;S:%s;P:%s;;",network.setup_ssid,network.setup_password);
        /* Extra white border guarantees four quiet-zone modules at this size. */
        lv_obj_t *frame = lv_obj_create(root);
        lv_obj_remove_style_all(frame);
        lv_obj_set_pos(frame,38,44); lv_obj_set_size(frame,164,164);
        lv_obj_set_style_bg_color(frame,lv_color_white(),0);
        lv_obj_set_style_bg_opa(frame,LV_OPA_COVER,0);
        lv_obj_remove_flag(frame,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *qr = lv_qrcode_create(frame);
        lv_qrcode_set_size(qr,140);
        lv_qrcode_set_dark_color(qr,lv_color_black());
        lv_qrcode_set_light_color(qr,lv_color_white());
        lv_qrcode_set_quiet_zone(qr,true);
        lv_obj_set_pos(qr,12,12);
        if (lv_qrcode_update(qr,text,strlen(text)) != LV_RESULT_OK) {
            lv_obj_delete(frame);
            setup_line(root,"Không tạo được QR",100,0xf0a0a0);
        }
    } else {
        setup_line(root,media.opening ? "Đang mở Setting..." :
            media.error ? "Không mở được Setting" : "Setting đã đóng",76,0xeeeeee);
        if (media.error && !media.opening) {
            snprintf(text,sizeof(text),"Lỗi: 0x%x",(unsigned)media.error);
            setup_line(root,text,108,0xf0a0a0);
        }
    }
    footer(LV_SYMBOL_LIST,media.enabled ? LV_SYMBOL_SETTINGS : media.opening ? "" : LV_SYMBOL_POWER,LV_SYMBOL_CLOSE);
}

static int flashlight_brightness(int64_t now)
{
    if (!s_flashlight_enabled) return 0;
    int64_t elapsed = now-s_flashlight_start_ms;
    if (s_flashlight_mode == 0) return 100;
    if (s_flashlight_mode == 1) return elapsed % 1000 < 500 ? 100 : 0;
    /* ... --- ... : intra-letter gap 1, letter gap 3, repeat gap 7 units. */
    static const unsigned durations[] = {1,1,1,1,1,3,3,1,3,1,3,3,1,1,1,1,1,7};
    unsigned phase = (unsigned)(elapsed/200 % 34);
    for (unsigned i = 0; i < sizeof(durations)/sizeof(durations[0]); ++i) {
        if (phase < durations[i]) return i % 2 == 0 ? 100 : 0;
        phase -= durations[i];
    }
    return 0;
}

static void render_calendar(void)
{
    int today_d, today_m, today_y;
    if (!hisu_clock_vietnam_date(&today_d, &today_m, &today_y)) {
        header("Lịch");
        label(lv_screen_active(), "Chưa đồng bộ giờ", 12, 75, 216, 0x86dbaf);
        label(lv_screen_active(), "Kết nối Wi-Fi từ Setting", 12, 108, 216, 0x8c9690);
        footer("", "", "");
        return;
    }
    int today = hisu_calendar_jdn(today_d,today_m,today_y);
    int selected = s_calendar_day ? s_calendar_day : today, day,month,year;
    hisu_calendar_date(selected,&day,&month,&year);
    char text[64];
    snprintf(text,sizeof(text),"Lịch %02d/%d",month,year); header(text);
    static const char *const weekdays[] = {"Chủ nhật","Thứ hai","Thứ ba","Thứ tư","Thứ năm","Thứ sáu","Thứ bảy"};
    snprintf(text,sizeof(text),"%s, %02d/%02d",weekdays[(selected+1)%7],day,month);
    lv_obj_t *title = label(lv_screen_active(),text,8,43,224,0xeeeeee);
    lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    hisu_lunar_date_t lunar;
    if (hisu_calendar_lunar(day,month,year,&lunar)) {
        snprintf(text,sizeof(text),"Âm: %02d/%02d/%d%s",lunar.day,lunar.month,lunar.year,lunar.leap ? " nhuận" : "");
        title = label(lv_screen_active(),text,4,66,232,0x86dbaf);
        lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    }
    static const char *const short_days[] = {"CN","T2","T3","T4","T5","T6","T7"};
    for (int col = 0; col < 7; ++col) {
        title = label(lv_screen_active(),short_days[col],8+col*32,91,32,col ? 0x8c9690 : 0xf0a0a0);
        lv_obj_set_style_text_font(title,&lv_font_montserrat_14,0);
        lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    }
    int first = hisu_calendar_jdn(1,month,year), offset = (first+1)%7;
    int end = hisu_calendar_jdn(1,month == 12 ? 1 : month+1,year+(month == 12));
    for (int jd = first; jd < end; ++jd) {
        int slot = offset+jd-first, col = slot%7;
        lv_obj_t *cell = lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(cell);
        lv_obj_set_pos(cell,8+col*32,111+slot/7*16); lv_obj_set_size(cell,32,16);
        lv_obj_set_style_radius(cell,3,0);
        if (jd == selected) {
            lv_obj_set_style_bg_color(cell,lv_color_hex(theme_color(0x224b3d)),0);
            lv_obj_set_style_bg_opa(cell,LV_OPA_COVER,0);
        }
        if (jd == today) {
            lv_obj_set_style_border_width(cell,1,0);
            lv_obj_set_style_border_color(cell,lv_color_hex(theme_color(0x86dbaf)),0);
        }
        snprintf(text,sizeof(text),"%d",jd-first+1);
        title = label(cell,text,0,0,32,jd == selected ? 0x86dbaf : col ? 0xeeeeee : 0xf0a0a0);
        lv_obj_set_style_text_font(title,&lv_font_montserrat_14,0);
        lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    }
    footer("<","Hôm nay",">");
}

static void render_edit(void)
{
    header(s_items[s_model.selected]);
    char value[16];
    value_text(s_model.selected, s_model.value, value, sizeof(value));
    lv_obj_t *v = label(lv_screen_active(), value, 12, 73, 216, 0x86dbaf);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    if (s_model.selected == UI_SPEAKER || s_model.selected == UI_THEME) {
        lv_obj_t *control = lv_switch_create(lv_screen_active());
        lv_obj_set_pos(control, 93, 127);
        lv_obj_set_size(control, 54, 28);
        if (s_model.value) lv_obj_add_state(control, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(control, lv_color_hex(theme_color(0x34423e)), LV_PART_MAIN);
        lv_obj_set_style_bg_color(control, lv_color_hex(theme_color(0x86dbaf)), LV_PART_INDICATOR | LV_STATE_CHECKED);
    } else {
        lv_obj_t *slider = lv_slider_create(lv_screen_active());
        lv_obj_set_pos(slider, 24, 134);
        lv_obj_set_size(slider, 192, 8);
        lv_slider_set_range(slider, s_model.selected == UI_BRIGHTNESS ? 10 : 0,
                            s_model.selected == UI_TIMEOUT ? 600 : 100);
        lv_slider_set_value(slider, s_model.value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(theme_color(0x86dbaf)), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(theme_color(0x86dbaf)), LV_PART_KNOB);
    }
    if (s_model.selected == UI_SPEAKER || s_model.selected == UI_THEME)
        footer(s_model.value ? "filled-circle" : "circle", "Lưu", "Trở lại");
    else footer("-", "Lưu", "+");
    if (s_error) label(lv_screen_active(), s_error, 12, 188, 216, 0xf0a0a0);
}

static void render_subtitle(int state, const char *text)
{
    header(s_states[state]);
    s_subtitle_body = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_subtitle_body);
    lv_obj_set_pos(s_subtitle_body, 12, 58);
    lv_obj_set_size(s_subtitle_body, 216, 162);
    lv_obj_set_scroll_dir(s_subtitle_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_subtitle_body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *subtitle = label(s_subtitle_body, text, 0, 0, 216, 0xeeeeee);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
}

static const char *network_state(void)
{
    if (!s_network.ready) return "Wi-Fi chưa sẵn sàng";
    if (!s_network.enabled) return "Wi-Fi đã tắt";
    if (s_network.scanning) return "Đang quét mạng...";
    if (s_network.joining) return "Đang kết nối...";
    if (s_network.connected) return s_network.error == ESP_OK ? "Đã kết nối" : "Đã nối, lưu bị lỗi";
    if (s_network.error == ESP_ERR_INVALID_ARG) return "Kiểm tra mật khẩu";
    if (s_network.error != ESP_OK) return "Kết nối/quét bị lỗi";
    return "Chưa kết nối";
}

static void display_ssid(const char *ssid, char text[33])
{
    memcpy(text, ssid, 33);
    for (int i = 0; i < 32 && text[i]; ++i)
        if ((unsigned char)text[i] < 32 || (unsigned char)text[i] == 127) text[i] = '?';
    if (!hisu_ui_valid_utf8(text, 32))
        for (int i = 0; i < 32 && text[i]; ++i) if ((unsigned char)text[i] >= 128) text[i] = '?';
}

static void render_wifi(void)
{
    if (s_wifi_select_after_scan && !s_network.scanning) {
        s_wifi_select_after_scan = false;
        if (s_network.enabled && s_network.error == ESP_OK && s_network.count > 0)
            s_wifi_selected = s_wifi_first = 0;
    }
    static bool was_enabled;
    if (was_enabled != s_network.enabled) {
        s_wifi_selected = s_wifi_first = 0;
        was_enabled = s_network.enabled;
    }
    header("Wi-Fi");
    label(lv_screen_active(), s_error ? s_error : network_state(), 12, 44, 216,
          s_error || s_network.error != ESP_OK ? 0xf0a0a0 : 0x86dbaf);
    char status[48] = "";
    if (s_network.enabled && s_network.connected) snprintf(status, sizeof(status), "%s %ddB", s_network.ip, s_network.rssi);
    lv_obj_t *summary = label(lv_screen_active(), status, 12, 67, 216, 0x8c9690);
    lv_label_set_long_mode(summary, LV_LABEL_LONG_DOT);
    int count = s_network.enabled ? s_network.count : 0;
    int toggle = count + (s_network.enabled ? 1 : 0);
    int total = toggle + 2;
    if (s_wifi_selected >= total) s_wifi_selected = total - 1;
    if (s_wifi_selected < s_wifi_first) s_wifi_first = s_wifi_selected;
    if (s_wifi_selected >= s_wifi_first + 4) s_wifi_first = s_wifi_selected - 3;
    for (int row = 0; row < 4 && s_wifi_first + row < total; ++row) {
        int item = s_wifi_first + row;
        lv_obj_t *cell = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(cell);
        lv_obj_set_pos(cell, 8, 94 + row * 27);
        lv_obj_set_size(cell, 224, 26);
        lv_obj_set_style_radius(cell, 4, 0);
        if (item == s_wifi_selected) {
            lv_obj_set_style_bg_color(cell, lv_color_hex(theme_color(0x224b3d)), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        }
        const char *name = item < count ? s_network.aps[item].ssid :
            item < toggle ? "Quét lại" : item == toggle ?
            (s_network.enabled ? "Tắt Wi-Fi" : "Bật Wi-Fi") : "Trở lại";
        char shown_ssid[33];
        if (item < count) { display_ssid(name, shown_ssid); name = shown_ssid; }
        bool saved = item < count && s_network.aps[item].saved;
        lv_obj_t *text = label(cell, name, 7, 1, item < count ? 160 : 210,
                              saved ? 0x86dbaf : 0xeeeeee);
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        if (item < count) {
            create_wifi_icon(cell, 196, 5, wifi_signal_bars(s_network.aps[item].rssi), false);
        }
    }
    footer("v", "Chọn", "^");
}

static void render_password(void)
{
    header("Mật khẩu Wi-Fi");
    char shown_ssid[33]; display_ssid(s_join_ssid, shown_ssid);
    lv_obj_t *ssid = label(lv_screen_active(), shown_ssid, 12, 43, 216, 0x86dbaf);
    lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
    char masked[32];
    int shown = s_pass_length < 14 ? s_pass_length : 14;
    memset(masked, '*', shown); masked[shown] = 0;
    char status[64]; snprintf(status, sizeof(status), "%s %d/64", masked, s_pass_length);
    label(lv_screen_active(), s_error ? s_error : status, 12, 66, 216, s_error ? 0xf0a0a0 : 0x8c9690);
    for (int i = 0; i < 40; ++i) {
        lv_obj_t *cell = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(cell);
        lv_obj_set_pos(cell, 4 + (i % 8) * 29, 94 + (i / 8) * 22);
        lv_obj_set_size(cell, 28, 22);
        lv_obj_set_style_radius(cell, 3, 0);
        if (i == s_pass_choice) {
            lv_obj_set_style_bg_color(cell, lv_color_hex(theme_color(0x224b3d)), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        }
        char text[4] = {0};
        if (i < 36) {
            text[0] = s_keysets[s_pass_mode][i];
            if (text[0] == ' ') memcpy(text, "SP", 3);
        } else snprintf(text, sizeof(text), "%s", i == 36 ? "Aa" : i == 37 ? "<" : i == 38 ? "OK" : "X");
        lv_obj_t *key = label(cell, text, 0, 0, 28, i < 36 ? 0xeeeeee : 0x86dbaf);
        lv_obj_set_style_text_align(key, LV_TEXT_ALIGN_CENTER, 0);
    }
    const char *action = s_pass_choice < 36 ? "Thêm" : s_pass_choice == 36 ? "Đổi" :
        s_pass_choice == 37 ? "Xóa" : s_pass_choice == 38 ? "Nối" : "Hủy";
    footer(">", action, "v");
}

static bool handle_wifi_key(hisu_key_t key)
{
    if (s_model.page != UI_WIFI_PAGE && s_model.page != UI_WIFI_PASSWORD) return false;
    if (key == HISU_KEY_BACK) {
        s_wifi_select_after_scan = false;
        memset(s_password, 0, sizeof(s_password)); s_pass_length = 0;
        s_model.page = s_model.page == UI_WIFI_PASSWORD ? UI_WIFI_PAGE : UI_MENU;
        s_error = NULL; render(); return true;
    }
    s_error = NULL;
    if (s_model.page == UI_WIFI_PAGE) {
        int count = s_network.enabled ? s_network.count : 0;
        int toggle = count + (s_network.enabled ? 1 : 0);
        int total = toggle + 2;
        if (key == HISU_KEY_DOWN) s_wifi_selected = (s_wifi_selected + 1) % total;
        if (key == HISU_KEY_UP) s_wifi_selected = (s_wifi_selected + total - 1) % total;
        if (key == HISU_KEY_BOOT) {
            if (s_wifi_selected < count) {
                hisu_wifi_ap_t *ap = &s_network.aps[s_wifi_selected];
                if (!s_network.enabled) s_error = "Hãy bật Wi-Fi";
                else if (!ap->supported) s_error = "Mạng không hỗ trợ";
                else if (s_network.joining || s_network.scanning) s_error = "Wi-Fi đang bận";
                else {
                    memcpy(s_join_ssid, ap->ssid, sizeof(s_join_ssid));
                    bool retry_password = s_network.error == ESP_ERR_INVALID_ARG && !strcmp(s_network.ssid, ap->ssid);
                    if (ap->secure && ap->saved && !retry_password) {
                        char saved_password[65] = {0};
                        esp_err_t err = hisu_settings_find_wifi(ap->ssid, saved_password);
                        if (err == ESP_OK) err = hisu_network_join(ap->ssid, saved_password);
                        memset(saved_password, 0, sizeof(saved_password));
                        if (err != ESP_OK) s_error = "Chưa thể kết nối";
                    } else if (ap->secure) {
                        memset(s_password, 0, sizeof(s_password)); s_pass_length = 0;
                        s_pass_mode = 0; s_pass_choice = 0; s_model.page = UI_WIFI_PASSWORD;
                    } else if (hisu_network_join(s_join_ssid, "") != ESP_OK) s_error = "Chưa thể kết nối";
                }
            } else if (s_wifi_selected < toggle) {
                if (hisu_network_scan() != ESP_OK) s_error = "Chưa thể quét mạng";
                else s_wifi_select_after_scan = true;
            } else if (s_wifi_selected == toggle) {
                if (hisu_network_enable(!s_network.enabled) != ESP_OK) s_error = "Wi-Fi đang bận";
            } else s_model.page = UI_MENU;
        }
        if (s_model.page != UI_WIFI_PAGE) s_wifi_select_after_scan = false;
    } else {
        if (key == HISU_KEY_DOWN || key == HISU_KEY_DOWN_HELD)
            s_pass_choice = (s_pass_choice / 8) * 8 + (s_pass_choice + 1) % 8;
        if (key == HISU_KEY_UP || key == HISU_KEY_UP_HELD)
            s_pass_choice = ((s_pass_choice / 8 + 1) % 5) * 8 + s_pass_choice % 8;
        if (key == HISU_KEY_BOOT) {
            if (s_pass_choice < 36 && s_pass_length < 64) {
                s_password[s_pass_length++] = s_keysets[s_pass_mode][s_pass_choice];
                s_password[s_pass_length] = 0;
            } else if (s_pass_choice == 36) s_pass_mode = (s_pass_mode + 1) % 3;
            else if (s_pass_choice == 37 && s_pass_length) s_password[--s_pass_length] = 0;
            else if (s_pass_choice == 38) {
                if (!s_pass_length || !hisu_network_valid_password(s_password)) s_error = "Cần 8-63 ký tự";
                else if (hisu_network_join(s_join_ssid, s_password) != ESP_OK) s_error = "Wi-Fi đang bận";
                else {
                    memset(s_password, 0, sizeof(s_password)); s_pass_length = 0; s_model.page = UI_WIFI_PAGE;
                }
            } else if (s_pass_choice == 39) {
                memset(s_password, 0, sizeof(s_password)); s_pass_length = 0; s_model.page = UI_WIFI_PAGE;
            }
        }
    }
    render(); return true;
}

static void render(void)
{
    hisu_network_get(&s_network);
    hisu_power_get(&s_power);
    lv_obj_t *root = lv_screen_active();
    s_light_theme = (s_model.page == UI_EDIT && s_model.selected == UI_THEME)
        ? s_model.value != 0 : hisu_settings_get(HISU_SETTING_THEME) == 1;
    lv_obj_clean(root);
    refresh_wallpaper();
    s_idle_gif = NULL;
    s_idle_clock = NULL;
    s_subtitle_body = NULL;
    s_battery_body = s_battery_tip = s_battery_charge = NULL;
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(theme_color(0x000000)), 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    switch (s_model.page) {
    case UI_MENU: render_menu(); break;
    case UI_EDIT: render_edit(); break;
    case UI_CALENDAR_PAGE: render_calendar(); break;
    case UI_SETUP_PAGE: render_setup(); break;
    case UI_FLASHLIGHT_PAGE:
        lv_obj_set_style_bg_color(root,lv_color_hex(0xffffff),0);
        footer(s_flashlight_enabled ? "filled-circle" : "circle",
               s_flashlight_mode == 0 ? "Sáng" : s_flashlight_mode == 1 ? "Chớp" : "SOS", "Trở lại");
        break;
    case UI_INFO:
        header(s_items[s_model.selected]);
        if (s_model.selected == UI_BATTERY && s_power.valid) {
            char text[64];
            snprintf(text, sizeof(text), "Pin: ~%d%%", s_power.percent);
            label(root, text, 12, 48, 216, 0x86dbaf);
            snprintf(text, sizeof(text), "ADC: %d", s_power.raw_adc);
            label(root, text, 12, 71, 216, 0xeeeeee);
            snprintf(text, sizeof(text), "Nguồn ngoài: %s", s_power.external_power ? "Có" : "Không");
            label(root, text, 12, 94, 216, 0xeeeeee);
            if (s_power.external_power) snprintf(text, sizeof(text), "Còn lại: Cắm sạc");
            else if (s_power.remaining_valid)
                snprintf(text, sizeof(text), "Còn lại: ~%" PRIu64 "h%02" PRIu64 "p",
                         s_power.remaining_seconds / 3600, s_power.remaining_seconds / 60 % 60);
            else snprintf(text, sizeof(text), "Còn lại: Đang đo");
            label(root, text, 12, 117, 216, 0x86dbaf);
            usage_text(text, sizeof(text), "MH sáng", s_power.screen_on_seconds);
            label(root, text, 12, 140, 216, 0xeeeeee);
            usage_text(text, sizeof(text), "Dùng pin", s_power.battery_seconds);
            label(root, text, 12, 163, 216, 0xeeeeee);
            usage_text(text, sizeof(text), "Sleep", s_power.screen_sleep_seconds);
            label(root, text, 12, 186, 216, 0xeeeeee);
        } else label(root, "Không khả dụng", 12, 83, 216, 0xa1a9a5);
        footer("back", "", "");
        break;
    case UI_WIFI_PAGE: render_wifi(); break;
    case UI_WIFI_PASSWORD: render_password(); break;
    case UI_PREVIEW: render_subtitle(HISU_UI_LISTENING + s_model.preview, s_demo[s_model.preview]); break;
    case UI_IDLE:
        if (s_status.status != HISU_UI_IDLE) render_subtitle(s_status.status, s_status.text);
        else {
            if (s_wallpaper_available) {
                lv_obj_t *image = lv_image_create(root);
                lv_image_set_src(image,&s_wallpaper_image); lv_obj_center(image);
            } else {
                s_idle_gif = lv_gif_create(root);
                lv_gif_set_color_format(s_idle_gif, LV_COLOR_FORMAT_RGB565);
                lv_gif_set_src(s_idle_gif, &s_idle_cat[s_light_theme ? 1 : 0]);
                lv_obj_center(s_idle_gif);
                if (lv_gif_is_loaded(s_idle_gif)) lv_gif_set_loop_count(s_idle_gif, -1);
                else ESP_LOGE("UI", "Idle GIF could not be loaded");
            }
            render_idle_status(root);
        }
        break;
    }
}

static void handle_key(hisu_key_t key)
{
    if (!key) return;
    s_activity_ms = esp_timer_get_time() / 1000;
    if (s_asleep) { s_asleep = false; return; }
    if (s_model.page == UI_SETUP_PAGE) {
        hisu_media_status_t media; hisu_media_get(&media);
        int view = s_model.preview;
        hisu_ui_model_key(&s_model, key);
        if (key == HISU_KEY_BOOT) {
            if (view == UI_SETUP_STOP) hisu_media_close();
            else if (view == UI_SETUP_RESET) hisu_media_restore_default();
            else if (view == UI_SETUP_QR && !media.enabled) {
                s_model.preview = UI_SETUP_QR;
                s_setup_url = false;
                hisu_media_open();
            }
        }
        render(); return;
    }
    if (s_model.page == UI_FLASHLIGHT_PAGE) {
        if (key == HISU_KEY_UP || key == HISU_KEY_BACK) {
            s_flashlight_enabled = false;
            s_model.page = UI_MENU;
        } else if (key == HISU_KEY_DOWN) {
            s_flashlight_enabled = !s_flashlight_enabled;
            s_flashlight_start_ms = esp_timer_get_time()/1000;
        } else if (key == HISU_KEY_BOOT) {
            s_flashlight_mode = (s_flashlight_mode+1)%3;
            s_flashlight_enabled = true;
            s_flashlight_start_ms = esp_timer_get_time()/1000;
        }
        render(); return;
    }
    if (handle_wifi_key(key)) return;
    if (s_model.page == UI_CALENDAR_PAGE) {
        int d,m,y;
        if (key == HISU_KEY_BACK) s_model.page = UI_MENU;
        else if (key == HISU_KEY_BOOT) s_calendar_day = 0;
        else if (hisu_clock_vietnam_date(&d,&m,&y)) {
            int jd = s_calendar_day ? s_calendar_day : hisu_calendar_jdn(d,m,y);
            if (key == HISU_KEY_DOWN || key == HISU_KEY_DOWN_HELD) --jd;
            if (key == HISU_KEY_UP || key == HISU_KEY_UP_HELD) ++jd;
            if (jd >= hisu_calendar_jdn(1,1,2024) && jd <= hisu_calendar_jdn(31,12,2099)) s_calendar_day = jd;
        }
        render(); return;
    }
    if (s_model.page == UI_IDLE && s_status.status != HISU_UI_IDLE && s_subtitle_body &&
        (key == HISU_KEY_UP || key == HISU_KEY_DOWN)) {
        lv_obj_scroll_by(s_subtitle_body, 0, key == HISU_KEY_UP ? 44 : -44, LV_ANIM_OFF);
        return;
    }
    int previous = s_model.page;
    int old_value = s_model.value;
    s_error = NULL;
    // Confirm persists once; held BOOT cancels and restores live brightness.
    if (previous == UI_EDIT && key == HISU_KEY_BOOT) {
        esp_err_t err = hisu_settings_set((hisu_setting_t)s_model.selected, s_model.value);
        if (err != ESP_OK) {
            s_error = "Lưu thất bại";
            ESP_LOGW("UI", "Save setting: %s", esp_err_to_name(err));
            render();
            return;
        }
    }
    hisu_ui_model_key(&s_model, key);
    if (previous == UI_MENU && s_model.page == UI_SETUP_PAGE) {
        s_model.preview = UI_SETUP_QR;
        s_setup_url = false;
        hisu_media_open();
    }
    if (previous == UI_MENU && s_model.page == UI_CALENDAR_PAGE) s_calendar_day = 0;
    if (previous == UI_MENU && s_model.page == UI_FLASHLIGHT_PAGE) {
        s_flashlight_enabled = true;
        s_flashlight_mode = 0;
        s_flashlight_start_ms = esp_timer_get_time()/1000;
    }
    if (previous == UI_MENU && s_model.selected == UI_SCREEN_OFF && key == HISU_KEY_BOOT)
        s_asleep = true;
    if (previous == UI_MENU && s_model.page == UI_WIFI_PAGE) {
        s_wifi_selected = 0; s_wifi_first = 0;
        hisu_network_get(&s_network);
        if (s_network.enabled && !s_network.joining && !s_network.scanning && s_network.ready)
            hisu_network_scan();
    }
    if (previous != UI_EDIT && s_model.page == UI_EDIT) {
        s_model.value = hisu_settings_get((hisu_setting_t)s_model.selected);
    }
    if (s_model.page != previous || s_model.value != old_value || previous == UI_MENU || previous == UI_PREVIEW) render();
}

static void render_boot_splash(void)
{
    s_boot_image = (lv_image_dsc_t) {
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW, .w = 240, .h = 240},
        .data_size = (uint32_t)(boot_windows_xp_end - boot_windows_xp_start),
        .data = boot_windows_xp_start,
    };
    lv_obj_t *root = lv_screen_active();
    lv_obj_clean(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *gif = lv_gif_create(root);
    lv_gif_set_color_format(gif, LV_COLOR_FORMAT_RGB565);
    lv_gif_set_src(gif, &s_boot_image);
    if (!lv_gif_is_loaded(gif)) {
        ESP_LOGW("UI", "Boot GIF unavailable; showing black splash without overlays");
        lv_obj_delete(gif);
        return;
    }
    lv_obj_center(gif);
    lv_gif_set_loop_count(gif, -1);
    ESP_LOGI("UI", "Boot GIF loaded: 240x240; duration=%d ms; no overlays", HISU_BOOT_SPLASH_MS);
}

static void ui_task(void *arg)
{
    (void)arg;
    lv_init();
    s_idle_cat[0] = (lv_image_dsc_t) {
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW, .w = 240, .h = 240},
        .data_size = (uint32_t)(idle_cat_dark_end - idle_cat_dark_start),
        .data = idle_cat_dark_start,
    };
    s_idle_cat[1] = s_idle_cat[0];
    s_idle_cat[1].data_size = (uint32_t)(idle_cat_light_end - idle_cat_light_start);
    s_idle_cat[1].data = idle_cat_light_start;
    lv_tick_set_cb(tick_ms);
    lv_display_t *display = lv_display_create(HISU_DISPLAY_WIDTH, HISU_DISPLAY_HEIGHT);
    size_t buffer_size = HISU_DISPLAY_WIDTH * 40 * 2;
    void *buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!display || !buffer) {
        if (display) lv_display_delete(display);
        heap_caps_free(buffer);
        ESP_LOGE("UI", "Display allocation failed");
        s_startup_result = ESP_ERR_NO_MEM;
        xEventGroupSetBits(s_startup, 1);
        vTaskDelete(NULL);
        return;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    esp_err_t err = hisu_board_init(lcd_done, display);
    if (err != ESP_OK) {
        ESP_LOGE("UI", "Board initialization failed: %s", esp_err_to_name(err));
        lv_display_delete(display);
        heap_caps_free(buffer);
        s_startup_result = err;
        xEventGroupSetBits(s_startup, 1);
        vTaskDelete(NULL);
        return;
    }
    render_boot_splash();
    /* Draw with backlight off; display-on drains the final asynchronous DMA. */
    lv_refr_now(display);
    err = s_first_frame_error;
    if (err == ESP_OK) err = hisu_board_display_show();
    int boot_brightness = hisu_settings_get(HISU_SETTING_BRIGHTNESS);
    if (err == ESP_OK) err = hisu_board_set_brightness(boot_brightness);
    if (err != ESP_OK) {
        (void)hisu_board_set_brightness(0);
        ESP_LOGE("UI", "First boot frame failed: %s", esp_err_to_name(err));
        s_startup_result = err;
        xEventGroupSetBits(s_startup, 1);
        vTaskDelete(NULL);
        return;
    }
    s_applied_brightness = boot_brightness;
    hisu_power_set_screen_on(boot_brightness > 0);
    bool boot_splash = true;
    int64_t boot_until_ms = esp_timer_get_time() / 1000 + HISU_BOOT_SPLASH_MS;
    s_startup_result = ESP_OK;
    xEventGroupSetBits(s_startup, 1);
    lv_timer_handler();
    s_activity_ms = esp_timer_get_time() / 1000;
    ESP_LOGI("UI", "Ready: animated idle, menu and subtitles");
    int64_t s_last_service_refresh = 0;
    hisu_media_status_t initial_media; hisu_media_get(&initial_media);
    uint32_t seen_wallpaper_revision = initial_media.revision;
    for (;;) {
        if (boot_splash) {
            /* Discard splash inputs rather than replaying them into the menu. */
            (void)hisu_board_poll_key();
            if (esp_timer_get_time() / 1000 < boot_until_ms) {
                lv_timer_handler();
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            boot_splash = false;
            s_activity_ms = esp_timer_get_time() / 1000;
            render();
        }
        hisu_board_enable_volume_hold((s_model.page == UI_WIFI_PASSWORD || s_model.page == UI_CALENDAR_PAGE) && !s_asleep);
        hisu_board_enable_menu_close_hold(s_model.page == UI_MENU && !s_asleep);
        handle_key(hisu_board_poll_key());
        status_event_t event;
        bool changed = false;
        // Bound draining so a fast producer cannot starve input/rendering.
        for (int i = 0; i < 8 && xQueueReceive(s_events, &event, 0) == pdTRUE; ++i) {
            s_status = event;
            changed = true;
        }
        if (changed) {
            s_asleep = false;
            s_activity_ms = esp_timer_get_time() / 1000;
            if (s_model.page == UI_IDLE) render();
        }
        int timeout = hisu_settings_get(HISU_SETTING_TIMEOUT);
        int64_t now = esp_timer_get_time() / 1000;
        if (now - s_last_service_refresh >= 1000) {
            s_last_service_refresh = now;
            hisu_media_status_t media; hisu_media_get(&media);
            /* Revision advances only after the new wallpaper is persisted successfully. */
            if (media.revision != seen_wallpaper_revision) {
                seen_wallpaper_revision = media.revision;
                s_model.page = UI_IDLE;
                s_asleep = false;
                s_activity_ms = now;
                render();
            } else if (s_model.page == UI_MENU || s_model.page == UI_WIFI_PAGE || s_model.page == UI_CALENDAR_PAGE ||
                s_model.page == UI_SETUP_PAGE ||
                (s_model.page == UI_INFO && s_model.selected == UI_BATTERY)) render();
            else if (s_model.page == UI_IDLE && s_status.status == HISU_UI_IDLE) {
                if (media.revision != s_wallpaper_revision || media.has_image != s_wallpaper_available ||
                    s_light_theme != (hisu_settings_get(HISU_SETTING_THEME) == 1)) render();
                else update_idle_status();
            }
        }
        // Never turn the screen off during settings or active conversation.
        if (timeout && s_model.page == UI_IDLE && s_status.status == HISU_UI_IDLE && now - s_activity_ms >= timeout * 1000LL) {
            s_asleep = true;
        }
        int brightness = s_model.page == UI_FLASHLIGHT_PAGE ? flashlight_brightness(now) :
            s_asleep ? 0 : s_model.page == UI_EDIT && s_model.selected == UI_BRIGHTNESS
            ? s_model.value : hisu_settings_get(HISU_SETTING_BRIGHTNESS);
        if (brightness != s_applied_brightness) {
            err = hisu_board_set_brightness(brightness);
            if (err == ESP_OK) {
                s_applied_brightness = brightness;
                hisu_power_set_screen_on(brightness > 0);
            }
            else ESP_LOGW("UI", "Backlight: %s", esp_err_to_name(err));
        }
        if (s_idle_gif) {
            if (s_asleep) lv_gif_pause(s_idle_gif);
            else lv_gif_resume(s_idle_gif);
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t hisu_ui_start(void)
{
    if (s_events) return ESP_ERR_INVALID_STATE;
    s_events = xQueueCreate(8, sizeof(status_event_t));
    if (!s_events) return ESP_ERR_NO_MEM;
    s_startup = xEventGroupCreate();
    if (!s_startup) {
        vQueueDelete(s_events);
        s_events = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(ui_task, "hisu_ui", 8192, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_events);
        s_events = NULL;
        vEventGroupDelete(s_startup);
        s_startup = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (!(xEventGroupWaitBits(s_startup, 1, pdFALSE, pdTRUE, pdMS_TO_TICKS(5000)) & 1)) {
        return ESP_ERR_TIMEOUT;
    }
    return s_startup_result;
}

esp_err_t hisu_ui_post_status(hisu_ui_status_t status, const char *subtitle)
{
    if (!s_events || !s_startup || !(xEventGroupGetBits(s_startup) & 1) || s_startup_result != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (status < HISU_UI_IDLE || status > HISU_UI_ERROR || !hisu_ui_valid_utf8(subtitle, 255)) {
        return ESP_ERR_INVALID_ARG;
    }
    status_event_t event = {.status = status};
    memcpy(event.text, subtitle, strlen(subtitle) + 1);
    return xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
