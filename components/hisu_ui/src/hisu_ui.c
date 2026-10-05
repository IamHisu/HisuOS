#include "hisu_ui.h"
#include "hisu_ui_model.h"
#include "hisu_board.h"
#include "hisu_settings.h"
#include "hisu_network.h"
#include "hisu_power.h"

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

_Static_assert((int)UI_THEME == (int)HISU_SETTING_THEME, "Editable menu items must match settings keys");

LV_FONT_DECLARE(hisu_font_vi_semibold_16);
/* ESP-IDF EMBED_FILES symbols use the filename, without its directory. */
extern const uint8_t idle_cat_dark_start[] asm("_binary_idle_cat_dark_gif_start");
extern const uint8_t idle_cat_dark_end[] asm("_binary_idle_cat_dark_gif_end");
extern const uint8_t idle_cat_light_start[] asm("_binary_idle_cat_light_gif_start");
extern const uint8_t idle_cat_light_end[] asm("_binary_idle_cat_light_gif_end");
static lv_image_dsc_t s_idle_cat[2];
static lv_obj_t *s_idle_gif;
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
typedef struct { lv_obj_t *root, *arcs[3], *dot; } wifi_icon_t;
static wifi_icon_t s_idle_wifi;
static int s_wifi_selected, s_wifi_first;
static bool s_wifi_select_after_scan;
static char s_join_ssid[33], s_password[65];
static int s_pass_length, s_pass_mode, s_pass_choice;
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
    "Độ sáng", "Âm lượng", "Loa", "Thời gian chờ", "Theme", "Wi-Fi", "Bluetooth",
    "Pin", "Tắt màn hình", "Đóng menu",
};
static const char *const s_states[] = {"", "Đang nghe", "Đang suy nghĩ", "Đang nói", "Lỗi"};
static const char *const s_demo[] = {
    "Hôm nay bạn muốn làm gì?",
    "Mình đang chuẩn bị câu trả lời.",
    "Chào bạn!\nHôm nay trời trong xanh. Một ngày mới bắt đầu với những ý tưởng mới.",
};

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

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

