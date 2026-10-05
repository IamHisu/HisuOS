#include "hisu_ui.h"
#include "hisu_ui_model.h"
#include "hisu_board.h"
#include "hisu_settings.h"

#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lvgl.h"

LV_FONT_DECLARE(friend_font_vietnamese_16);

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
static const char *const s_items[] = {
    "Độ sáng", "Âm lượng", "Loa", "Tắt màn hình", "Wi-Fi", "Bluetooth",
    "Pin", "Kiểm tra phụ đề", "Đóng",
};
static const char *const s_states[] = {"", "Đang nghe", "Đang suy nghĩ", "Đang nói", "Lỗi"};
static const char *const s_demo[] = {
    "Hôm nay bạn muốn làm gì?",
    "Mình đang chuẩn bị câu trả lời.",
    "Chào bạn!\nHôm nay trời trong xanh. Một ngày mới bắt đầu với những ý tưởng mới.",
};

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

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
    lv_obj_set_style_text_font(obj, &friend_font_vietnamese_16, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
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
    lv_obj_set_style_bg_color(line, lv_color_hex(0x34423e), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
}

static void value_text(int item, int value, char *out, size_t size)
{
    if (item == UI_SPEAKER) snprintf(out, size, "%s", value ? "Bật" : "Tắt");
    else if (item == UI_TIMEOUT && value == 0) snprintf(out, size, "Tắt");
    else if (item == UI_TIMEOUT) snprintf(out, size, "%ds", value);
    else snprintf(out, size, "%d%%", value);
}

static void render_menu(void)
{
    header("Hisu");
    int start = s_model.selected < 5 ? 0 : UI_ITEM_COUNT - 5;
    for (int row = 0; row < 5; ++row) {
        int item = start + row;
        lv_obj_t *obj = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(obj);
        lv_obj_set_pos(obj, 8, 47 + row * 31);
        lv_obj_set_size(obj, 224, 30);
        lv_obj_set_style_radius(obj, 4, 0);
        if (item == s_model.selected) {
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x224b3d), 0);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        }
        label(obj, s_items[item], 7, 3, 158, item >= UI_WIFI && item <= UI_BATTERY ? 0xa1a9a5 : 0xeeeeee);
        char value[16] = "";
        if (item <= UI_TIMEOUT) value_text(item, hisu_settings_get((hisu_setting_t)item), value, sizeof(value));
        else if (item <= UI_BATTERY) snprintf(value, sizeof(value), "--");
        else snprintf(value, sizeof(value), "%s", LV_SYMBOL_RIGHT);
        lv_obj_t *v = label(obj, value, 164, 3, 54, 0x86dbaf);
        if (item > UI_BATTERY) lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    }
    char position[16];
    snprintf(position, sizeof(position), "%d / %d", s_model.selected + 1, UI_ITEM_COUNT);
    label(lv_screen_active(), s_error ? s_error : position, 12, 215, 216, s_error ? 0xf0a0a0 : 0x8c9690);
}

static void render_edit(void)
{
    header(s_items[s_model.selected]);
    char value[16];
    value_text(s_model.selected, s_model.value, value, sizeof(value));
    lv_obj_t *v = label(lv_screen_active(), value, 12, 73, 216, 0x86dbaf);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    if (s_model.selected == UI_SPEAKER) {
        lv_obj_t *control = lv_switch_create(lv_screen_active());
        lv_obj_set_pos(control, 93, 127);
        lv_obj_set_size(control, 54, 28);
        if (s_model.value) lv_obj_add_state(control, LV_STATE_CHECKED);
    } else {
        lv_obj_t *slider = lv_slider_create(lv_screen_active());
        lv_obj_set_pos(slider, 24, 134);
        lv_obj_set_size(slider, 192, 8);
        lv_slider_set_range(slider, s_model.selected == UI_BRIGHTNESS ? 10 : 0,
                            s_model.selected == UI_TIMEOUT ? 600 : 100);
        lv_slider_set_value(slider, s_model.value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x86dbaf), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x86dbaf), LV_PART_KNOB);
    }
    lv_obj_t *save = label(lv_screen_active(), s_error ? s_error : "Lưu", 12, 200, 216,
                           s_error ? 0xf0a0a0 : 0xeeeeee);
    lv_obj_set_style_text_align(save, LV_TEXT_ALIGN_CENTER, 0);
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

static void render(void)
{
    lv_obj_t *root = lv_screen_active();
    lv_obj_clean(root);
    s_subtitle_body = NULL;
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    switch (s_model.page) {
    case UI_MENU: render_menu(); break;
    case UI_EDIT: render_edit(); break;
    case UI_INFO:
        header(s_items[s_model.selected]);
        label(root, "Không khả dụng", 12, 83, 216, 0xa1a9a5);
        label(root, "Trở lại", 12, 200, 216, 0xeeeeee);
        break;
    case UI_PREVIEW: render_subtitle(HISU_UI_LISTENING + s_model.preview, s_demo[s_model.preview]); break;
    case UI_IDLE:
        if (s_status.status != HISU_UI_IDLE) render_subtitle(s_status.status, s_status.text);
        break;
    }
}

static void handle_key(hisu_key_t key)
{
    if (!key) return;
    s_activity_ms = esp_timer_get_time() / 1000;
    if (s_asleep) { s_asleep = false; return; }
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
    if (previous != UI_EDIT && s_model.page == UI_EDIT) {
        s_model.value = hisu_settings_get((hisu_setting_t)s_model.selected);
    }
    if (s_model.page != previous || s_model.value != old_value || previous == UI_MENU || previous == UI_PREVIEW) render();
}

static void ui_task(void *arg)
{
    (void)arg;
    lv_init();
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
    ESP_LOGI("UI", "Ready: black idle, menu and subtitles");
    for (;;) {
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
        // Never turn the screen off during settings or active conversation.
        if (timeout && s_model.page == UI_IDLE && s_status.status == HISU_UI_IDLE && now - s_activity_ms >= timeout * 1000LL) {
            s_asleep = true;
        }
        int brightness = s_asleep ? 0 : s_model.page == UI_EDIT && s_model.selected == UI_BRIGHTNESS
            ? s_model.value : hisu_settings_get(HISU_SETTING_BRIGHTNESS);
        if (brightness != s_applied_brightness) {
            err = hisu_board_set_brightness(brightness);
            if (err == ESP_OK) s_applied_brightness = brightness;
            else ESP_LOGW("UI", "Backlight: %s", esp_err_to_name(err));
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
