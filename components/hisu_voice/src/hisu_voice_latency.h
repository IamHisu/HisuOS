#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    VOICE_START_UI, VOICE_START_PERF, VOICE_START_WSS, VOICE_START_CONNECTED,
    VOICE_START_SETUP, VOICE_START_READY, VOICE_START_RX, VOICE_START_PLAY,
    VOICE_START_MIC, VOICE_START_CUE, VOICE_START_COUNT
} voice_start_phase_t;
typedef struct {
    uint32_t epoch, seen;
    int64_t started_us, phase_us[VOICE_START_COUNT], build_us, send_us;
    size_t setup_bytes;
    unsigned connections;
    bool reported;
} voice_start_t;
static inline void voice_start_mark(voice_start_t *s, voice_start_phase_t phase, int64_t now)
{
    if (!(s->seen&(1U<<phase))) { s->seen|=1U<<phase; s->phase_us[phase]=now; }
}
static inline long long voice_start_ms(const voice_start_t *s, voice_start_phase_t phase)
{
    return s->seen&(1U<<phase) ? (long long)((s->phase_us[phase]-s->started_us)/1000) : -1;
}
