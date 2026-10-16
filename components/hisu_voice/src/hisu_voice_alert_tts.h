#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include <stdbool.h>
/* Runs at most one requested clip on live_send, only with idle audio ownership. */
bool hisu_voice_alert_prepare(bool (*idle)(void));
/* Short one-shot speech: PSRAM PCM ownership passes to caller; no flash write. */
esp_err_t hisu_voice_reminder_tts(const char *text, bool (*idle)(void), int16_t **pcm, size_t *bytes);
