#pragma once

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#ifndef CONFIG_HISU_VOICE_TTS_MODEL
#define CONFIG_HISU_VOICE_TTS_MODEL "gemini-2.5-flash-preview-tts"
#endif
#define HISU_VOICE_TTS_URL "https://generativelanguage.googleapis.com/v1beta/models/" \
    CONFIG_HISU_VOICE_TTS_MODEL ":generateContent"
