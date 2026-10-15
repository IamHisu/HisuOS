/* ADC-to-percent profile adapted from Muse's Zhengchen board (Apache-2.0).
 * Copyright (c) Meta Platforms, Inc. and affiliates. See licenses/NOTICE.md.
 */
#include "hisu_power.h"
#include "hisu_board.h"
#include "hisu_settings.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static hisu_power_status_t s_status;
static bool s_screen_known, s_screen_on;
static int64_t s_last_usage_us;
enum {USAGE_KNOWN = 1, USAGE_EXTERNAL = 2, USAGE_BASELINE = 4, USAGE_PREVIOUS = 8};
static hisu_power_usage_record_t s_usage;
static uint64_t s_settle_until_battery_us;

/* All callers hold s_lock; account the previous state before a transition. */
static void account_usage(void)
{
    int64_t now = esp_timer_get_time();
    uint64_t elapsed = now > s_last_usage_us ? (uint64_t)(now - s_last_usage_us) : 0;
    s_last_usage_us = now;
    s_usage.total_active_us += elapsed;
    if (s_status.valid && !s_status.external_power) {
        s_usage.battery_us += elapsed;
        if (s_screen_known) {
            if (s_screen_on) s_usage.screen_on_us += elapsed;
            else s_usage.screen_sleep_us += elapsed;
        }
    }
}

/* Preserve the completed discharge when charging starts, including after reboot. */
static void remember_battery_run(void)
{
    if (!s_usage.battery_us) return;
    s_usage.previous_battery_us = s_usage.battery_us;
    s_usage.previous_screen_on_us = s_usage.screen_on_us;
    s_usage.previous_screen_sleep_us = s_usage.screen_sleep_us;
    s_usage.flags |= USAGE_PREVIOUS;
}

static void update_power(hisu_power_status_t status)
{
    account_usage();
    if (status.valid) {
        bool known = s_usage.flags & USAGE_KNOWN;
        bool previous_external = s_usage.flags & USAGE_EXTERNAL;
        if (known && !previous_external && status.external_power) remember_battery_run();
        if (known && previous_external && !status.external_power) {
            s_usage.screen_on_us = s_usage.screen_sleep_us = s_usage.battery_us = 0;
            s_usage.baseline_battery_us = s_usage.baseline_percent = 0;
            s_usage.flags &= ~USAGE_BASELINE;
            s_settle_until_battery_us = 60000000;
        }
        s_usage.flags |= USAGE_KNOWN;
        if (status.external_power) {
            s_usage.flags |= USAGE_EXTERNAL;
            s_usage.flags &= ~USAGE_BASELINE;
        } else {
            s_usage.flags &= ~USAGE_EXTERNAL;
            if ((s_usage.flags & USAGE_BASELINE) && status.percent > (int)s_usage.baseline_percent + 3)
                s_usage.flags &= ~USAGE_BASELINE;
            if (!(s_usage.flags & USAGE_BASELINE) && s_usage.battery_us >= s_settle_until_battery_us) {
                s_usage.baseline_percent = status.percent;
                s_usage.baseline_battery_us = s_usage.battery_us;
                s_usage.flags |= USAGE_BASELINE;
            }
        }
        s_usage.last_percent = status.percent;
    }
    s_status = status;
}

static bool checkpoint_due(const hisu_power_status_t *next, int64_t now, int64_t last_saved)
{
    /* Keep the last checkpoint rather than write NVS on a nearly empty battery. */
    if (!next->valid || (!next->external_power && next->percent <= 5)) return false;
    bool transition = next->valid && (!(s_usage.flags & USAGE_KNOWN) ||
        ((s_usage.flags & USAGE_EXTERNAL) != 0) != next->external_power);
    return transition || now - last_saved >= 60000000;
}

void hisu_power_set_screen_on(bool on)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    account_usage();
    s_screen_known = true;
    s_screen_on = on;
    xSemaphoreGive(s_lock);
}

static void power_task(void *arg)
{
    (void)arg;
    int samples[3] = {0}, count = 0, next = 0;
    int64_t last_saved_us = esp_timer_get_time();
    int64_t retry_after_us = 0;
    bool save_pending = false;
    for (;;) {
        hisu_power_status_t status = {0};
        int raw;
        status.valid = hisu_board_power_read(&raw, &status.external_power) == ESP_OK;
        if (status.valid) {
            samples[next] = raw;
            next = (next + 1) % 3;
            if (count < 3) ++count;
            for (int i = 0; i < count; ++i) status.raw_adc += samples[i];
            status.raw_adc /= count;
            status.percent = hisu_power_percent_from_adc(status.raw_adc);
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        int64_t now = esp_timer_get_time();
        bool safe_to_save = status.valid && (status.external_power || status.percent > 5);
        bool save = safe_to_save && now >= retry_after_us &&
            (save_pending || checkpoint_due(&status, now, last_saved_us));
        update_power(status);
        hisu_power_usage_record_t record = s_usage;
        xSemaphoreGive(s_lock);
        if (save) {
            esp_err_t err = hisu_settings_save_power_usage(&record);
            save_pending = err != ESP_OK;
            if (err != ESP_OK) ESP_LOGW("POWER", "Usage save: %s", esp_err_to_name(err));
            if (err == ESP_OK) { last_saved_us = now; retry_after_us = 0; }
            else retry_after_us = now + 60000000;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

esp_err_t hisu_power_start(void)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;
    esp_err_t err = hisu_board_power_init();
    if (err != ESP_OK) return err;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    esp_err_t load_err = hisu_settings_load_power_usage(&s_usage);
    if (load_err != ESP_OK && load_err != ESP_ERR_NOT_FOUND)
        ESP_LOGW("POWER", "Usage load: %s", esp_err_to_name(load_err));
    s_usage.version = 2;
    /* Resume usage counters but measure a fresh drain slope after reboot. */
    s_usage.flags &= ~USAGE_BASELINE;
    s_settle_until_battery_us = s_usage.battery_us + 60000000;
    s_last_usage_us = esp_timer_get_time();
    if (xTaskCreate(power_task, "hisu_power", 3072, NULL, 2, NULL) != pdPASS) {
        vSemaphoreDelete(s_lock); s_lock = NULL; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void hisu_power_get(hisu_power_status_t *out)
{
    if (!out) return;
    *out = (hisu_power_status_t){0};
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    account_usage();
    *out = s_status;
    out->screen_on_seconds = s_usage.screen_on_us / 1000000;
    out->screen_sleep_seconds = s_usage.screen_sleep_us / 1000000;
    out->battery_seconds = s_usage.battery_us / 1000000;
    out->total_active_seconds = s_usage.total_active_us / 1000000;
    out->previous_valid = (s_usage.flags & USAGE_PREVIOUS) != 0;
    out->previous_battery_seconds = s_usage.previous_battery_us / 1000000;
    if (s_status.valid && !s_status.external_power && (s_usage.flags & USAGE_BASELINE)) {
        int64_t remaining = hisu_power_remaining_seconds(s_usage.baseline_percent, s_status.percent,
            (s_usage.battery_us - s_usage.baseline_battery_us) / 1000000);
        out->remaining_valid = remaining >= 0;
        if (out->remaining_valid) out->remaining_seconds = (uint64_t)remaining;
    }
    xSemaphoreGive(s_lock);
}
