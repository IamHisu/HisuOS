#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef enum { LIVE_READY, LIVE_TEXT, LIVE_AUDIO, LIVE_DONE, LIVE_ERROR, LIVE_INPUT, LIVE_FLASHLIGHT,
               LIVE_END, LIVE_DEVICE, LIVE_EXPRESSION, LIVE_MUSIC, LIVE_MUSIC_LOADING, LIVE_MUSIC_ERROR,
               LIVE_STORY_NEXT, LIVE_SLEEP, LIVE_MUSIC_FINISHED, LIVE_MUSIC_HANDOFF, LIVE_LISTENING, LIVE_REKEY } live_event_t;
typedef void (*live_event_fn)(live_event_t event, uint32_t generation, const char *text);
esp_err_t voice_live_init(live_event_fn callback);
void voice_live_deinit(void);
void voice_live_open(uint32_t generation);
void voice_live_open_at(uint32_t generation, int64_t wake_us);
void voice_live_ui_queued(uint32_t generation);
void voice_live_listening(uint32_t generation);
/* Bounded offline feedback on the existing speaker worker; respects mute/volume. */
void voice_live_feedback(bool error);
bool voice_live_feedback_busy(void);
/* Resume only after the music producer, PCM queue and speaker have drained. */
void voice_live_resume_music(uint32_t generation);
void voice_live_close(void);
bool voice_live_interrupt(uint32_t generation);
bool voice_live_ready(void);
bool voice_live_begin(uint32_t generation);
bool voice_live_feed(uint32_t generation, const int16_t *pcm, size_t samples);
bool voice_live_end(uint32_t generation);
/* NULL in ordinary conversation; otherwise an immutable target-language code. */
const char *voice_live_interpreter_language(void);
bool voice_live_uses_memory(void);
