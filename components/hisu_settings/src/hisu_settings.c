#include "hisu_settings.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *const s_keys[] = {"brightness", "volume", "speaker", "timeout", "theme", "wifi_on", "tz_minutes", "wall_default"};
static const int s_min[] = {10, 0, 0, 0, 0, 0, -720, 0};
static const int s_max[] = {100, 100, 1, 600, 1, 1, 840, 1};
static int s_values[] = {75, 55, 1, 0, 0, 1, 420, 0};
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

esp_err_t hisu_settings_get_asr_key(char out[128])
{
    if (!s_lock || !out) return ESP_ERR_INVALID_ARG;
    out[0] = 0;
    size_t length = 128;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = nvs_get_str(s_nvs, "groq_key", out, &length);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) memset(out, 0, 128);
    return err;
}

bool hisu_settings_has_asr_key(void)
{
    if (!s_lock) return false;
    size_t length = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool exists = nvs_get_str(s_nvs, "groq_key", NULL, &length) == ESP_OK && length > 1;
    xSemaphoreGive(s_lock);
    return exists;
}

esp_err_t hisu_settings_set_asr_key(const char *key)
{
    if (!s_lock || !key) return ESP_ERR_INVALID_ARG;
    size_t length = strlen(key);
    if (length >= 128) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)key[i] < 33 || (unsigned char)key[i] > 126) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = length ? nvs_set_str(s_nvs, "groq_key", key) : nvs_erase_key(s_nvs, "groq_key");
    if (err == ESP_ERR_NVS_NOT_FOUND && !length) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t hisu_settings_get_gemini_key(char out[128])
{
    if (!s_lock || !out) return ESP_ERR_INVALID_ARG;
    out[0] = 0;
    size_t length = 128;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = nvs_get_str(s_nvs, "gemini_key", out, &length);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) memset(out, 0, 128);
    return err;
}

bool hisu_settings_has_gemini_key(void)
{
    if (!s_lock) return false;
    size_t length = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool exists = nvs_get_str(s_nvs, "gemini_key", NULL, &length) == ESP_OK && length > 1;
    xSemaphoreGive(s_lock);
    return exists;
}

esp_err_t hisu_settings_set_gemini_key(const char *key)
{
    if (!s_lock || !key) return ESP_ERR_INVALID_ARG;
    size_t length = strlen(key);
    if (length >= 128) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)key[i] < 33 || (unsigned char)key[i] > 126) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = length ? nvs_set_str(s_nvs, "gemini_key", key) : nvs_erase_key(s_nvs, "gemini_key");
    if (err == ESP_ERR_NVS_NOT_FOUND && !length) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    xSemaphoreGive(s_lock);
    return err;
}

typedef struct { char ssid[33]; char password[65]; } wifi_credentials_t;

