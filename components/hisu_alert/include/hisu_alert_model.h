#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { unsigned fired; int pending_level, last[2]; int64_t low_since, charge_since; } hisu_alert_policy_t;
int hisu_alert_level(hisu_alert_policy_t *state, bool valid, bool charging, int percent, int64_t now_us);
uint32_t hisu_alert_crc_update(uint32_t crc, const void *data, size_t bytes);
