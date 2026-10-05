#include "board_display.h"
#include "board_config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lvgl.h"


// ============================================================
// Configuration
// ============================================================

#define LVGL_BUFFER_LINES 60
#define LVGL_BUFFER_FALLBACK_LINES 20


// ============================================================
// Module state
// ============================================================

static const char *TAG = "board_display";

static esp_lcd_panel_handle_t s_panel = NULL;
static lv_display_t *s_lvgl_display = NULL;

static uint8_t *s_lvgl_buf1 = NULL;
static board_display_perf_t s_perf;
static int64_t s_render_started_us;
static int64_t s_previous_render_started_us;
static int64_t s_flush_wait_started_us;

static void display_perf_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    int64_t now = esp_timer_get_time();

    if (code == LV_EVENT_RENDER_START)
    {
        if (s_previous_render_started_us != 0)
        {
            uint32_t gap = (uint32_t)(now - s_previous_render_started_us);
            s_perf.render_gap_count++;
            s_perf.render_gap_total_us += gap;
            if (gap > s_perf.render_gap_max_us) s_perf.render_gap_max_us = gap;
        }

        s_previous_render_started_us = now;
        s_render_started_us = now;
    }
    else if (code == LV_EVENT_RENDER_READY && s_render_started_us != 0)
    {
        uint32_t elapsed = (uint32_t)(now - s_render_started_us);
        s_perf.render_count++;
        s_perf.render_total_us += elapsed;
        if (elapsed > s_perf.render_max_us) s_perf.render_max_us = elapsed;
        s_render_started_us = 0;
    }
    else if (code == LV_EVENT_FLUSH_WAIT_START)
    {
        s_flush_wait_started_us = now;
    }
    else if (code == LV_EVENT_FLUSH_WAIT_FINISH && s_flush_wait_started_us != 0)
    {
        uint32_t elapsed = (uint32_t)(now - s_flush_wait_started_us);
        s_perf.flush_wait_total_us += elapsed;
        if (elapsed > s_perf.flush_wait_max_us) s_perf.flush_wait_max_us = elapsed;
        s_flush_wait_started_us = 0;
    }
    else if (code == LV_EVENT_FLUSH_START)
    {
        const lv_area_t *area = lv_event_get_param(event);
        s_perf.flush_count++;
        s_perf.flush_pixels += lv_area_get_width(area) * lv_area_get_height(area);
    }
}

void board_display_perf_take(board_display_perf_t *stats)
{
    if (stats == NULL) return;
    *stats = s_perf;
    memset(&s_perf, 0, sizeof(s_perf));
    s_previous_render_started_us = 0;
}


// ============================================================
// ESP LCD -> LVGL
// ============================================================

/**
 * Called by ESP LCD when the queued color transfer has
 * actually finished.
 *
 * Only now is LVGL allowed to reuse the render buffer.
 */
static bool lcd_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *display = (lv_display_t *)user_ctx;

    if (display != NULL)
    {
        lv_display_flush_ready(display);
    }

    return false;
}


// ============================================================
// LVGL -> ESP LCD
// ============================================================

static uint32_t lvgl_tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(display);

    uint32_t pixel_count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);

    /*
     * Verified on this Zhengchen ST7789:
     *
     * LVGL RGB565 byte order needs to be swapped before
     * sending to the LCD.
     */
    lv_draw_sw_rgb565_swap(px_map, pixel_count);

    /*
     * esp_lcd_panel_draw_bitmap() queues the color transfer.
     *
     * DO NOT call lv_display_flush_ready() here.
     *
     * lcd_color_trans_done_cb() will notify LVGL when
     * DMA transmission is actually complete.
     */
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map));
}


// ============================================================
// Public API
// ============================================================

esp_err_t board_display_init(void)
{
    ESP_LOGI(TAG, "Initializing Zhengchen display");

    // --------------------------------------------------------
    // Backlight
    // --------------------------------------------------------

    gpio_config_t bl_config = {
        .pin_bit_mask = 1ULL << LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
    };

    ESP_ERROR_CHECK(gpio_config(&bl_config));

    // Keep backlight off during initialization.
    gpio_set_level(LCD_BL, 0);

    // --------------------------------------------------------
    // SPI bus
    // --------------------------------------------------------

    spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_SCLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
    };

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    // --------------------------------------------------------
    // LCD SPI IO
    // --------------------------------------------------------

    esp_lcd_panel_io_handle_t io_handle = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_DC,
        .cs_gpio_num = LCD_CS,
        .pclk_hz = 80 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 3,
        .trans_queue_depth = 10,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    // --------------------------------------------------------
    // ST7789
    // --------------------------------------------------------

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    // // Rotate display 180 degrees.
    // // Comment this line to restore the original orientation.
    // ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, true));

    /*
     * Required by this specific Zhengchen panel.
     *
     * Verified during hardware bring-up.
     */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    // --------------------------------------------------------
    // Backlight ON
    // --------------------------------------------------------

    gpio_set_level(LCD_BL, 1);

    ESP_LOGI(TAG, "ST7789 initialized");

    // --------------------------------------------------------
    // LVGL
    // --------------------------------------------------------

    lv_init();

    /*
     * Use ESP Timer as LVGL's millisecond time source.
     */
    lv_tick_set_cb(lvgl_tick_get_cb);

    s_lvgl_display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

    if (s_lvgl_display == NULL)
    {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return ESP_FAIL;
    }

    // --------------------------------------------------------
    // Link LVGL <-> ESP LCD
    // --------------------------------------------------------

    lv_display_set_user_data(s_lvgl_display, s_panel);
    lv_display_set_flush_cb(s_lvgl_display, lvgl_flush_cb);
    lv_display_add_event_cb(s_lvgl_display, display_perf_event_cb, LV_EVENT_ALL, NULL);

    /*
     * Register ESP LCD DMA completion callback.
     *
     * s_lvgl_display is passed back to us as user_ctx.
     */
    esp_lcd_panel_io_callbacks_t io_callbacks = {
        .on_color_trans_done = lcd_color_trans_done_cb,
    };

    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &io_callbacks, s_lvgl_display));

    // --------------------------------------------------------
    // LVGL render buffers
    // --------------------------------------------------------

    size_t buffer_lines = LVGL_BUFFER_LINES;
    size_t buffer_size = LCD_WIDTH * buffer_lines * sizeof(uint16_t);
    s_lvgl_buf1 = heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

    if (s_lvgl_buf1 == NULL)
    {
        buffer_lines = LVGL_BUFFER_FALLBACK_LINES;
        buffer_size = LCD_WIDTH * buffer_lines * sizeof(uint16_t);
        s_lvgl_buf1 = heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (s_lvgl_buf1 == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate internal DMA LVGL buffer");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGW(TAG, "Using smaller LVGL DMA buffer (%u lines)", (unsigned int)buffer_lines);
    }

    lv_display_set_buffers(s_lvgl_display, s_lvgl_buf1, NULL, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "LVGL display initialized (%dx%d, SPI mode 3 at 80 MHz, internal DMA buffer=%u lines)",
        LCD_WIDTH, LCD_HEIGHT, (unsigned int)buffer_lines);

    return ESP_OK;
}
