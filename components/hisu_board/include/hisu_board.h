#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define HISU_DISPLAY_WIDTH 240
#define HISU_DISPLAY_HEIGHT 240

typedef void (*hisu_display_done_fn)(void *context);

typedef enum {
    HISU_KEY_NONE,
    HISU_KEY_BOOT,
    HISU_KEY_UP,
    HISU_KEY_DOWN,
    HISU_KEY_BACK,
    HISU_KEY_UP_HELD,
    HISU_KEY_DOWN_HELD,
} hisu_key_t;

// Completion runs in the LCD ISR; the callback must be ISR-safe.
esp_err_t hisu_board_init(hisu_display_done_fn done, void *context);
esp_err_t hisu_board_display_write(int x1, int y1, int x2, int y2, const void *pixels);
esp_err_t hisu_board_set_brightness(int percent);
// Poll at least every 10ms, from one owner task only.
hisu_key_t hisu_board_poll_key(void);
void hisu_board_enable_volume_hold(bool enabled);
esp_err_t hisu_board_power_init(void);
esp_err_t hisu_board_power_read(int *raw_adc, bool *external_power);
