#pragma once
#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "hisu_voice_music_lyrics.h"

typedef struct {
    uint32_t ticket;
    char id[41];
    char title[128], artist[128], url[4096];
    hisu_music_lyrics_t *lyrics;
} hisu_music_track_t;
typedef bool (*hisu_music_pcm_fn)(const int16_t *pcm, size_t samples, int rate, int channels);

esp_err_t hisu_music_search(const char *query, cJSON *results);
esp_err_t hisu_music_prepare(const char *id, hisu_music_track_t *track);
esp_err_t hisu_music_queue(const char *id, hisu_music_track_t *track);
/* The playback owner reports any resolve error before finishing this ticket. */
esp_err_t hisu_music_resolve(hisu_music_track_t *track);
/* Optional, before PCM is published; caller/track playback owner frees result. */
hisu_music_lyrics_t *hisu_music_load_lyrics(const hisu_music_track_t *track);
esp_err_t hisu_music_play(const hisu_music_track_t *track, hisu_music_pcm_fn output);
bool hisu_music_cancel(void);
bool hisu_music_busy(void);
esp_err_t hisu_music_repeat(uint32_t previous, const char *id, hisu_music_track_t *track);
bool hisu_music_current(uint32_t ticket);
bool hisu_music_completed(uint32_t ticket);
void hisu_music_finish(uint32_t ticket);

bool hisu_music_valid_id(const char *id);
bool hisu_music_valid_url(const char *url);
bool hisu_music_encode_query(const char *text, char *out, size_t capacity);
bool hisu_music_parse_search(const cJSON *root, cJSON *results);
bool hisu_music_parse_track(const cJSON *info, hisu_music_track_t *track);
