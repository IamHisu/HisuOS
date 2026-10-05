#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool ready, enabled, connected, authenticated, show_pin;
    uint32_t pin;
    char name[20];
    int error;
} hisu_ble_status_t;
esp_err_t hisu_ble_start(void);
esp_err_t hisu_ble_enable(bool enabled);
void hisu_ble_get(hisu_ble_status_t *out);
