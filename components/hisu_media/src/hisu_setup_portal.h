#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"
esp_err_t hisu_setup_portal_register(httpd_handle_t server);
esp_err_t hisu_setup_portal_redirect(httpd_req_t *req);
esp_err_t hisu_setup_portal_start(bool (*active)(void));
void hisu_setup_portal_stop(void);
bool hisu_setup_peer_is_associated(uint32_t address);
bool hisu_setup_socket_ipv4(int fd, bool peer, uint32_t *address);
