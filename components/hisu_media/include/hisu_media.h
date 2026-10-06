#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define HISU_MEDIA_WIDTH 240
#define HISU_MEDIA_HEIGHT 240
#define HISU_MEDIA_BYTES (HISU_MEDIA_WIDTH * HISU_MEDIA_HEIGHT * 2)

typedef struct {
    bool ready, enabled, opening, receiving, pending, saving, has_image;
    uint32_t revision;
    esp_err_t error;
} hisu_media_status_t;

esp_err_t hisu_media_start(void);
esp_err_t hisu_media_open(void);
void hisu_media_close(void);
esp_err_t hisu_media_restore_default(void);
void hisu_media_get(hisu_media_status_t *out);
/* Copy into caller-owned storage: no LVGL objects or shared pixel pointers. */
bool hisu_media_copy_image(void *pixels, size_t size, uint32_t *revision);
