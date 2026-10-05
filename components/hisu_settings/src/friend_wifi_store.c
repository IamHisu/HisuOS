#include "friend_wifi_store.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#define WIFI_NAMESPACE       "friend_wifi"
#define WIFI_COUNT_KEY       "count"
#define WIFI_LEGACY_SSID_KEY "ssid"
#define WIFI_LEGACY_PASS_KEY "password"

static const char *TAG = "WIFI_STORE";

static esp_err_t wifi_store_read_network(nvs_handle_t handle, size_t index, friend_wifi_saved_network_t *network);
static esp_err_t wifi_store_write_network(nvs_handle_t handle, size_t index, const char *ssid, const char *password);
static esp_err_t wifi_store_erase_network(nvs_handle_t handle, size_t index);
static esp_err_t wifi_store_get_count(nvs_handle_t handle, size_t *count);
static esp_err_t wifi_store_set_count(nvs_handle_t handle, size_t count);
static esp_err_t wifi_store_migrate_legacy(void);

esp_err_t friend_wifi_store_init(void)
{
    ESP_LOGI(TAG, "Initializing saved Wi-Fi store");

    esp_err_t ret = wifi_store_migrate_legacy();

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Wi-Fi store migration failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Saved Wi-Fi store ready, %u network(s)", (unsigned int)friend_wifi_store_count());

    return ESP_OK;
}

esp_err_t friend_wifi_store_save(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);

    if (ssid_len == 0 || ssid_len > FRIEND_WIFI_SSID_MAX_LEN || password_len > FRIEND_WIFI_PASSWORD_MAX_LEN)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    size_t count = 0;

    ret = wifi_store_get_count(handle, &count);

    if (ret != ESP_OK)
    {
        nvs_close(handle);
        return ret;
    }

    friend_wifi_saved_network_t network;

    for (size_t i = 0; i < count; i++)
    {
        ret = wifi_store_read_network(handle, i, &network);

        if (ret != ESP_OK)
        {
            nvs_close(handle);
            return ret;
        }

        if (strcmp(network.ssid, ssid) == 0)
        {
            ret = wifi_store_write_network(handle, i, ssid, password);

            if (ret == ESP_OK)
            {
                ret = nvs_commit(handle);
            }

            nvs_close(handle);

            if (ret == ESP_OK)
            {
                ESP_LOGI(TAG, "Updated saved Wi-Fi: %s", ssid);
            }

            return ret;
        }
    }

    if (count >= FRIEND_WIFI_MAX_SAVED_NETWORKS)
    {
        nvs_close(handle);

        ESP_LOGW(TAG, "Saved Wi-Fi store is full");
        return ESP_ERR_NO_MEM;
    }

    ret = wifi_store_write_network(handle, count, ssid, password);

    if (ret == ESP_OK)
    {
        ret = wifi_store_set_count(handle, count + 1);
    }

    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Saved Wi-Fi: %s", ssid);
    }

    return ret;
}

