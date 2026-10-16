#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define HISU_SCHEDULE_COUNT 32U
#define HISU_SCHEDULE_TEXT 160U
typedef struct {
    int64_t utc;
    uint32_t id, delivered;
    char text[HISU_SCHEDULE_TEXT + 1];
} hisu_schedule_event_t;
esp_err_t hisu_schedule_init(void);
/* Mutations are pending until the idle worker flushes them. */
esp_err_t hisu_schedule_set(uint32_t id, int64_t utc, const char *text, int64_t now, uint32_t *result);
esp_err_t hisu_schedule_delete(uint32_t id);
bool hisu_schedule_at(unsigned index, hisu_schedule_event_t *out);
bool hisu_schedule_due(int64_t now, hisu_schedule_event_t *out);
esp_err_t hisu_schedule_flush(void);
/* Retain past entries for seven days; never remove future events. RAM-only. */
unsigned hisu_schedule_prune(int64_t now);
/* Persist the delivery claim before playback, preventing duplicates after reboot. */
esp_err_t hisu_schedule_claim(uint32_t id);
uint32_t hisu_schedule_revision(void);
char *hisu_schedule_json(int64_t from, int64_t until);
/* Strict local YYYY-MM-DDTHH:MM:SS, fixed device UTC offset, no DST guessing. */
int64_t hisu_schedule_local_utc(const char *text, int offset_minutes);