static void update_wifi_icon(wifi_icon_t *icon, int bars, bool visible)
{
    if (visible) lv_obj_remove_flag(icon->root, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(icon->root, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; ++i) {
        lv_obj_set_style_arc_color(icon->arcs[i], lv_color_hex(theme_color(0x86dbaf)), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(icon->arcs[i], i < bars ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
    }
    lv_obj_set_style_bg_color(icon->dot, lv_color_hex(theme_color(0x86dbaf)), 0);
    lv_obj_set_style_bg_opa(icon->dot, bars ? LV_OPA_COVER : LV_OPA_30, 0);
}

static wifi_icon_t create_wifi_icon(lv_obj_t *parent, int x, int y, int bars)
{
    wifi_icon_t icon = {0};
    icon.root = lv_obj_create(parent);
    lv_obj_remove_style_all(icon.root);
    lv_obj_set_pos(icon.root, x, y);
    lv_obj_set_size(icon.root, 20, 16);
    lv_obj_remove_flag(icon.root, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 3; ++i) {
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
    icon.dot = lv_obj_create(icon.root);
    lv_obj_remove_style_all(icon.dot);
    lv_obj_set_pos(icon.dot, 9, 10);
    lv_obj_set_size(icon.dot, 2, 2);
    lv_obj_set_style_radius(icon.dot, LV_RADIUS_CIRCLE, 0);
    update_wifi_icon(&icon, bars, true);
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

/* Update the status overlay independently so polling never restarts the GIF. */
static void update_idle_status(void)
{
    if (!s_battery_body) return;
    hisu_power_get(&s_power);
    hisu_network_get(&s_network);
    int percent = s_power.percent < 0 ? 0 : s_power.percent > 100 ? 100 : s_power.percent;
    lv_color_t color = lv_color_hex(theme_color(s_power.valid && percent <= 20 && !s_power.external_power
                                               ? 0xf0a0a0 : 0x86dbaf));
    lv_obj_set_style_border_color(s_battery_body, color, 0);
    lv_obj_set_style_bg_color(s_battery_tip, color, 0);
    int bars = s_power.valid ? (percent + 19) / 20 : 0;
    for (int i = 0; i < 5; ++i) {
        lv_obj_set_style_bg_color(s_battery_bars[i], color, 0);
        if (i < bars && !s_power.external_power) lv_obj_remove_flag(s_battery_bars[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_battery_bars[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_line_color(s_battery_charge, color, 0);
    if (s_power.valid && s_power.external_power) lv_obj_remove_flag(s_battery_charge, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_battery_charge, LV_OBJ_FLAG_HIDDEN);
    update_wifi_icon(&s_idle_wifi, s_network.connected ? wifi_signal_bars(s_network.rssi) : 0,
                     s_network.enabled);
}

static void render_idle_status(lv_obj_t *root)
{
    lv_obj_t *panel = lv_obj_create(root);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, 174, 6);
    lv_obj_set_size(panel, 60, 22);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(theme_color(0x000000)), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    /* Visible Wi-Fi pixels span 12 px; center them on the 14 px battery. */
    s_idle_wifi = create_wifi_icon(panel, 3, 6, 0);
    s_battery_body = lv_obj_create(panel);
    lv_obj_remove_style_all(s_battery_body);
    lv_obj_set_pos(s_battery_body, 27, 5);
    lv_obj_set_size(s_battery_body, 24, 14);
    lv_obj_set_style_border_width(s_battery_body, 2, 0);
    lv_obj_set_style_radius(s_battery_body, 3, 0);
    lv_obj_remove_flag(s_battery_body, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 5; ++i) {
        s_battery_bars[i] = lv_obj_create(panel);
        lv_obj_remove_style_all(s_battery_bars[i]);
        lv_obj_set_pos(s_battery_bars[i], 32 + i * 3, 9);
        lv_obj_set_size(s_battery_bars[i], 2, 6);
        lv_obj_set_style_radius(s_battery_bars[i], 1, 0);
        lv_obj_set_style_bg_opa(s_battery_bars[i], LV_OPA_COVER, 0);
    }
    s_battery_tip = lv_obj_create(panel);
    lv_obj_remove_style_all(s_battery_tip);
    lv_obj_set_pos(s_battery_tip, 51, 10);
    lv_obj_set_size(s_battery_tip, 2, 4);
    lv_obj_set_style_bg_opa(s_battery_tip, LV_OPA_COVER, 0);
    static const lv_point_precise_t bolt[] = {{3, 0}, {1, 4}, {5, 4}, {3, 8}};
    s_battery_charge = lv_line_create(panel);
    lv_obj_remove_style_all(s_battery_charge);
    lv_line_set_points(s_battery_charge, bolt, 4);
    lv_obj_set_pos(s_battery_charge, 36, 8);
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
        /* Compact return arrow: left arrowhead, curved elbow, bottom tail. */
        static const lv_point_precise_t return_path[] = {
            {1, 3}, {6, 3}, {8, 4}, {9, 6}, {9, 8}, {8, 10}, {6, 11}, {3, 11},
        };
        static const lv_point_precise_t return_head[] = {{4, 0}, {1, 3}, {4, 6}};
        const lv_point_precise_t *paths[] = {return_path, return_head};
        const uint32_t lengths[] = {8, 3};
        for (int i = 0; i < 2; ++i) {
            lv_obj_t *line = lv_line_create(back);
            lv_obj_remove_style_all(line);
            lv_line_set_points(line, paths[i], lengths[i]);
            lv_obj_set_style_line_width(line, 1, 0);
            lv_obj_set_style_line_rounded(line, true, 0);
            lv_obj_set_style_line_color(line, lv_color_hex(theme_color(0x86dbaf)), 0);
        }
        return;
    }
    static const lv_point_precise_t down[] = {{0, 0}, {6, 6}, {12, 0}};
    static const lv_point_precise_t up[] = {{0, 6}, {6, 0}, {12, 6}};
    static const lv_point_precise_t next[] = {{0, 0}, {6, 6}, {0, 12}};
    static const lv_point_precise_t previous[] = {{6, 0}, {0, 6}, {6, 12}};
    const char *texts[] = {left, middle, right};
    for (int i = 0; i < 3; ++i) {
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
              item == UI_BLE ? 0xa1a9a5 : 0xeeeeee);
        if (item == UI_THEME) {
            create_theme_icon(obj, hisu_settings_get(HISU_SETTING_THEME) == 1, item == s_model.selected);
            continue;
        }
        if (item == UI_WIFI) {
            wifi_icon_t icon = create_wifi_icon(obj, 196, 8,
                s_network.enabled && s_network.connected ? wifi_signal_bars(s_network.rssi) : 0);
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
            create_wifi_icon(cell, 196, 5, wifi_signal_bars(s_network.aps[item].rssi));
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
    footer("<", action, ">");
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
        if (key == HISU_KEY_DOWN) s_pass_choice = (s_pass_choice / 8) * 8 + (s_pass_choice + 7) % 8;
        if (key == HISU_KEY_UP) s_pass_choice = (s_pass_choice / 8) * 8 + (s_pass_choice + 1) % 8;
        if (key == HISU_KEY_DOWN_HELD) s_pass_choice = ((s_pass_choice / 8 + 1) % 5) * 8 + s_pass_choice % 8;
        if (key == HISU_KEY_UP_HELD) s_pass_choice = ((s_pass_choice / 8 + 4) % 5) * 8 + s_pass_choice % 8;
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
    s_idle_gif = NULL;
    s_subtitle_body = NULL;
    s_battery_body = s_battery_tip = s_battery_charge = NULL;
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(theme_color(0x000000)), 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    switch (s_model.page) {
    case UI_MENU: render_menu(); break;
    case UI_EDIT: render_edit(); break;
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
        footer("Trở lại", "", "");
        break;
    case UI_WIFI_PAGE: render_wifi(); break;
    case UI_WIFI_PASSWORD: render_password(); break;
    case UI_PREVIEW: render_subtitle(HISU_UI_LISTENING + s_model.preview, s_demo[s_model.preview]); break;
    case UI_IDLE:
        if (s_status.status != HISU_UI_IDLE) render_subtitle(s_status.status, s_status.text);
        else {
            s_idle_gif = lv_gif_create(root);
            lv_gif_set_color_format(s_idle_gif, LV_COLOR_FORMAT_RGB565);
            lv_gif_set_src(s_idle_gif, &s_idle_cat[s_light_theme ? 1 : 0]);
            lv_obj_center(s_idle_gif);
            if (lv_gif_is_loaded(s_idle_gif)) lv_gif_set_loop_count(s_idle_gif, -1);
            else ESP_LOGE("UI", "Idle GIF could not be loaded");
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
    if (handle_wifi_key(key)) return;
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
    render();
    s_startup_result = ESP_OK;
    xEventGroupSetBits(s_startup, 1);
    lv_timer_handler();
    s_activity_ms = esp_timer_get_time() / 1000;
    ESP_LOGI("UI", "Ready: animated idle, menu and subtitles");
    int64_t s_last_service_refresh = 0;
    for (;;) {
        hisu_board_enable_volume_hold(s_model.page == UI_WIFI_PASSWORD && !s_asleep);
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
            if (s_model.page == UI_MENU || s_model.page == UI_WIFI_PAGE ||
                (s_model.page == UI_INFO && s_model.selected == UI_BATTERY)) render();
            else if (s_model.page == UI_IDLE && s_status.status == HISU_UI_IDLE) update_idle_status();
        }
        // Never turn the screen off during settings or active conversation.
        if (timeout && s_model.page == UI_IDLE && s_status.status == HISU_UI_IDLE && now - s_activity_ms >= timeout * 1000LL) {
            s_asleep = true;
        }
        int brightness = s_asleep ? 0 : s_model.page == UI_EDIT && s_model.selected == UI_BRIGHTNESS
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
