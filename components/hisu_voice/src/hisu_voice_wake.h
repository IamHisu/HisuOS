#pragma once
#include <stdbool.h>

#define HISU_WAKE_MIN_CONFIDENCE 0.20f

static inline bool voice_wake_accept(int command, float confidence)
{
    return command == 1 && confidence >= HISU_WAKE_MIN_CONFIDENCE && confidence <= 1.0f;
}
