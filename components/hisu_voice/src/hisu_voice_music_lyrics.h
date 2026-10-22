#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define HISU_MUSIC_LRC_MAX_BYTES 32768
#define HISU_MUSIC_LRC_MAX_LINES 256

typedef struct { uint32_t ms; uint16_t offset, order; } music_lyric_line_t;
typedef struct {
    unsigned count;
    music_lyric_line_t *lines;
    char *text;
} hisu_music_lyrics_t;

/* Buffer has capacity *bytes+1; decoded text stays local and never enters AI. */
bool hisu_music_lyrics_decode(char *hex,size_t *bytes,const char *provided_key);
hisu_music_lyrics_t *hisu_music_lyrics_parse(const char *lrc,size_t bytes);
void hisu_music_lyrics_free(hisu_music_lyrics_t *lyrics);
int hisu_music_lyrics_index(const hisu_music_lyrics_t *lyrics,uint64_t position_ms);
