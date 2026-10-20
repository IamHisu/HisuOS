#pragma once

#include <stdint.h>
#include <stdbool.h>

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
