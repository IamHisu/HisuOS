#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t board_audio_init(void);
esp_err_t board_microphone_read_pcm(int16_t *samples, size_t capacity,
                                    size_t *count, uint32_t timeout_ms);
esp_err_t board_speaker_init(void);
// Interleaved stereo, signed PCM16 at 16000 Hz; one writer at a time.
esp_err_t board_speaker_write(const int16_t *samples, size_t count,
                             size_t *written, uint32_t timeout_ms);

esp_err_t board_microphone_read_stats(int32_t *min_sample, int32_t *max_sample, uint32_t *peak);

#ifdef __cplusplus
}
#endif
