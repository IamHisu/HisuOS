#include "hisu_board.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_timer.h"

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static hisu_display_done_fn s_done;
static void *s_context;
static bool s_volume_hold;

typedef struct {
    int pin;
    hisu_key_t key;
    bool raw;
    bool pressed;
    bool held;
    int64_t changed_ms;
    int64_t pressed_ms;
} button_t;

static button_t s_buttons[] = {
    {.pin = BTN_BOOT, .key = HISU_KEY_BOOT},
    {.pin = BTN_VOL_UP, .key = HISU_KEY_UP},
    {.pin = BTN_VOL_DOWN, .key = HISU_KEY_DOWN},
};

static bool transfer_done(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *event, void *context)
{
    (void)io;
    (void)event;
    (void)context;
    s_done(s_context);
    return false;
}

esp_err_t hisu_board_init(hisu_display_done_fn done, void *context)
{
    if (!done) return ESP_ERR_INVALID_ARG;
    if (s_panel) return ESP_ERR_INVALID_STATE;
    s_done = done;
    s_context = context;
    const gpio_config_t buttons = {
        .pin_bit_mask = (1ULL << BTN_BOOT) | (1ULL << BTN_VOL_UP) | (1ULL << BTN_VOL_DOWN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&buttons);
    if (err != ESP_OK) return err;
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;
    const ledc_channel_config_t channel = {
        .gpio_num = LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) return err;
    const spi_bus_config_t bus = {
        .mosi_io_num = LCD_MOSI, .sclk_io_num = LCD_SCLK, .miso_io_num = -1,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * 40 * 2,
    };
    err = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS, .dc_gpio_num = LCD_DC,
        .spi_mode = 3, .pclk_hz = 80 * 1000 * 1000,
        .trans_queue_depth = 4, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io);
    if (err != ESP_OK) goto fail;
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel);
    if (err != ESP_OK) goto fail;
    if ((err = esp_lcd_panel_reset(s_panel)) != ESP_OK) goto fail;
    if ((err = esp_lcd_panel_init(s_panel)) != ESP_OK) goto fail;
    if ((err = esp_lcd_panel_invert_color(s_panel, true)) != ESP_OK) goto fail;
    if ((err = esp_lcd_panel_disp_on_off(s_panel, true)) != ESP_OK) goto fail;
    const esp_lcd_panel_io_callbacks_t callbacks = {.on_color_trans_done = transfer_done};
    err = esp_lcd_panel_io_register_event_callbacks(s_io, &callbacks, NULL);
    if (err == ESP_OK) return ESP_OK;
fail:
    if (s_panel) esp_lcd_panel_del(s_panel);
    if (s_io) esp_lcd_panel_io_del(s_io);
    s_panel = NULL;
    s_io = NULL;
    spi_bus_free(LCD_HOST);
    return err;
}

esp_err_t hisu_board_display_write(int x1, int y1, int x2, int y2, const void *pixels)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    if (!pixels || x1 < 0 || y1 < 0 || x2 > LCD_WIDTH || y2 > LCD_HEIGHT || x2 <= x1 || y2 <= y1) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, pixels);
}

esp_err_t hisu_board_set_brightness(int percent)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100);
    if (err == ESP_OK) err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    return err;
}

void hisu_board_enable_volume_hold(bool enabled) { s_volume_hold = enabled; }

hisu_key_t hisu_board_poll_key(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    hisu_key_t result = HISU_KEY_NONE;
    for (unsigned i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        button_t *b = &s_buttons[i];
        bool pressed = gpio_get_level(b->pin) == BTN_ACTIVE_LEVEL;
        if (pressed != b->raw) {
            b->raw = pressed;
            b->changed_ms = now;
        }
        if (now - b->changed_ms >= 30 && b->pressed != b->raw) {
            b->pressed = b->raw;
            if (b->pressed) {
                b->pressed_ms = now;
                b->held = false;
            } else if (!b->held && result == HISU_KEY_NONE) {
                result = b->key;
            }
        }
        int hold_ms = b->key == HISU_KEY_BOOT ? 800 : 1000;
        if ((b->key == HISU_KEY_BOOT || s_volume_hold) && b->pressed && b->raw && !b->held &&
            now - b->pressed_ms >= hold_ms && result == HISU_KEY_NONE) {
            b->held = true;
            result = b->key == HISU_KEY_BOOT ? HISU_KEY_BACK :
                     b->key == HISU_KEY_UP ? HISU_KEY_UP_HELD : HISU_KEY_DOWN_HELD;
        }
    }
    return result;
}
