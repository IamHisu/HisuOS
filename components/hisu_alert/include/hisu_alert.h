#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define HISU_ALERT_COUNT 5U
#define HISU_ALERT_REMINDER 4U
#define HISU_ALERT_PCM_MAX 384000U
typedef struct {
    unsigned ready, current;
    bool requested, busy;
    esp_err_t error;
    int http_status;
} hisu_alert_status_t;
esp_err_t hisu_alert_init(void);
void hisu_alert_get(hisu_alert_status_t *out);
const char *hisu_alert_phrase(unsigned index);
esp_err_t hisu_alert_request(void);
bool hisu_alert_prepare_take(unsigned *index);
void hisu_alert_prepare_finish(esp_err_t error, int http_status);
esp_err_t hisu_alert_store(unsigned index, const int16_t *pcm, size_t bytes);
esp_err_t hisu_alert_read(unsigned index, size_t offset, void *pcm, size_t bytes);
size_t hisu_alert_size(unsigned index);
int hisu_alert_poll(bool valid, bool charging, int percent, int64_t now_us);
void hisu_alert_played(unsigned index);
