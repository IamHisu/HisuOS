#include "hisu_settings.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *const s_keys[] = {"brightness", "volume", "speaker", "timeout"};
static const int s_min[] = {10, 0, 0, 0};
static const int s_max[] = {100, 100, 1, 600};
static int s_values[] = {75, 55, 1, 0};
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;

esp_err_t hisu_settings_init(void)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;
    // Do not erase existing NVS automatically when schema/storage fails.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    err = nvs_open("hisu_device", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) return err;
    for (int i = 0; i < HISU_SETTING_COUNT; ++i) {
        int32_t value;
        err = nvs_get_i32(s_nvs, s_keys[i], &value);
        if (err == ESP_ERR_NVS_NOT_FOUND) continue;
        if (err != ESP_OK) {
            nvs_close(s_nvs);
            return err;
        }
        if (value >= s_min[i] && value <= s_max[i]) s_values[i] = value;
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        nvs_close(s_nvs);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

int hisu_settings_get(hisu_setting_t key)
{
    if (!s_lock || key < 0 || key >= HISU_SETTING_COUNT) return -1;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int value = s_values[key];
    xSemaphoreGive(s_lock);
    return value;
}

esp_err_t hisu_settings_set(hisu_setting_t key, int value)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (key < 0 || key >= HISU_SETTING_COUNT || value < s_min[key] || value > s_max[key]) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (value != s_values[key]) {
        err = nvs_set_i32(s_nvs, s_keys[key], value);
        if (err == ESP_OK) err = nvs_commit(s_nvs);
        if (err == ESP_OK) s_values[key] = value;
    }
    xSemaphoreGive(s_lock);
    return err;
}