esp_err_t hisu_settings_load_power_usage(hisu_power_usage_record_t *out)
{
    if (!s_lock || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    size_t length = sizeof(*out);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = nvs_get_blob(s_nvs, "power_usage", out, &length);
    xSemaphoreGive(s_lock);
    if (err == ESP_OK && (length != sizeof(*out) || out->version != 1 || out->flags > 7 ||
        out->baseline_percent > 100 || out->last_percent > 100 || out->baseline_battery_us > out->battery_us ||
        out->screen_on_us > out->battery_us || out->screen_sleep_us > out->battery_us)) err = ESP_ERR_INVALID_SIZE;
    if (err != ESP_OK) memset(out, 0, sizeof(*out));
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
}

esp_err_t hisu_settings_save_power_usage(const hisu_power_usage_record_t *record)
{
    if (!s_lock || !record || record->version != 1) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = nvs_set_blob(s_nvs, "power_usage", record, sizeof(*record));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    xSemaphoreGive(s_lock);
    return err;
}
typedef struct {
    uint32_t version, count;
    wifi_credentials_t entries[HISU_WIFI_SAVED_MAX];
    uint32_t hidden_mask;
} wifi_store_t;

static bool valid_credentials(const wifi_credentials_t *entry)
{
    return entry->ssid[0] && memchr(entry->ssid, 0, sizeof(entry->ssid)) &&
           memchr(entry->password, 0, sizeof(entry->password));
}

/* Caller holds s_lock. Preserve the old one-network format on upgrade. */
static esp_err_t read_wifi_store(wifi_store_t *store)
{
    memset(store, 0, sizeof(*store));
    size_t length = sizeof(*store);
    esp_err_t err = nvs_get_blob(s_nvs, "wifi_list", store, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        wifi_credentials_t old = {0};
        length = sizeof(old);
        err = nvs_get_blob(s_nvs, "wifi_creds", &old, &length);
        if (err == ESP_OK && (length != sizeof(old) || !valid_credentials(&old))) err = ESP_ERR_INVALID_SIZE;
        if (err == ESP_OK) { store->entries[0] = old; store->count = 1; }
        memset(&old, 0, sizeof(old));
        store->version = 2;
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    if (err != ESP_OK) return err;
    if (store->version == 1 && length == offsetof(wifi_store_t, hidden_mask)) {
        store->version = 2;
        store->hidden_mask = 0;
    } else if (length != sizeof(*store) || store->version != 2) return ESP_ERR_INVALID_SIZE;
    if (store->count > HISU_WIFI_SAVED_MAX || (store->hidden_mask >> store->count) != 0)
        return ESP_ERR_INVALID_SIZE;
    for (uint32_t i = 0; i < store->count; ++i) {
        if (!valid_credentials(&store->entries[i])) return ESP_ERR_INVALID_SIZE;
        for (uint32_t j = 0; j < i; ++j)
            if (!strcmp(store->entries[i].ssid, store->entries[j].ssid)) return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t hisu_settings_get_wifi(char ssid[33], char password[65])
{
    return hisu_settings_get_wifi_at(0, ssid, password);
}

esp_err_t hisu_settings_get_wifi_at(unsigned index, char ssid[33], char password[65])
{
    if (!s_lock || !ssid || !password) return ESP_ERR_INVALID_ARG;
    wifi_store_t store;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = read_wifi_store(&store);
    xSemaphoreGive(s_lock);
    if (err == ESP_OK && index >= store.count) err = ESP_ERR_NVS_NOT_FOUND;
    if (err == ESP_OK) {
        memcpy(ssid, store.entries[index].ssid, 33);
        memcpy(password, store.entries[index].password, 65);
    } else { ssid[0] = 0; password[0] = 0; }
    memset(&store, 0, sizeof(store));
    return err;
}

esp_err_t hisu_settings_find_wifi(const char *ssid, char password[65])
{
    if (!s_lock || !ssid || !password) return ESP_ERR_INVALID_ARG;
    password[0] = 0;
    wifi_store_t store;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = read_wifi_store(&store);
    xSemaphoreGive(s_lock);
    if (err == ESP_OK) {
        err = ESP_ERR_NVS_NOT_FOUND;
        for (uint32_t i = 0; i < store.count; ++i) {
            if (strcmp(ssid, store.entries[i].ssid)) continue;
            memcpy(password, store.entries[i].password, 65);
            err = ESP_OK; break;
        }
    }
    memset(&store, 0, sizeof(store));
    return err;
}

esp_err_t hisu_settings_save_wifi(const char *ssid, const char *password)
{
    return hisu_settings_save_wifi_ex(ssid, password, false);
}

bool hisu_settings_wifi_hidden(const char *ssid)
{
    if (!s_lock || !ssid) return false;
    wifi_store_t store;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = read_wifi_store(&store);
    xSemaphoreGive(s_lock);
    bool hidden = false;
    if (err == ESP_OK) {
        for (uint32_t i = 0; i < store.count; ++i)
            if (!strcmp(ssid, store.entries[i].ssid)) hidden = (store.hidden_mask & (1U << i)) != 0;
    }
    memset(&store, 0, sizeof(store));
    return hidden;
}

esp_err_t hisu_settings_save_wifi_ex(const char *ssid, const char *password, bool hidden)
{
    if (!s_lock || !ssid || !password || !ssid[0] || strlen(ssid) > 32 || strlen(password) > 64)
        return ESP_ERR_INVALID_ARG;
    wifi_store_t store;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = read_wifi_store(&store);
    if (err == ESP_OK) {
        uint32_t index = 0;
        while (index < store.count && strcmp(store.entries[index].ssid, ssid)) ++index;
        if (index == store.count && store.count < HISU_WIFI_SAVED_MAX) ++store.count;
        if (index >= HISU_WIFI_SAVED_MAX) index = HISU_WIFI_SAVED_MAX - 1;
        for (uint32_t i = index; i > 0; --i) {
            store.entries[i] = store.entries[i - 1];
            store.hidden_mask = (store.hidden_mask & ~(1U << i)) |
                (((store.hidden_mask >> (i - 1)) & 1U) << i);
        }
        store.hidden_mask = (store.hidden_mask & ~1U) | (hidden ? 1U : 0U);
        memset(&store.entries[0], 0, sizeof(store.entries[0]));
        memcpy(store.entries[0].ssid, ssid, strlen(ssid));
        memcpy(store.entries[0].password, password, strlen(password));
        err = nvs_set_blob(s_nvs, "wifi_list", &store, sizeof(store));
    }
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    xSemaphoreGive(s_lock);
    memset(&store, 0, sizeof(store));
    return err;
}
