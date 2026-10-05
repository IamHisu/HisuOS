#include "friend_media.h"
#include "friend_gif.h"
#include "friend_media_store.h"

#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

typedef struct
{
    friend_media_kind_t kind;
    uint8_t *data;
    size_t size;
} media_command_t;

static const char *TAG = "MEDIA";
static QueueHandle_t command_queue;
static void *lv_pool_memory;
static bool gif_ready;
static lv_obj_t *media_object;
static lv_obj_t *media_backdrop;
static uint8_t *media_data;
static lv_image_dsc_t *media_descriptor;
static volatile friend_media_kind_t current_kind = FRIEND_MEDIA_FACE;
static lv_obj_t *media_notice;
static void (*s_mode_changed_callback)(void);

void friend_media_set_mode_changed_callback(void (*callback)(void))
{
    s_mode_changed_callback = callback;
}

#define FRIEND_MEDIA_TOGGLE ((friend_media_kind_t)3)

static void notice_deleted(lv_event_t *event)
{
    (void)event;
    media_notice = NULL;
}

static void show_notice(const char *message)
{
    if (media_notice != NULL) lv_obj_delete(media_notice);
    media_notice = lv_obj_create(lv_layer_top());
    if (media_notice == NULL) return;
    lv_obj_set_size(media_notice, 220, 42);
    lv_obj_align(media_notice, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(media_notice, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(media_notice, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(media_notice, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(media_notice, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(media_notice, 4, LV_PART_MAIN);
    lv_obj_remove_flag(media_notice, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *label = lv_label_create(media_notice);
    lv_label_set_text(label, message);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
    lv_obj_add_event_cb(media_notice, notice_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_delete_delayed(media_notice, 2200);
}

esp_err_t friend_media_init(void)
{
    command_queue = xQueueCreate(1, sizeof(media_command_t));
    if (command_queue == NULL) return ESP_ERR_NO_MEM;

    lv_pool_memory = heap_caps_malloc(512 * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (lv_pool_memory == NULL)
    {
        ESP_LOGW(TAG, "GIF disabled: no 512 KB PSRAM block (largest=%u)",
            (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        return ESP_OK;
    }

    if (lv_mem_add_pool(lv_pool_memory, 512 * 1024) == NULL)
    {
        ESP_LOGW(TAG, "GIF disabled: LVGL rejected PSRAM pool");
        free(lv_pool_memory);
        lv_pool_memory = NULL;
        return ESP_OK;
    }

    gif_ready = true;
    ESP_LOGI(TAG, "GIF PSRAM pool ready (512 KB)");
    return ESP_OK;
}

esp_err_t friend_media_submit(friend_media_kind_t kind, uint8_t *data, size_t size)
{
    if (command_queue == NULL) return ESP_ERR_INVALID_STATE;
    if (kind == FRIEND_MEDIA_GIF && !gif_ready) return ESP_ERR_NOT_SUPPORTED;

    media_command_t command = { .kind = kind, .data = data, .size = size };
    return xQueueSend(command_queue, &command, 0) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool friend_media_gif_available(void)
{
    return gif_ready;
}

void friend_media_toggle(void)
{
    if (command_queue == NULL) return;
    media_command_t command = { .kind = FRIEND_MEDIA_TOGGLE };
    if (xQueueSend(command_queue, &command, 0) != pdTRUE)
        ESP_LOGW(TAG, "BOOT media toggle ignored: display busy");
}

const char *friend_media_current(void)
{
    switch (current_kind)
    {
        case FRIEND_MEDIA_STILL: return "image";
        case FRIEND_MEDIA_GIF: return "gif";
        default: return "face";
    }
}

void friend_media_process(void)
{
    media_command_t command;
    if (command_queue == NULL || xQueueReceive(command_queue, &command, 0) != pdTRUE) return;

    bool toggle = command.kind == FRIEND_MEDIA_TOGGLE;
    if (toggle && current_kind == FRIEND_MEDIA_FACE)
    {
        esp_err_t err = friend_media_store_load_image(&command.kind, &command.data, &command.size);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "No saved media for BOOT toggle: %s", esp_err_to_name(err));
            show_notice(err == ESP_ERR_NOT_FOUND ? "Chua co anh/GIF da luu" : "Khong mo duoc anh da luu");
            return;
        }
        if (command.kind == FRIEND_MEDIA_GIF && !gif_ready)
        {
            free(command.data);
            show_notice("Khong du bo nho cho GIF");
            return;
        }
    }
    else if (toggle)
    {
        command.kind = FRIEND_MEDIA_FACE;
    }

    if (command.kind == FRIEND_MEDIA_FACE)
    {
        if (media_backdrop != NULL) lv_obj_delete(media_backdrop);
        media_object = NULL;
        media_backdrop = NULL;
        free(media_data);
        media_data = NULL;
        free(media_descriptor);
        media_descriptor = NULL;
        current_kind = FRIEND_MEDIA_FACE;
        if (toggle)
        {
            esp_err_t err = friend_media_store_set_image_visible(false);
            if (err != ESP_OK) ESP_LOGW(TAG, "Could not save face mode: %s", esp_err_to_name(err));
        }
        return;
    }

    lv_image_dsc_t *next_descriptor = NULL;
    if (command.kind != FRIEND_MEDIA_GIF)
    {
        next_descriptor = malloc(sizeof(*next_descriptor));
        if (next_descriptor == NULL)
        {
            free(command.data);
            return;
        }

        *next_descriptor = (lv_image_dsc_t){
            .header.magic = LV_IMAGE_HEADER_MAGIC,
            .header.cf = LV_COLOR_FORMAT_RGB565,
            .header.w = FRIEND_MEDIA_WIDTH,
            .header.h = FRIEND_MEDIA_HEIGHT,
            .header.stride = FRIEND_MEDIA_WIDTH * 2,
            .data_size = command.size,
            .data = command.data,
        };
    }

    lv_obj_t *next_backdrop = lv_obj_create(lv_screen_active());
    if (next_backdrop == NULL)
    {
        free(next_descriptor);
        free(command.data);
        return;
    }

    lv_obj_set_size(next_backdrop, FRIEND_MEDIA_WIDTH, FRIEND_MEDIA_HEIGHT);
    lv_obj_set_pos(next_backdrop, 0, 0);
    lv_obj_set_style_bg_color(next_backdrop, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(next_backdrop, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(next_backdrop, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(next_backdrop, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(next_backdrop, 0, LV_PART_MAIN);
    lv_obj_remove_flag(next_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(next_backdrop, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *next_object = command.kind == FRIEND_MEDIA_GIF
        ? friend_gif_create(next_backdrop, command.data, command.size)
        : lv_image_create(next_backdrop);
    if (next_object == NULL)
    {
        if (command.kind == FRIEND_MEDIA_GIF) ESP_LOGW(TAG, "GIF decoder rejected upload");
        lv_obj_delete(next_backdrop);
        free(next_descriptor);
        free(command.data);
        return;
    }

    lv_obj_add_flag(next_object, LV_OBJ_FLAG_HIDDEN);
    if (command.kind != FRIEND_MEDIA_GIF)
    {
        lv_image_set_src(next_object, next_descriptor);
    }

    if (media_backdrop != NULL) lv_obj_delete(media_backdrop);
    free(media_data);
    free(media_descriptor);
    media_data = command.data;
    media_descriptor = next_descriptor;

    media_object = next_object;
    media_backdrop = next_backdrop;
    lv_obj_center(media_object);
    lv_obj_remove_flag(media_object, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(media_backdrop, LV_OBJ_FLAG_HIDDEN);
    current_kind = command.kind;
    if (s_mode_changed_callback != NULL) s_mode_changed_callback();
    if (toggle)
    {
        esp_err_t err = friend_media_store_set_image_visible(true);
        if (err != ESP_OK) ESP_LOGW(TAG, "Could not save image mode: %s", esp_err_to_name(err));
    }
}
