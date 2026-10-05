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
// PCM conversion adapted from Muse; original microphone driver from FriendOS.
#include "board_audio.h"
#include "board_config.h"

#include <stdint.h>
#include <stdlib.h>
#include <limits.h>

#include "driver/i2s_std.h"

#include "esp_log.h"
#include "esp_check.h"


static const char *TAG = "AUDIO";

static i2s_chan_handle_t s_mic_rx = NULL;


esp_err_t board_audio_init(void)
{
    if (s_mic_rx != NULL) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "Initializing microphone");

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, NULL, &s_mic_rx), TAG, "microphone channel");

    i2s_std_config_t mic_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_SCK,
            .ws = MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    mic_config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    esp_err_t ret = i2s_channel_init_std_mode(s_mic_rx, &mic_config);
    if (ret == ESP_OK) ret = i2s_channel_enable(s_mic_rx);
    if (ret != ESP_OK) {
        i2s_del_channel(s_mic_rx);
        s_mic_rx = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "Microphone ready: 16000 Hz");
    ESP_LOGI(TAG, "SCK=%d WS=%d DIN=%d", MIC_SCK, MIC_WS, MIC_SD);

    return board_speaker_init();
}

esp_err_t board_microphone_read_pcm(int16_t *samples, size_t capacity,
                                    size_t *count, uint32_t timeout_ms)
{
    if (samples == NULL || count == NULL || capacity == 0) return ESP_ERR_INVALID_ARG;
    *count = 0;
    if (s_mic_rx == NULL) return ESP_ERR_INVALID_STATE;
    int32_t raw[256];
    size_t frames = capacity < 256 ? capacity : 256;
    size_t bytes = 0;
    esp_err_t err = i2s_channel_read(s_mic_rx, raw, frames * sizeof(raw[0]),
                                   &bytes, timeout_ms);
    *count = bytes / sizeof(raw[0]);
    for (size_t i = 0; i < *count; ++i) samples[i] = (int16_t)(raw[i] >> 16);
    return err;
}

esp_err_t board_microphone_read_stats(int32_t *min_sample, int32_t *max_sample, uint32_t *peak)
{
    if (min_sample == NULL || max_sample == NULL || peak == NULL) return ESP_ERR_INVALID_ARG;
    if (s_mic_rx == NULL) return ESP_ERR_INVALID_STATE;
    int32_t samples[256];
    size_t bytes_read = 0;

    esp_err_t ret = i2s_channel_read(s_mic_rx, samples, sizeof(samples), &bytes_read, 100);

    if (ret != ESP_OK)
    {
        return ret;
    }

    size_t sample_count = bytes_read / sizeof(int32_t);

    if (sample_count == 0)
    {
        *min_sample = 0;
        *max_sample = 0;
        *peak = 0;
        return ESP_OK;
    }

    int32_t min_value = INT32_MAX;
    int32_t max_value = INT32_MIN;
    uint32_t peak_value = 0;

    for (size_t i = 0; i < sample_count; i++)
    {
        int32_t sample = samples[i];

        if (sample < min_value)
        {
            min_value = sample;
        }

        if (sample > max_value)
        {
            max_value = sample;
        }

        uint32_t magnitude;

        if (sample == INT32_MIN)
        {
            magnitude = 2147483648U;
        }
        else
        {
            magnitude = (uint32_t)abs(sample);
        }

        if (magnitude > peak_value)
        {
            peak_value = magnitude;
        }
    }

    *min_sample = min_value;
    *max_sample = max_value;
    *peak = peak_value;

    return ESP_OK;
}
