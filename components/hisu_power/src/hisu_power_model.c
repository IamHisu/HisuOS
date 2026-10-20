#include <stdint.h>

int hisu_power_saving_next(int active, int valid, int external_power, int percent)
{
    if (!valid || percent < 0 || percent > 100) return active != 0;
    if (external_power || percent >= 15) return 0;
    if (percent <= 10) return 1;
    return active != 0;
}

/* Profile adapted from Muse's Zhengchen board, Apache-2.0.
 * Copyright (c) Meta Platforms, Inc. and affiliates. See licenses/NOTICE.md.
 */
int hisu_power_percent_from_adc(int raw_adc)
{
    const int levels[] = {2030, 2134, 2252, 2370, 2488, 2606};
    if (raw_adc <= 1950) return 0;
    if (raw_adc >= 2526) return 100;
    int adjusted = raw_adc + 80;
    for (int i = 0; i < 5; ++i)
        if (adjusted < levels[i + 1]) return i * 20 + (adjusted - levels[i]) * 20 / (levels[i + 1] - levels[i]);
    return 100;
}

/* Require a meaningful measured drop; never infer runtime from percent alone. */
int64_t hisu_power_remaining_seconds(int start_percent, int current_percent, uint64_t elapsed_seconds)
{
    if (start_percent < 0 || start_percent > 100 || current_percent < 0 || current_percent > 100) return -1;
    if (current_percent == 0) return 0;
    int drop = start_percent - current_percent;
    if (drop < 5 || elapsed_seconds < 600 || elapsed_seconds > 31536000) return -1;
    return (int64_t)(elapsed_seconds * (uint64_t)current_percent / (uint64_t)drop);
}
