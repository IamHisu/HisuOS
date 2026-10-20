#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define HISU_WIFI_MAX_APS 12
typedef struct { char ssid[33]; int rssi; bool secure; bool supported; bool saved; bool password_rejected; } hisu_wifi_ap_t;

typedef enum {
    HISU_INTERNET_UNKNOWN,
    HISU_INTERNET_CHECKING,
    HISU_INTERNET_ONLINE,
    HISU_INTERNET_CAPTIVE,
    HISU_INTERNET_OFFLINE,
} hisu_internet_state_t;
typedef struct {
    bool ready, enabled, scanning, joining, connected;
    hisu_internet_state_t internet;
    uint32_t revision;
    esp_err_t error;
    int disconnect_reason;
    char ssid[33], ip[16];
    int rssi, count;
    hisu_wifi_ap_t aps[HISU_WIFI_MAX_APS];
    bool setup_enabled, setup_client_connected;
    char setup_ssid[33], setup_password[17], setup_ip[16];
} hisu_network_status_t;
static inline bool hisu_network_has_internet_warning(const hisu_network_status_t *status)
{
    return status->enabled && status->connected &&
        (status->internet == HISU_INTERNET_CAPTIVE || status->internet == HISU_INTERNET_OFFLINE);
}
esp_err_t hisu_network_start(void);
void hisu_network_get(hisu_network_status_t *out);
/* Small atomic snapshot for nested audio/TLS paths; no scan list on their stack. */
typedef struct {
    bool connected, setup_enabled;
    hisu_internet_state_t internet;
} hisu_network_link_t;
void hisu_network_get_link(hisu_network_link_t *out);
esp_err_t hisu_network_scan(void);
esp_err_t hisu_network_provision_scan(void);
esp_err_t hisu_network_join(const char *ssid, const char *password);
esp_err_t hisu_network_cancel_join(void);
esp_err_t hisu_network_provision(const char *ssid, const char *password);
esp_err_t hisu_network_provision_ex(const char *ssid, const char *password, bool hidden);
esp_err_t hisu_network_enable(bool enabled);
/* Worker applies Wi-Fi power saving changes; voice tasks never own the radio. */
esp_err_t hisu_network_set_performance(bool active);
typedef struct { bool enabled; int64_t applied_us; esp_err_t error; } hisu_network_performance_t;
/* Last driver result, not merely the queued request. */
void hisu_network_get_performance(hisu_network_performance_t *out);
/* Physical setup portal radio lifecycle; password is for the board screen only. */
esp_err_t hisu_network_setup(bool enabled);
bool hisu_network_valid_password(const char *password);