esp_err_t friend_wifi_store_get(const char *ssid, char *password, size_t password_size)
{
    if (ssid == NULL || password == NULL || password_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    size_t count = 0;

    ret = wifi_store_get_count(handle, &count);

    if (ret != ESP_OK)
    {
        nvs_close(handle);
        return ret;
    }

    friend_wifi_saved_network_t network;

    for (size_t i = 0; i < count; i++)
    {
        ret = wifi_store_read_network(handle, i, &network);

        if (ret != ESP_OK)
        {
            nvs_close(handle);
            return ret;
        }

        if (strcmp(network.ssid, ssid) == 0)
        {
            strlcpy(password, network.password, password_size);

            nvs_close(handle);
            return ESP_OK;
        }
    }

    nvs_close(handle);

    return ESP_ERR_NOT_FOUND;
}

esp_err_t friend_wifi_store_forget(const char *ssid)
{
    if (ssid == NULL || strlen(ssid) == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    size_t count = 0;

    ret = wifi_store_get_count(handle, &count);

    if (ret != ESP_OK)
    {
        nvs_close(handle);
        return ret;
    }

    friend_wifi_saved_network_t network;
    size_t found_index = count;

    for (size_t i = 0; i < count; i++)
    {
        ret = wifi_store_read_network(handle, i, &network);

        if (ret != ESP_OK)
        {
            nvs_close(handle);
            return ret;
        }

        if (strcmp(network.ssid, ssid) == 0)
        {
            found_index = i;
            break;
        }
    }

    if (found_index == count)
    {
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = found_index; i + 1 < count; i++)
    {
        ret = wifi_store_read_network(handle, i + 1, &network);

        if (ret != ESP_OK)
        {
            nvs_close(handle);
            return ret;
        }

        ret = wifi_store_write_network(handle, i, network.ssid, network.password);

        if (ret != ESP_OK)
        {
            nvs_close(handle);
            return ret;
        }
    }

    ret = wifi_store_erase_network(handle, count - 1);

    if (ret == ESP_OK)
    {
        ret = wifi_store_set_count(handle, count - 1);
    }

    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Forgot saved Wi-Fi: %s", ssid);
    }

    return ret;
}

esp_err_t friend_wifi_store_get_all(friend_wifi_saved_network_t *networks, size_t max_count, size_t *count)
{
    if (count == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *count = 0;

    nvs_handle_t handle;

    esp_err_t ret = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        return ESP_OK;
    }

    if (ret != ESP_OK)
    {
        return ret;
    }

    size_t stored_count = 0;

    ret = wifi_store_get_count(handle, &stored_count);

    if (ret != ESP_OK)
    {
        nvs_close(handle);
        return ret;
    }

    if (networks == NULL || max_count == 0)
    {
        *count = stored_count;
        nvs_close(handle);
        return ESP_OK;
    }

    size_t read_count = stored_count;

    if (read_count > max_count)
    {
        read_count = max_count;
    }

    for (size_t i = 0; i < read_count; i++)
    {
        ret = wifi_store_read_network(handle, i, &networks[i]);

        if (ret != ESP_OK)
        {
            nvs_close(handle);
            return ret;
        }
    }

    *count = read_count;

    nvs_close(handle);

    return ESP_OK;
}

bool friend_wifi_store_is_saved(const char *ssid)
{
    if (ssid == NULL || strlen(ssid) == 0)
    {
        return false;
    }

    char password[FRIEND_WIFI_PASSWORD_MAX_LEN + 1] = {0};

    bool saved = friend_wifi_store_get(ssid, password, sizeof(password)) == ESP_OK;
    memset(password, 0, sizeof(password));
    return saved;
}

size_t friend_wifi_store_count(void)
{
    nvs_handle_t handle;

    esp_err_t ret = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);

    if (ret != ESP_OK)
    {
        return 0;
    }

    size_t count = 0;

    ret = wifi_store_get_count(handle, &count);

    nvs_close(handle);

    if (ret != ESP_OK)
    {
        return 0;
    }

    return count;
}

esp_err_t friend_wifi_store_clear(void)
{
    nvs_handle_t handle;

    esp_err_t ret = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nvs_erase_all(handle);

    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "All saved Wi-Fi networks cleared");
    }

    return ret;
}

