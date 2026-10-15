#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    HISU_FACE_AUTO, HISU_FACE_NEUTRAL, HISU_FACE_HAPPY, HISU_FACE_SAD,
    HISU_FACE_SURPRISED, HISU_FACE_CURIOUS, HISU_FACE_SLEEPY, HISU_FACE_FOCUSED,
    HISU_FACE_COUNT
} hisu_face_t;

static inline bool hisu_face_parse(const char *name, hisu_face_t *face)
{
    static const char *const names[] = {
        "auto", "neutral", "happy", "sad", "surprised", "curious", "sleepy", "focused"
    };
    if (!name) return false;
    for (int i = 0; i < HISU_FACE_COUNT; ++i) {
        if (!strcmp(name, names[i])) { *face = (hisu_face_t)i; return true; }
    }
    return false;
}

typedef struct { int x, y, width, height, radius; } hisu_eye_t;

/* Fixed 240x240 layout: eyes stay above the subtitle band at y=164. */
static inline void hisu_face_frame(hisu_face_t face, bool thinking, bool speaking,
                                  uint32_t ms, hisu_eye_t eyes[2])
{
    int gaze = thinking ? ((int)(ms / 700 % 3) - 1) * 8 :
        ((int)(ms / 2300 % 3) - 1) * 3;
    int bob = speaking ? (int)(ms / 160 % 3) - 1 : 0;
    uint32_t blink = ms % 4300;
    for (int i = 0; i < 2; ++i) {
        int w = 48, h = 58, y = 78 + bob;
        switch (face) {
        case HISU_FACE_HAPPY: h = 38; y = 82 + bob; break;
        case HISU_FACE_SAD: h = 25; y = 108; break;
        case HISU_FACE_SURPRISED: w = 58; h = 72; y = 65; break;
        case HISU_FACE_CURIOUS: h = i ? 64 : 36; y = i ? 72 : 92; break;
        case HISU_FACE_SLEEPY: h = 12; y = 108; break;
        case HISU_FACE_FOCUSED: h = 30; y = 90; break;
        default: break;
        }
        if (blink < 150) {
            int phase = blink < 75 ? (int)blink : 150 - (int)blink;
            int closed = h - (h - 4) * phase / 75;
            y += (h - closed) / 2; h = closed;
        }
        eyes[i] = (hisu_eye_t){(i ? 151 : 41) - w / 2 + 24 + gaze, y, w, h,
            face == HISU_FACE_HAPPY ? 10 : 12};
    }
}
