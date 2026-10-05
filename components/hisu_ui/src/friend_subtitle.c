#include "friend_subtitle.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

LV_FONT_DECLARE(friend_font_vietnamese_16);

#define PAGE_BYTES 55
#define REVEAL_MS 55

static lv_obj_t *panel;
static lv_obj_t *label;
static lv_timer_t *timer;
static char *message;
static size_t offset;
static size_t page_end;
static size_t visible;
static unsigned int hold_ticks;
static const char *TAG = "SUBTITLE";

bool friend_subtitle_active(void)
{
    return timer != NULL;
}

static size_t utf8_next(const char *value, size_t index)
{
    if (value[index] == '\0') return index;
    index++;
    while (value[index] != '\0' && ((unsigned char)value[index] & 0xc0) == 0x80) index++;
    return index;
}

static void next_page(void)
{
    size_t end = offset;
    size_t last_space = offset;
    while (message[end] != '\0' && end - offset < PAGE_BYTES)
    {
        size_t next = utf8_next(message, end);
        if (next - offset > PAGE_BYTES) break;
        if (message[end] == ' ' || message[end] == '\n') last_space = end;
        end = next;
    }
    page_end = message[end] != '\0' && last_space > offset + PAGE_BYTES / 2 ? last_space : end;
    visible = offset;
    hold_ticks = 0;
}

void friend_subtitle_stop(void)
{
    if (timer != NULL) lv_timer_delete(timer);
    timer = NULL;
    if (panel != NULL) lv_obj_delete(panel);
    panel = NULL;
    label = NULL;
    free(message);
    message = NULL;
    offset = page_end = visible = 0;
}

static void subtitle_tick(lv_timer_t *unused)
{
    (void)unused;
    if (visible < page_end)
    {
        visible = utf8_next(message, visible);
        char saved = message[visible];
        message[visible] = '\0';
        lv_label_set_text(label, message + offset);
        message[visible] = saved;
        return;
    }
    if (++hold_ticks < (message[page_end] == '\0' ? 35 : 13)) return;
    if (message[page_end] == '\0')
    {
        friend_subtitle_stop();
        return;
    }
    offset = page_end;
    while (message[offset] == ' ' || message[offset] == '\n') offset++;
    next_page();
    lv_label_set_text(label, "");
}

bool friend_subtitle_show(const char *text)
{
    friend_subtitle_stop();
    if (text == NULL || text[0] == '\0') return false;
    message = strdup(text);
    if (message == NULL) return false;

    panel = lv_obj_create(lv_layer_top());
    if (panel == NULL) { friend_subtitle_stop(); return false; }
    lv_obj_set_size(panel, 240, 76);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x080d0d), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    label = lv_label_create(panel);
    if (label == NULL) { friend_subtitle_stop(); return false; }
    lv_obj_set_width(label, 220);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &friend_font_vietnamese_16, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
    offset = 0;
    next_page();
    timer = lv_timer_create(subtitle_tick, REVEAL_MS, NULL);
    if (timer == NULL) { friend_subtitle_stop(); return false; }
    lv_obj_invalidate(panel);
    ESP_LOGI(TAG, "Subtitle started: %u bytes, first page %u bytes",
        (unsigned int)strlen(message), (unsigned int)(page_end - offset));
    return true;
}
