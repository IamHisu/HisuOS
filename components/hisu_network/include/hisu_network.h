#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define HISU_WIFI_MAX_APS 12
typedef struct { char ssid[33]; int rssi; bool secure; bool supported; bool saved; } hisu_wifi_ap_t;
typedef struct {
    bool ready, enabled, scanning, joining, connected;
    uint32_t revision;
    esp_err_t error;
    int disconnect_reason;
    char ssid[33], ip[16];
    int rssi, count;
    hisu_wifi_ap_t aps[HISU_WIFI_MAX_APS];
} hisu_network_status_t;
esp_err_t hisu_network_start(void);
void hisu_network_get(hisu_network_status_t *out);
esp_err_t hisu_network_scan(void);
esp_err_t hisu_network_join(const char *ssid, const char *password);
esp_err_t hisu_network_enable(bool enabled);
bool hisu_network_valid_password(const char *password);