static esp_err_t wifi_store_read_network(nvs_handle_t handle, size_t index, friend_wifi_saved_network_t *network)
{
    if (network == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    char ssid_key[16];
    char pass_key[16];

    snprintf(ssid_key, sizeof(ssid_key), "ssid_%u", (unsigned int)index);
    snprintf(pass_key, sizeof(pass_key), "pass_%u", (unsigned int)index);

    memset(network, 0, sizeof(*network));

    size_t ssid_size = sizeof(network->ssid);
    size_t password_size = sizeof(network->password);

    esp_err_t ret = nvs_get_str(handle, ssid_key, network->ssid, &ssid_size);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nvs_get_str(handle, pass_key, network->password, &password_size);

    return ret;
}

static esp_err_t wifi_store_write_network(nvs_handle_t handle, size_t index, const char *ssid, const char *password)
{
    char ssid_key[16];
    char pass_key[16];

    snprintf(ssid_key, sizeof(ssid_key), "ssid_%u", (unsigned int)index);
    snprintf(pass_key, sizeof(pass_key), "pass_%u", (unsigned int)index);

    esp_err_t ret = nvs_set_str(handle, ssid_key, ssid);

    if (ret != ESP_OK)
    {
        return ret;
    }

    return nvs_set_str(handle, pass_key, password);
}

static esp_err_t wifi_store_erase_network(nvs_handle_t handle, size_t index)
{
    char ssid_key[16];
    char pass_key[16];

    snprintf(ssid_key, sizeof(ssid_key), "ssid_%u", (unsigned int)index);
    snprintf(pass_key, sizeof(pass_key), "pass_%u", (unsigned int)index);

    esp_err_t ret = nvs_erase_key(handle, ssid_key);

    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND)
    {
        return ret;
    }

    ret = nvs_erase_key(handle, pass_key);

    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND)
    {
        return ret;
    }

    return ESP_OK;
}

static esp_err_t wifi_store_get_count(nvs_handle_t handle, size_t *count)
{
    if (count == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t stored_count = 0;

    esp_err_t ret = nvs_get_u8(handle, WIFI_COUNT_KEY, &stored_count);

    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        *count = 0;
        return ESP_OK;
    }

    if (ret != ESP_OK)
    {
        return ret;
    }

    if (stored_count > FRIEND_WIFI_MAX_SAVED_NETWORKS)
    {
        return ESP_ERR_INVALID_STATE;
    }

    *count = stored_count;

    return ESP_OK;
}

static esp_err_t wifi_store_set_count(nvs_handle_t handle, size_t count)
{
    if (count > FRIEND_WIFI_MAX_SAVED_NETWORKS)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return nvs_set_u8(handle, WIFI_COUNT_KEY, (uint8_t)count);
}

static esp_err_t wifi_store_migrate_legacy(void)
{
    nvs_handle_t handle;

    esp_err_t ret = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    size_t count = 0;

    ret = wifi_store_get_count(handle, &count);

    if (ret != ESP_OK)
    {
        nvs_close(handle);
        return ret;
    }

    if (count > 0)
    {
        nvs_close(handle);
        return ESP_OK;
    }

    char ssid[FRIEND_WIFI_SSID_MAX_LEN + 1] = {0};
    char password[FRIEND_WIFI_PASSWORD_MAX_LEN + 1] = {0};

    size_t ssid_size = sizeof(ssid);
    size_t password_size = sizeof(password);

    ret = nvs_get_str(handle, WIFI_LEGACY_SSID_KEY, ssid, &ssid_size);

    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        return ESP_OK;
    }

    if (ret != ESP_OK)
    {
        nvs_close(handle);
        return ret;
    }

    ret = nvs_get_str(handle, WIFI_LEGACY_PASS_KEY, password, &password_size);

    if (ret != ESP_OK)
    {
        nvs_close(handle);
        return ret;
    }

    ESP_LOGI(TAG, "Migrating legacy Wi-Fi credential: %s", ssid);

    ret = wifi_store_write_network(handle, 0, ssid, password);

    if (ret == ESP_OK)
    {
        ret = wifi_store_set_count(handle, 1);
    }

    if (ret == ESP_OK)
    {
        ret = nvs_erase_key(handle, WIFI_LEGACY_SSID_KEY);
    }

    if (ret == ESP_OK)
    {
        ret = nvs_erase_key(handle, WIFI_LEGACY_PASS_KEY);
    }

    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Legacy Wi-Fi credential migrated");
    }

    return ret;
}
