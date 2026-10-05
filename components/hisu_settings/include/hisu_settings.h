#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef enum {
    HISU_SETTING_BRIGHTNESS,
    HISU_SETTING_VOLUME,
    HISU_SETTING_SPEAKER,
    HISU_SETTING_TIMEOUT,
    HISU_SETTING_THEME, /* 0: dark, 1: light */
    HISU_SETTING_WIFI_ENABLED,
    HISU_SETTING_COUNT,
} hisu_setting_t;

esp_err_t hisu_settings_init(void);
int hisu_settings_get(hisu_setting_t key);
esp_err_t hisu_settings_set(hisu_setting_t key, int value);
esp_err_t hisu_settings_get_wifi(char ssid[33], char password[65]);
esp_err_t hisu_settings_save_wifi(const char *ssid, const char *password);
#define HISU_WIFI_SAVED_MAX 8
esp_err_t hisu_settings_find_wifi(const char *ssid, char password[65]);
typedef struct {
    uint32_t version, flags, baseline_percent, last_percent;
    uint64_t screen_on_us, screen_sleep_us, battery_us, baseline_battery_us;
} hisu_power_usage_record_t;
esp_err_t hisu_settings_load_power_usage(hisu_power_usage_record_t *out);
esp_err_t hisu_settings_save_power_usage(const hisu_power_usage_record_t *record);
