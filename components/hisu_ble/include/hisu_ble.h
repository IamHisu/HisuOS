#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool ready, enabled, connected, authenticated, show_pin;
    uint32_t pin;
    char name[20];
    unsigned window_minutes;
    int error;
} hisu_ble_status_t;
esp_err_t hisu_ble_start(void);
esp_err_t hisu_ble_enable(bool enabled);
esp_err_t hisu_ble_open(unsigned minutes); /* 0 closes, 1..30 opens/renews. */
void hisu_ble_get(hisu_ble_status_t *out);
