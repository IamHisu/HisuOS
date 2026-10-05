/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Adapted from Muse's Zhengchen board driver.
#include "board_power.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"

static adc_oneshot_unit_handle_t s_adc;

esp_err_t board_power_init(void)
{
    if (s_adc != NULL) return ESP_ERR_INVALID_STATE;
    const gpio_config_t gpio = {
        .pin_bit_mask = 1ULL << CHARGE_GPIO,
        .mode = GPIO_MODE_INPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&gpio), "power", "charge pin");
    const adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &s_adc), "power", "ADC unit");
    const adc_oneshot_chan_cfg_t channel = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t err = adc_oneshot_config_channel(s_adc, BAT_ADC_CHANNEL, &channel);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
    }
    return err;
}

esp_err_t board_power_read(board_power_status_t *status)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    if (s_adc == NULL) return ESP_ERR_INVALID_STATE;
    static int samples[3];
    static int count, next;
    static const int adc_points[] = {2030, 2134, 2252, 2370, 2488, 2606};
    int sum = 0;
    for (int i = 0; i < 8; ++i) {
        int raw;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, BAT_ADC_CHANNEL, &raw), "power", "ADC read");
        sum += raw;
    }
    samples[next] = sum / 8 + 80;
    next = (next + 1) % 3;
    if (count < 3) ++count;
    int average = 0;
    for (int i = 0; i < count; ++i) average += samples[i];
    average /= count;
    int percent = 100;
    if (average < adc_points[0]) percent = 0;
    else for (int i = 0; i < 5; ++i) {
        if (average < adc_points[i + 1]) {
            percent = i * 20 + (average - adc_points[i]) * 20 /
                      (adc_points[i + 1] - adc_points[i]);
            break;
        }
    }
    *status = (board_power_status_t) {
        .battery_percent = percent,
        .external_power = gpio_get_level(CHARGE_GPIO) == 1,
    };
    status->charging = status->external_power && percent < 100;
    return ESP_OK;
}
