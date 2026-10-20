#pragma once
#include "esp_err.h"
#include <stddef.h>

#define HISU_FEEDBACK_COUNT 4U
#define HISU_FEEDBACK_OFFSET 0x3ea000U
#define HISU_FEEDBACK_SLOT_BYTES 0x3c000U
/* Read-only PC-generated PCM16, mono, 24 kHz. No flash writes at runtime. */
void hisu_feedback_init(void);
size_t hisu_feedback_size(unsigned index);
esp_err_t hisu_feedback_read(unsigned index, size_t offset, void *pcm, size_t bytes);
