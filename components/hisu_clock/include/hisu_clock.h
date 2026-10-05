#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t hisu_clock_start(void);
esp_err_t hisu_clock_set(uint64_t utc_seconds, int offset_minutes);
bool hisu_clock_read(char text[6]);
/* Today's Gregorian date in Vietnam (UTC+7); false until the clock is valid. */
bool hisu_clock_vietnam_date(int *day, int *month, int *year);
void hisu_clock_network_connected(void);
