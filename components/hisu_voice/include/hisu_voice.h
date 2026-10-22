#pragma once
#include "esp_err.h"
#include <stdbool.h>

/* Starts Zhengchen I2S microphone capture and offline Hey Hisu detection. */
esp_err_t hisu_voice_start(void);
/* Internal interruption/end control; physical BOOT uses hisu_voice_boot(). */
bool hisu_voice_control(bool end_session);
bool hisu_voice_wake(void);
/* Double BOOT leaves music/story for Chat, or ends an ordinary conversation. */
bool hisu_voice_boot(void);
/* -1 outside music/story; 0=off, 1=repeat one, 2=automatic next. RAM only. */
int hisu_voice_playback_mode(bool cycle);
esp_err_t hisu_voice_submit_text(const char *text);
