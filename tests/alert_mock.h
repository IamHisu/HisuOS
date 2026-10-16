#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "hisu_storage_layout.h"
typedef int esp_err_t;
enum { ESP_OK,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=2,ESP_ERR_INVALID_ARG,ESP_ERR_NO_MEM,
    ESP_ERR_NOT_FOUND,ESP_ERR_TIMEOUT,ESP_ERR_HTTP_EAGAIN,ESP_PARTITION_TYPE_DATA,
    MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,portMAX_DELAY=-1,HTTP_METHOD_POST=1,HTTP_EVENT_ON_DATA=2 };
#define strcasecmp _stricmp
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
typedef void *SemaphoreHandle_t;
static unsigned mock_lock_depth;
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static int xSemaphoreTake(SemaphoreHandle_t lock, int ticks) { assert(!mock_lock_depth); ++mock_lock_depth; return 1; }
static int xSemaphoreGive(SemaphoreHandle_t lock) { assert(mock_lock_depth==1); --mock_lock_depth; return 1; }
static int mock_alloc_budget=-1;
static void *heap_caps_malloc(size_t bytes,int caps) {
    if (!mock_alloc_budget) return NULL;
    if (mock_alloc_budget>0) --mock_alloc_budget;
    return malloc(bytes);
}
typedef struct { size_t size; } esp_partition_t;
#define MOCK_ALERT_BASE (HISU_MEMORY_OFFSET + 2U * HISU_MEMORY_SLOT_BYTES)
static uint8_t mock_flash[0x4e0000];
static esp_partition_t mock_partition = {sizeof(mock_flash)};
static int mock_flash_budget=-1;
static unsigned mock_flash_writes;
static bool mock_flash_allowed(void) {
    assert(mock_lock_depth==0); ++mock_flash_writes;
    if (!mock_flash_budget) return false;
    if (mock_flash_budget>0) --mock_flash_budget;
    return true;
}
static const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *name) { return &mock_partition; }
static esp_err_t esp_partition_read(const esp_partition_t *p,size_t offset,void *out,size_t bytes) {
    assert(offset>=MOCK_ALERT_BASE && offset+bytes<=sizeof(mock_flash));
    memcpy(out,mock_flash+offset,bytes); return ESP_OK;
}
static esp_err_t esp_partition_erase_range(const esp_partition_t *p,size_t offset,size_t bytes) {
    assert(offset>=MOCK_ALERT_BASE && !(offset%4096) && bytes==0x60000 && offset+bytes<=sizeof(mock_flash));
    if (!mock_flash_allowed()) return ESP_FAIL;
    memset(mock_flash+offset,0xff,bytes); return ESP_OK;
}
static esp_err_t esp_partition_write(const esp_partition_t *p,size_t offset,const void *data,size_t bytes) {
    assert(offset>=MOCK_ALERT_BASE && offset+bytes<=sizeof(mock_flash));
    bool ok=mock_flash_allowed(); memcpy(mock_flash+offset,data,ok ? bytes : bytes/2);
    return ok ? ESP_OK : ESP_FAIL;
}
static int64_t mock_now=1000000;
static int64_t esp_timer_get_time(void) { return mock_now; }
static void vTaskDelay(unsigned ticks) { mock_now+=(int64_t)ticks*1000; }
typedef struct { int event_id,data_len; void *user_data,*data; } esp_http_client_event_t;
typedef struct {
    const char *url; int method,timeout_ms,buffer_size,buffer_size_tx;
    bool is_async,disable_auto_redirect;
    esp_err_t (*crt_bundle_attach)(void *);
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    void *user_data;
} esp_http_client_config_t;
typedef struct { esp_http_client_config_t config; unsigned calls; } *esp_http_client_handle_t;
static unsigned mock_http_live,mock_http_calls;
static int mock_http_status=200,mock_mode;
static bool mock_idle=true,mock_charging=true;
static esp_err_t esp_crt_bundle_attach(void *arg) { return ESP_OK; }
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    assert(strstr(config->url,"gemini-2.5-flash-preview-tts:generateContent"));
    assert(config->crt_bundle_attach && config->is_async && config->disable_auto_redirect);
    esp_http_client_handle_t http=calloc(1,sizeof(*http));
    if (http) { http->config=*config; ++mock_http_live; }
    return http;
}
static esp_err_t esp_http_client_set_header(esp_http_client_handle_t http,const char *key,const char *value) {
    if (!strcmp(key,"x-goog-api-key")) assert(!strcmp(value,"fake-key"));
    return ESP_OK;
}
static esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t http,const char *body,int size) {
    cJSON *json=cJSON_Parse(body); assert(json && size==(int)strlen(body));
    cJSON *voice=cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(json,"generationConfig"),"speechConfig"),"voiceConfig"),"prebuiltVoiceConfig");
    assert(!strcmp(cJSON_GetObjectItem(voice,"voiceName")->valuestring,"Kore"));
    cJSON_Delete(json); return ESP_OK;
}
static esp_err_t esp_http_client_perform(esp_http_client_handle_t http) {
    ++mock_http_calls;
    if (mock_mode==7 || !http->calls++) return ESP_ERR_HTTP_EAGAIN;
    const char *body="{\"candidates\":[{\"finishReason\":\"STOP\",\"content\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/L16;rate=24000\",\"data\":\"ok\"}}]}}]}";
    if (mock_mode==1) body="{\"candidates\":[]}";
    if (mock_mode==3) body="<html>error</html>";
    if (mock_mode==4) body="{\"candidates\":[{\"finishReason\":\"STOP\",\"content\":{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/L16;rate=44100\",\"data\":\"ok\"}}]}}]}";
    if (mock_mode==8) mock_charging=false;
    esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.user_data=http->config.user_data,
        .data=(void *)body,.data_len=mock_mode==5 ? 600000 : (int)strlen(body)};
    return http->config.event_handler(&event);
}
static int esp_http_client_get_status_code(esp_http_client_handle_t http) { return mock_http_status; }
static bool esp_http_client_is_complete_data_received(esp_http_client_handle_t http) { return mock_mode!=6; }
static esp_err_t esp_http_client_cleanup(esp_http_client_handle_t http) { --mock_http_live; free(http); return ESP_OK; }
static int mbedtls_base64_decode(unsigned char *out,size_t capacity,size_t *bytes,const unsigned char *in,size_t n) {
    if (mock_mode==2 || capacity<24000) return -1;
    *bytes=24000; memset(out,mock_mode==9 ? 0 : 4,*bytes); return 0;
}
