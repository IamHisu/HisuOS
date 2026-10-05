#pragma once

#include "esp_err.h"

typedef enum {
    HISU_SETTING_BRIGHTNESS,
    HISU_SETTING_VOLUME,
    HISU_SETTING_SPEAKER,
    HISU_SETTING_TIMEOUT,
    HISU_SETTING_COUNT,
} hisu_setting_t;

esp_err_t hisu_settings_init(void);
int hisu_settings_get(hisu_setting_t key);
esp_err_t hisu_settings_set(hisu_setting_t key, int value);
