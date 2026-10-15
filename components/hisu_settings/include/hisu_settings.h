#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HISU_SETTING_BRIGHTNESS,
    HISU_SETTING_VOLUME,
    HISU_SETTING_SPEAKER,
    HISU_SETTING_TIMEOUT,
    HISU_SETTING_THEME, /* 0: dark, 1: light */
    HISU_SETTING_WIFI_ENABLED,
    HISU_SETTING_TIMEZONE, /* Minutes east of UTC; default UTC+7. */
    HISU_SETTING_WALLPAPER_DEFAULT, /* 0: uploaded image if available, 1: built-in */
    HISU_SETTING_COUNT,
} hisu_setting_t;

esp_err_t hisu_settings_init(void);
int hisu_settings_get(hisu_setting_t key);
esp_err_t hisu_settings_set(hisu_setting_t key, int value);
/* API key remains in NVS and is never returned by the setup web API. */
esp_err_t hisu_settings_get_asr_key(char out[128]);
esp_err_t hisu_settings_set_asr_key(const char *key);
bool hisu_settings_has_asr_key(void);
esp_err_t hisu_settings_get_gemini_key(char out[128]);
esp_err_t hisu_settings_set_gemini_key(const char *key);
bool hisu_settings_has_gemini_key(void);
esp_err_t hisu_settings_get_wifi(char ssid[33], char password[65]);
esp_err_t hisu_settings_get_wifi_at(unsigned index, char ssid[33], char password[65]);
esp_err_t hisu_settings_save_wifi(const char *ssid, const char *password);
esp_err_t hisu_settings_save_wifi_ex(const char *ssid, const char *password, bool hidden);
bool hisu_settings_wifi_hidden(const char *ssid);
#define HISU_WIFI_SAVED_MAX 8
esp_err_t hisu_settings_find_wifi(const char *ssid, char password[65]);
typedef struct {
    uint32_t version, flags, baseline_percent, last_percent;
    uint64_t screen_on_us, screen_sleep_us, battery_us, baseline_battery_us;
    uint64_t total_active_us, previous_battery_us, previous_screen_on_us, previous_screen_sleep_us;
} hisu_power_usage_record_t;
esp_err_t hisu_settings_load_power_usage(hisu_power_usage_record_t *out);
esp_err_t hisu_settings_save_power_usage(const hisu_power_usage_record_t *record);
