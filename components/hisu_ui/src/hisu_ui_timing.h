#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct { bool pending, woke_screen; int64_t released_ms; } hisu_ui_boot_click_t;
enum { HISU_BOOT_CLICK_NONE, HISU_BOOT_CLICK_SINGLE, HISU_BOOT_CLICK_DOUBLE };

/* Active audio acts on press; swallow its later release/hold even after a view change. */
static inline int hisu_ui_audio_boot(bool *latched,bool pressed,bool enabled)
{
    if (*latched) { if (!pressed) *latched=false; return -1; }
    if (pressed && enabled) { *latched=true; return 1; }
    return 0;
}

/* Delay only idle single clicks; other buttons cancel them, never replay them. */
static inline int hisu_ui_boot_click(hisu_ui_boot_click_t *s, bool released,
    bool pressed, bool enabled, bool waking, int64_t now)
{
    if (!enabled) { s->pending=false; return HISU_BOOT_CLICK_NONE; }
    if (released) {
        if (s->pending && now>=s->released_ms && now-s->released_ms<=350) {
            s->pending=false; return HISU_BOOT_CLICK_DOUBLE;
        }
        s->pending=true; s->woke_screen=waking; s->released_ms=now;
    } else if (s->pending && !pressed && now>=s->released_ms && now-s->released_ms>350) {
        s->pending=false;
        return s->woke_screen ? HISU_BOOT_CLICK_NONE : HISU_BOOT_CLICK_SINGLE;
    }
    return HISU_BOOT_CLICK_NONE;
}

static inline int hisu_ui_power_timeout(bool saving, int configured)
{
    return saving && (configured == 0 || configured > 15) ? 15 : configured;
}

static inline int hisu_ui_power_brightness(bool saving, int requested)
{
    return saving && requested > 10 ? 10 : requested;
}

static inline bool hisu_ui_error_due(bool is_error, int64_t shown_ms, int64_t now_ms)
{
    return is_error && now_ms >= shown_ms && now_ms - shown_ms >= 3000;
}

/* Keep buttons responsive, but honor earlier LVGL deadlines without spinning. */
static inline uint32_t hisu_ui_wait_ms(uint32_t next_timer_ms)
{
    if (next_timer_ms == 0) return 1;
    return next_timer_ms < 10 ? next_timer_ms : 10;
}
