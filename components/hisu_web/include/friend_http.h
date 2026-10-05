#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t friend_http_start(void);
esp_err_t friend_http_stop(void);

#ifdef __cplusplus
}
#endif
