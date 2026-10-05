#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "lvgl.h"

#include "board_display.h"
#include "board_buttons.h"
#include "board_audio.h"
#include "friend_ui.h"
#include "friend_media.h"
#include "friend_media_store.h"
#include "friend_ai.h"
#include "friend_wifi.h"
#include "friend_wifi_store.h"
#include "friend_http.h"
#include "board_power.h"
#include "hisu_app.h"
#include "friend_subtitle.h"

static const char *TAG = "HisuOS";

#define FRIEND_UI_TASK_STACK_SIZE 8192
#define FRIEND_UI_TASK_PRIORITY   4
#define FRIEND_UI_TASK_DELAY_MS   10
#define FRIEND_UI_STACK_LOG_MS    10000
#define FRIEND_GIF_PERF_LOG_MS    5000

static bool ai_output_available(void)
{
    return strcmp(friend_media_current(), "face") == 0;
}

static void friend_ui_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "UI task started, stack=%d bytes", FRIEND_UI_TASK_STACK_SIZE);

    TickType_t last_stack_log = xTaskGetTickCount();
    TickType_t last_perf_log = last_stack_log;
    uint32_t handler_count = 0;
    uint32_t handler_total_us = 0;
    uint32_t handler_max_us = 0;

    while (1)
    {
        friend_media_process();
        friend_ai_process_ui();
        int64_t handler_started_us = esp_timer_get_time();
        lv_timer_handler();
        uint32_t handler_elapsed_us = (uint32_t)(esp_timer_get_time() - handler_started_us);
        handler_count++;
        handler_total_us += handler_elapsed_us;
        if (handler_elapsed_us > handler_max_us) handler_max_us = handler_elapsed_us;

        TickType_t now = xTaskGetTickCount();

        if ((now - last_perf_log) >= pdMS_TO_TICKS(FRIEND_GIF_PERF_LOG_MS))
        {
            board_display_perf_t perf;
            board_display_perf_take(&perf);

            if (strcmp(friend_media_current(), "gif") == 0)
            {
                uint32_t interval_ms = (uint32_t)pdTICKS_TO_MS(now - last_perf_log);
                uint32_t fps_tenths = interval_ms > 0 ? perf.render_count * 10000 / interval_ms : 0;
                ESP_LOGI(TAG, "GIF perf/%ums: render=%u (%u.%u fps), gap avg/max=%u/%u ms, render avg/max=%u/%u us, flush wait=%u us max=%u us, flush=%u/%u px, LVGL avg/max=%u/%u us",
                    interval_ms, perf.render_count, fps_tenths / 10, fps_tenths % 10,
                    perf.render_gap_count ? perf.render_gap_total_us / perf.render_gap_count / 1000 : 0, perf.render_gap_max_us / 1000,
                    perf.render_count ? perf.render_total_us / perf.render_count : 0, perf.render_max_us,
                    perf.flush_wait_total_us, perf.flush_wait_max_us,
                    perf.flush_count, perf.flush_pixels,
                    handler_count ? handler_total_us / handler_count : 0, handler_max_us);
            }

            last_perf_log = now;
            handler_count = 0;
            handler_total_us = 0;
            handler_max_us = 0;
        }

        if ((now - last_stack_log) >= pdMS_TO_TICKS(FRIEND_UI_STACK_LOG_MS))
        {
            UBaseType_t stack_free = uxTaskGetStackHighWaterMark(NULL);

            ESP_LOGI(TAG, "UI task minimum free stack: %u bytes", (unsigned int)stack_free);

            last_stack_log = now;
        }

        vTaskDelay(pdMS_TO_TICKS(FRIEND_UI_TASK_DELAY_MS));
    }
}

void hisu_app_start(void)
{
    ESP_LOGI(TAG, "Starting HisuOS");

    ESP_ERROR_CHECK(board_display_init());
    ESP_ERROR_CHECK(friend_ui_init());
    esp_err_t media_ret = friend_media_init();
    if (media_ret != ESP_OK) ESP_LOGW(TAG, "Media unavailable: %s", esp_err_to_name(media_ret));
    ESP_ERROR_CHECK(board_buttons_init());
    esp_err_t audio_ret = board_audio_init();
    if (audio_ret != ESP_OK) ESP_LOGW(TAG, "Audio unavailable: %s", esp_err_to_name(audio_ret));
    esp_err_t power_ret = board_power_init();
    if (power_ret != ESP_OK) ESP_LOGW(TAG, "Power monitor unavailable: %s", esp_err_to_name(power_ret));

    ESP_ERROR_CHECK(friend_wifi_init());
    ESP_ERROR_CHECK(friend_wifi_store_init());
    const friend_ai_output_t ai_output = {
        .available = ai_output_available,
        .show = friend_subtitle_show,
        .active = friend_subtitle_active,
        .clear = friend_subtitle_stop,
    };
    ESP_ERROR_CHECK(friend_ai_set_output(&ai_output));
    ESP_ERROR_CHECK(friend_ai_init());
    friend_media_kind_t saved_kind = FRIEND_MEDIA_FACE;
    uint8_t *saved_data = NULL;
    size_t saved_size = 0;
    esp_err_t restore_ret = friend_media_store_load(&saved_kind, &saved_data, &saved_size);
    bool show_image = restore_ret == ESP_OK && saved_kind != FRIEND_MEDIA_FACE;
    esp_err_t mode_ret = friend_media_store_get_image_visible(&show_image);
    if (mode_ret != ESP_OK && mode_ret != ESP_ERR_NVS_NOT_FOUND)
        ESP_LOGW(TAG, "Could not read display mode: %s", esp_err_to_name(mode_ret));
    if (show_image && (restore_ret != ESP_OK || saved_kind == FRIEND_MEDIA_FACE))
    {
        free(saved_data);
        restore_ret = friend_media_store_load_image(&saved_kind, &saved_data, &saved_size);
    }
    if (show_image && restore_ret == ESP_OK && saved_kind != FRIEND_MEDIA_FACE)
    {
        if (friend_media_submit(saved_kind, saved_data, saved_size) == ESP_OK)
            friend_media_process();
        else
            free(saved_data);
    }
    else free(saved_data);
    if (restore_ret != ESP_OK && restore_ret != ESP_ERR_NOT_FOUND)
        ESP_LOGW(TAG, "Could not restore media: %s", esp_err_to_name(restore_ret));
    friend_wifi_set_service_start_callback(friend_http_start);
    friend_media_set_mode_changed_callback(friend_ai_cancel);

    board_buttons_set_boot_long_press_callback(friend_wifi_request_manual_mode);
    board_buttons_set_boot_click_callback(friend_media_toggle);

    friend_ui_set_idle_actions(true);

    UBaseType_t main_stack_free = uxTaskGetStackHighWaterMark(NULL);

    ESP_LOGI(TAG, "Main task minimum free stack before UI task: %u bytes", (unsigned int)main_stack_free);

    BaseType_t task_result = xTaskCreate(friend_ui_task, "friend_ui", FRIEND_UI_TASK_STACK_SIZE, NULL, FRIEND_UI_TASK_PRIORITY, NULL);

    if (task_result != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create UI task");
        return;
    }

    ESP_ERROR_CHECK(friend_wifi_manager_start());

    ESP_LOGI(TAG, "Hisu is awake!");
    ESP_LOGI(TAG, "HisuOS initialization complete");
}
