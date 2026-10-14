#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef enum { LIVE_READY, LIVE_TEXT, LIVE_AUDIO, LIVE_DONE, LIVE_ERROR, LIVE_INPUT, LIVE_FLASHLIGHT } live_event_t;
typedef void (*live_event_fn)(live_event_t event, uint32_t generation, const char *text);
esp_err_t voice_live_init(live_event_fn callback);
void voice_live_deinit(void);
void voice_live_open(uint32_t generation);
void voice_live_close(void);
bool voice_live_interrupt(uint32_t generation);
bool voice_live_ready(void);
bool voice_live_begin(uint32_t generation);
bool voice_live_feed(uint32_t generation, const int16_t *pcm, size_t samples);
bool voice_live_end(uint32_t generation);
