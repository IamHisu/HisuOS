#pragma once
#include "esp_http_server.h"
#include <stdbool.h>
esp_err_t hisu_setup_api_register(httpd_handle_t server,
    bool (*guard)(httpd_req_t *), void (*headers)(httpd_req_t *));
