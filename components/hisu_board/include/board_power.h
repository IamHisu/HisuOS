#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    int battery_percent;
    bool external_power;
    bool charging;
} board_power_status_t;

esp_err_t board_power_init(void);
// Call from one owner task; battery estimate uses the imported board calibration.
esp_err_t board_power_read(board_power_status_t *status);
