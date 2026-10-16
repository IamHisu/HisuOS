#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "hisu_storage_layout.h"

typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL=-1, ESP_ERR_INVALID_STATE=2, ESP_ERR_INVALID_ARG,
       ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND, ESP_PARTITION_TYPE_DATA,
       MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2, portMAX_DELAY=-1 };
typedef void *SemaphoreHandle_t;
static unsigned mock_lock_depth;
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static int xSemaphoreTake(SemaphoreHandle_t handle, int ticks) { assert(!mock_lock_depth); ++mock_lock_depth; return 1; }
static int xSemaphoreGive(SemaphoreHandle_t handle) { assert(mock_lock_depth == 1); --mock_lock_depth; return 1; }
static int mock_alloc_budget = -1;
static void *heap_caps_calloc(size_t count, size_t bytes, int caps) {
    if (mock_alloc_budget == 0) return NULL;
    if (mock_alloc_budget > 0) --mock_alloc_budget;
    return calloc(count, bytes);
}
typedef struct { size_t size; } esp_partition_t;
static esp_partition_t mock_partition = {HISU_MEMORY_OFFSET + 2U * HISU_MEMORY_SLOT_BYTES};
static uint8_t mock_flash[HISU_MEMORY_OFFSET + 2U * HISU_MEMORY_SLOT_BYTES];
static int mock_flash_budget = -1;
static unsigned mock_flash_operations;
static void (*mock_during_flash)(void);
static const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *name) { return &mock_partition; }
static esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset, void *out, size_t bytes) {
    assert(offset >= HISU_MEMORY_OFFSET && offset + bytes <= sizeof(mock_flash));
    memcpy(out, mock_flash + offset, bytes); return ESP_OK;
}
static bool mock_flash_allowed(void) {
    assert(mock_lock_depth == 0); ++mock_flash_operations;
    if (mock_during_flash) { void (*hook)(void) = mock_during_flash; mock_during_flash = NULL; hook(); }
    if (mock_flash_budget == 0) return false;
    if (mock_flash_budget > 0) --mock_flash_budget;
    return true;
}
static esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t bytes) {
    assert(offset >= HISU_MEMORY_OFFSET && offset % 4096 == 0 && bytes == HISU_MEMORY_SLOT_BYTES);
    assert(offset + bytes <= sizeof(mock_flash));
    if (!mock_flash_allowed()) return ESP_FAIL;
    memset(mock_flash + offset, 0xff, bytes); return ESP_OK;
}
static esp_err_t esp_partition_write(const esp_partition_t *partition, size_t offset, const void *data, size_t bytes) {
    assert(offset >= HISU_MEMORY_OFFSET && offset + bytes <= sizeof(mock_flash));
    bool ok = mock_flash_allowed();
    if (!ok) { memcpy(mock_flash + offset, data, bytes / 2); return ESP_FAIL; }
    memcpy(mock_flash + offset, data, bytes); return ESP_OK;
}
