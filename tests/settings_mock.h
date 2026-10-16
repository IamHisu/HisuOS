#pragma once
#include "network_mock.h"

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned mock_values_critical_depth;
#define portENTER_CRITICAL(mux) ((void)(mux), ++mock_values_critical_depth)
#define portEXIT_CRITICAL(mux) ((void)(mux), --mock_values_critical_depth)
typedef int nvs_handle_t;
enum { NVS_READWRITE, ESP_ERR_INVALID_SIZE=100, ESP_ERR_NOT_FOUND };
static unsigned char mock_wifi_list[1024], mock_wifi_legacy[128];
static size_t mock_wifi_list_size, mock_wifi_legacy_size;
static unsigned char mock_power_usage[256];
static size_t mock_power_usage_size;
static unsigned mock_power_writes, mock_power_write_failures;
static esp_err_t nvs_flash_init(void) { return ESP_OK; }
static esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) { *handle=1; return ESP_OK; }
static void nvs_close(nvs_handle_t handle) {}
static int32_t mock_wallpaper_default = -1;
static esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *value) {
    if (strcmp(key,"wall_default") || mock_wallpaper_default < 0) return ESP_ERR_NVS_NOT_FOUND;
    *value=mock_wallpaper_default; return ESP_OK;
}
static esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value) {
    if (!strcmp(key,"wall_default")) mock_wallpaper_default=value;
    return ESP_OK;
}
static esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *size) { return ESP_ERR_NVS_NOT_FOUND; }
static esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) { return ESP_OK; }
static esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) { return ESP_OK; }
static esp_err_t (*mock_during_commit)(void);
static esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(mock_values_critical_depth == 0);
    return mock_during_commit ? mock_during_commit() : ESP_OK;
}
static esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size) {
    const void *data;
    size_t length;
    if (!strcmp(key,"wifi_list")) { data=mock_wifi_list; length=mock_wifi_list_size; }
    else if (!strcmp(key,"wifi_creds")) { data=mock_wifi_legacy; length=mock_wifi_legacy_size; }
    else if (!strcmp(key,"power_usage")) { data=mock_power_usage; length=mock_power_usage_size; }
    else return ESP_ERR_NVS_NOT_FOUND;
    if (!length) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < length) { *size=length; return ESP_ERR_INVALID_SIZE; }
    memcpy(out,data,length); *size=length; return ESP_OK;
}
static esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size) {
    if (!strcmp(key,"power_usage")) {
        ++mock_power_writes;
        if (mock_power_write_failures) { --mock_power_write_failures; return ESP_ERR_INVALID_STATE; }
        if (size>sizeof(mock_power_usage)) return ESP_ERR_INVALID_SIZE;
        memcpy(mock_power_usage,data,size); mock_power_usage_size=size; return ESP_OK;
    }
    if (strcmp(key,"wifi_list") || size>sizeof(mock_wifi_list)) return ESP_ERR_INVALID_SIZE;
    memcpy(mock_wifi_list,data,size); mock_wifi_list_size=size; return ESP_OK;
}
