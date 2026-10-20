#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool valid, external_power;
    int percent, raw_adc;
    uint64_t screen_on_seconds, screen_sleep_seconds, battery_seconds;
    bool remaining_valid;
    uint64_t remaining_seconds;
    bool previous_valid;
    uint64_t total_active_seconds, previous_battery_seconds;
    bool power_saving;
} hisu_power_status_t;
esp_err_t hisu_power_start(void);
void hisu_power_get(hisu_power_status_t *out);
void hisu_power_set_screen_on(bool on);
int hisu_power_percent_from_adc(int raw_adc);
int64_t hisu_power_remaining_seconds(int start_percent, int current_percent, uint64_t elapsed_seconds);
int hisu_power_saving_next(int active, int valid, int external_power, int percent);
