#pragma once
#include "esp_err.h"
#include <stdbool.h>

/* Starts Zhengchen I2S microphone capture and offline Hey Hisu detection. */
esp_err_t hisu_voice_start(void);
/* Active session only: short press interrupts; held press ends the session. */
bool hisu_voice_control(bool end_session);
