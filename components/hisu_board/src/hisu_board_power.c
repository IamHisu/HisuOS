#include "hisu_board.h"
#include "board_config.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

static adc_oneshot_unit_handle_t s_adc;

esp_err_t hisu_board_power_init(void)
{
    if (s_adc) return ESP_ERR_INVALID_STATE;
    gpio_config_t gpio = {.pin_bit_mask = 1ULL << CHARGE_STATUS_GPIO, .mode = GPIO_MODE_INPUT};
    esp_err_t err = gpio_config(&gpio);
    if (err != ESP_OK) return err;
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    err = adc_oneshot_new_unit(&unit, &s_adc);
    if (err != ESP_OK) return err;
    adc_oneshot_chan_cfg_t channel = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
    err = adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &channel);
    if (err != ESP_OK) { adc_oneshot_del_unit(s_adc); s_adc = NULL; }
    return err;
}

esp_err_t hisu_board_power_read(int *raw_adc, bool *external_power)
{
    if (!s_adc) return ESP_ERR_INVALID_STATE;
    if (!raw_adc || !external_power) return ESP_ERR_INVALID_ARG;
    int sum = 0;
    for (int i = 0; i < 8; ++i) {
        int raw;
        esp_err_t err = adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &raw);
        if (err != ESP_OK) return err;
        sum += raw;
    }
    *raw_adc = sum / 8;
    *external_power = gpio_get_level(CHARGE_STATUS_GPIO) == 1;
    return ESP_OK;
}
