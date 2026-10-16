#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL=-1, ESP_ERR_INVALID_STATE=2, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM,
    ESP_ERR_NOT_FOUND, ESP_PARTITION_TYPE_DATA, MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2, portMAX_DELAY=-1 };
typedef void *SemaphoreHandle_t;
static unsigned mock_depth;
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static int xSemaphoreTake(void *lock,int ticks) { assert(!mock_depth); ++mock_depth; return 1; }
static int xSemaphoreGive(void *lock) { assert(mock_depth==1); --mock_depth; return 1; }
static void *heap_caps_calloc(size_t count,size_t size,int caps) { return calloc(count,size); }
typedef struct { size_t size; } esp_partition_t;
static uint8_t mock_flash[0x4e0000];
static esp_partition_t mock_partition={sizeof(mock_flash)};
static int mock_budget=-1;
static void (*mock_hook)(void);
static const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *name) { return &mock_partition; }
static bool allow_write(void) {
    assert(!mock_depth);
    if (mock_hook) { void (*hook)(void)=mock_hook; mock_hook=NULL; hook(); }
    if (!mock_budget) return false;
    if (mock_budget>0) --mock_budget;
    return true;
}
static esp_err_t esp_partition_read(const esp_partition_t *p,size_t offset,void *data,size_t size) {
    assert(offset>=0x3e6000 && offset+size<=0x3ea000); memcpy(data,mock_flash+offset,size); return ESP_OK;
}
static esp_err_t esp_partition_erase_range(const esp_partition_t *p,size_t offset,size_t size) {
    assert(offset>=0x3e6000 && offset+size<=0x3ea000 && !(offset%4096) && size==8192);
    if (!allow_write()) return ESP_FAIL; memset(mock_flash+offset,0xff,size); return ESP_OK;
}
static esp_err_t esp_partition_write(const esp_partition_t *p,size_t offset,const void *data,size_t size) {
    assert(offset>=0x3e6000 && offset+size<=0x3ea000);
    bool ok=allow_write(); memcpy(mock_flash+offset,data,ok ? size : size/2); return ok ? ESP_OK : ESP_FAIL;
}
