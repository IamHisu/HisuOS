#pragma once
#include <stdbool.h>

#define HISU_WAKE_MIN_CONFIDENCE 0.20f

static inline bool voice_wake_accept(int command, float confidence)
{
    return command == 1 && confidence >= HISU_WAKE_MIN_CONFIDENCE && confidence <= 1.0f;
}

/* Speaker leakage must not restart a story on a weak wake-word match. */
#define HISU_WAKE_PLAYBACK_MIN_CONFIDENCE 0.65f
static inline bool voice_wake_accept_context(int command, float confidence, bool playback)
{
    return voice_wake_accept(command, confidence) &&
        (!playback || confidence >= HISU_WAKE_PLAYBACK_MIN_CONFIDENCE);
}
