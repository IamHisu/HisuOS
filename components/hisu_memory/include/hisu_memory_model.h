#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hisu_storage_layout.h"

#define HISU_MEMORY_COUNT 24U
#define HISU_MEMORY_FACT_BYTES 256U
#define HISU_MEMORY_RECENT_BYTES 2048U
typedef struct { uint32_t id; char text[HISU_MEMORY_FACT_BYTES + 1]; } hisu_memory_fact_t;
typedef struct {
    uint32_t count, next_id, enabled;
    hisu_memory_fact_t facts[HISU_MEMORY_COUNT];
} hisu_memory_data_t;
typedef struct {
    uint32_t magic, version, sequence, length, crc, header_crc, reserved[10];
} hisu_memory_record_t;
bool hisu_memory_fact_valid(const char *text);
bool hisu_memory_data_valid(const hisu_memory_data_t *data);
uint32_t hisu_memory_crc(const void *data, size_t bytes);
void hisu_memory_record_make(hisu_memory_record_t *record, uint32_t sequence, const hisu_memory_data_t *data);
bool hisu_memory_record_valid(const hisu_memory_record_t *record, const hisu_memory_data_t *data);
