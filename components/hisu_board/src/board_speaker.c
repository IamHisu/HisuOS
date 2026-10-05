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
#include "board_audio.h"
#include "board_config.h"

#include "driver/i2s_std.h"
#include "esp_check.h"

static i2s_chan_handle_t s_speaker;

esp_err_t board_speaker_init(void)
{
    if (s_speaker != NULL) return ESP_ERR_INVALID_STATE;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, &s_speaker, NULL), "speaker", "channel");
    const i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_BCLK,
            .ws = SPK_LRCK,
            .dout = SPK_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    esp_err_t err = i2s_channel_init_std_mode(s_speaker, &config);
    if (err == ESP_OK) err = i2s_channel_enable(s_speaker);
    if (err != ESP_OK) {
        i2s_del_channel(s_speaker);
        s_speaker = NULL;
    }
    return err;
}

esp_err_t board_speaker_write(const int16_t *samples, size_t count,
                             size_t *written, uint32_t timeout_ms)
{
    if (samples == NULL || written == NULL || count == 0 || count % 2 != 0 ||
        count > SIZE_MAX / sizeof(*samples)) return ESP_ERR_INVALID_ARG;
    *written = 0;
    if (s_speaker == NULL) return ESP_ERR_INVALID_STATE;
    size_t bytes = 0;
    esp_err_t err = i2s_channel_write(s_speaker, samples, count * sizeof(*samples),
                                    &bytes, timeout_ms);
    *written = bytes / sizeof(*samples);
    return err;
}
