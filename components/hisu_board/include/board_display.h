#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the board display.
 *
 * Initializes:
 * - Backlight GPIO
 * - SPI bus
 * - ST7789 LCD
 * - LVGL
 * - LVGL display buffers
 *
 * @return ESP_OK on success.
 */
esp_err_t board_display_init(void);

typedef struct
{
    uint32_t render_count;
    uint32_t render_total_us;
    uint32_t render_max_us;
    uint32_t render_gap_count;
    uint32_t render_gap_total_us;
    uint32_t render_gap_max_us;
    uint32_t flush_wait_total_us;
    uint32_t flush_wait_max_us;
    uint32_t flush_count;
    uint32_t flush_pixels;
} board_display_perf_t;

void board_display_perf_take(board_display_perf_t *stats);

#ifdef __cplusplus
}
#endif
