#pragma once
#include <stdbool.h>
#include <stddef.h>

/* All durations use 16 kHz input samples, not detector scheduling time. */
typedef struct {
    float noise_dbfs;
    size_t quiet_samples;
    size_t speech_samples;
    bool noise_known;
    bool armed;
} voice_gate_t;

static inline void voice_gate_pause(voice_gate_t *g)
{
    g->quiet_samples = g->speech_samples = 0;
    g->armed = false;
}

static inline void voice_gate_observe(voice_gate_t *g, bool speech, float dbfs, size_t samples)
{
    /* Do not learn speech or speaker playback as ambient noise. */
    if (speech || !(dbfs >= -90.0f && dbfs <= 0.0f)) return;
    if (!g->noise_known) { g->noise_dbfs = dbfs; g->noise_known = true; }
    else g->noise_dbfs += (dbfs - g->noise_dbfs) *
        ((float)samples / (16000.0f + (float)samples));
}

static inline float voice_gate_threshold(const voice_gate_t *g)
{
    float threshold = g->noise_known ? g->noise_dbfs + 12.0f : -32.0f;
    if (threshold < -32.0f) threshold = -32.0f;
    if (threshold > -24.0f) threshold = -24.0f;
    return threshold;
}

static inline bool voice_gate_start(voice_gate_t *g, bool speech, float dbfs, size_t samples)
{
    bool strong = speech && dbfs >= voice_gate_threshold(g) && dbfs <= 0.0f;
    if (!g->armed) {
        if (strong) g->quiet_samples = 0;
        else if (g->quiet_samples < 4000) g->quiet_samples += samples;
        if (g->quiet_samples >= 4000) g->armed = true;
        return false;
    }
    if (!strong) { g->speech_samples = 0; return false; }
    g->speech_samples += samples;
    return g->speech_samples >= 5120; /* 320 ms, with the existing 600 ms preroll. */
}

static inline bool voice_gate_continue(bool speech, float dbfs, float start_threshold)
{
    /* Hysteresis keeps softer syllables but not weak VAD chatter. */
    return speech && dbfs >= start_threshold - 8.0f && dbfs <= 0.0f;
}
