#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*board_button_callback_t)(void);

esp_err_t board_buttons_init(void);
void board_buttons_set_boot_long_press_callback(board_button_callback_t callback);
void board_buttons_set_boot_click_callback(board_button_callback_t callback);

#ifdef __cplusplus
}
#endif
