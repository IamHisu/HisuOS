#pragma once

#include "esp_err.h"

typedef enum {
    HISU_UI_IDLE,
    HISU_UI_LISTENING,
    HISU_UI_THINKING,
    HISU_UI_SPEAKING,
    HISU_UI_ERROR,
} hisu_ui_status_t;

esp_err_t hisu_ui_start(void);
// Copies UTF-8 text (up to 255 bytes); returns an error on invalid input or full queue.
// Safe for worker tasks, not ISR callbacks. Caller never accesses LVGL.
esp_err_t hisu_ui_post_status(hisu_ui_status_t status, const char *subtitle);
