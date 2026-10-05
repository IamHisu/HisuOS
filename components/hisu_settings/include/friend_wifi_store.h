#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRIEND_WIFI_MAX_SAVED_NETWORKS 8
#define FRIEND_WIFI_SSID_MAX_LEN       32
#define FRIEND_WIFI_PASSWORD_MAX_LEN   64

typedef struct
{
    char ssid[FRIEND_WIFI_SSID_MAX_LEN + 1];
    char password[FRIEND_WIFI_PASSWORD_MAX_LEN + 1];
} friend_wifi_saved_network_t;

esp_err_t friend_wifi_store_init(void);

esp_err_t friend_wifi_store_save(const char *ssid, const char *password);

esp_err_t friend_wifi_store_get(const char *ssid, char *password, size_t password_size);

esp_err_t friend_wifi_store_forget(const char *ssid);

esp_err_t friend_wifi_store_get_all(friend_wifi_saved_network_t *networks, size_t max_count, size_t *count);

bool friend_wifi_store_is_saved(const char *ssid);

size_t friend_wifi_store_count(void);

esp_err_t friend_wifi_store_clear(void);

#ifdef __cplusplus
}
#endif