#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
enum { ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_NO_MEM,
       ESP_ERR_TIMEOUT, ESP_ERR_NVS_NOT_FOUND };
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *EventGroupHandle_t;
typedef void *TaskHandle_t;
typedef unsigned EventBits_t;
typedef int esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef struct { int unused; } esp_netif_t;
typedef struct { struct { unsigned addr; } ip; } esp_netif_ip_info_t;
typedef struct { int reason; } wifi_event_sta_disconnected_t;
typedef struct { unsigned char ssid[33]; int rssi, authmode; } wifi_ap_record_t;
typedef struct { bool show_hidden; } wifi_scan_config_t;
typedef struct { int unused; } wifi_init_config_t;
typedef struct {
    struct { unsigned char ssid[32], password[64]; struct { bool capable; } pmf_cfg;
             struct { int authmode; } threshold; } sta;
    struct { unsigned char ssid[33], password[65]; size_t ssid_len;
             int channel, max_connection, authmode, ssid_hidden;
             struct { bool capable; } pmf_cfg; } ap;
} wifi_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) 192U, 168U, 1U, 2U
#define portMAX_DELAY 0
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
enum { WIFI_EVENT, IP_EVENT, WIFI_EVENT_AP_STACONNECTED, WIFI_EVENT_AP_STADISCONNECTED,
       WIFI_EVENT_AP_STOP, WIFI_EVENT_STA_DISCONNECTED, IP_EVENT_STA_GOT_IP,
       ESP_EVENT_ANY_ID, WIFI_MODE_APSTA, WIFI_MODE_AP, WIFI_MODE_STA, WIFI_IF_AP,
       WIFI_IF_STA, WIFI_STORAGE_RAM, ESP_MAC_WIFI_SOFTAP,
       WIFI_AUTH_OPEN, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK,
       WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK,
       WIFI_REASON_ASSOC_LEAVE, WIFI_REASON_AUTH_FAIL,
       WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT, WIFI_REASON_HANDSHAKE_TIMEOUT };
static int64_t mock_now = 1000000;
static wifi_ap_record_t mock_aps[32];
static uint16_t mock_count;
static wifi_config_t mock_config;
static esp_err_t mock_connect_error;
static int mock_connects;
static int mock_disconnects;
static esp_err_t mock_scan_error;
static int mock_scans;
static char mock_saved_ssids[8][33], mock_saved_passwords[8][65];
static unsigned mock_saved_count;
typedef struct {
    esp_err_t error;
    int status, offset;
    const char *body;
    bool incomplete, read_error;
} mock_http_response_t;
typedef mock_http_response_t *esp_http_client_handle_t;
typedef struct {
    const char *url;
    int timeout_ms, buffer_size, buffer_size_tx;
    bool disable_auto_redirect;
    esp_err_t (*crt_bundle_attach)(void *);
} esp_http_client_config_t;
static mock_http_response_t mock_http[2];
static unsigned mock_http_count;
static int mock_read_chunk;
static bool mock_redirect_disabled;
static int mock_http_cleanup_count;
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    mock_redirect_disabled = config->disable_auto_redirect;
    return mock_http_count < 2 ? &mock_http[mock_http_count++] : NULL;
}
static esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len) { return client->error; }
static int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) { return (int64_t)strlen(client->body); }
static int esp_http_client_get_status_code(esp_http_client_handle_t client) { return client->status; }
static int esp_http_client_read(esp_http_client_handle_t client, char *out, int size) {
    if (client->read_error) return -1;
    int remaining = (int)strlen(client->body) - client->offset;
    if (size > remaining) size = remaining;
    if (mock_read_chunk && size > mock_read_chunk) size = mock_read_chunk;
    memcpy(out, client->body + client->offset, size); client->offset += size;
    return size;
}
static bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client) {
    return !client->incomplete && client->offset == (int)strlen(client->body);
}
static esp_err_t esp_http_client_close(esp_http_client_handle_t client) { return ESP_OK; }
static esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { ++mock_http_cleanup_count; return ESP_OK; }
static esp_err_t esp_crt_bundle_attach(void *conf) { return ESP_OK; }
static int64_t esp_timer_get_time(void) { return mock_now; }
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static int xSemaphoreTake(SemaphoreHandle_t lock, int timeout) { return 1; }
static int xSemaphoreGive(SemaphoreHandle_t lock) { return 1; }
static void vSemaphoreDelete(SemaphoreHandle_t lock) {}
static QueueHandle_t xQueueCreate(int count, size_t size) { return (void *)1; }
static int xQueueSend(QueueHandle_t queue, const void *data, int timeout) { return 1; }
static int xQueueReceive(QueueHandle_t queue, void *data, int timeout) { return 0; }
static void vQueueDelete(QueueHandle_t queue) {}
static EventGroupHandle_t xEventGroupCreate(void) { return (void *)1; }
static EventBits_t xEventGroupClearBits(EventGroupHandle_t events, unsigned bits) { return 0; }
static EventBits_t xEventGroupSetBits(EventGroupHandle_t events, unsigned bits) { return bits; }
static EventBits_t xEventGroupWaitBits(EventGroupHandle_t events, unsigned bits, int clear, int all, int timeout) { return bits; }
static void vEventGroupDelete(EventGroupHandle_t events) {}
static int xTaskCreate(void (*task)(void *), const char *name, int stack, void *arg, int priority, void *handle) {
    if (handle) *(TaskHandle_t *)handle = (void *)1;
    return 1;
}
static unsigned ulTaskNotifyTake(int clear, unsigned timeout) { return 0; }
static void xTaskNotifyGive(TaskHandle_t task) {}
static void vTaskDelete(TaskHandle_t task) {}
static esp_err_t esp_wifi_start(void) { return ESP_OK; }
static esp_err_t esp_wifi_stop(void) { return ESP_OK; }
static esp_err_t esp_wifi_disconnect(void) { ++mock_disconnects; return ESP_OK; }
static esp_err_t esp_wifi_connect(void) { ++mock_connects; return mock_connect_error; }
static esp_err_t esp_wifi_set_mode(int mode) { return ESP_OK; }
static esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *config) { mock_config = *config; return ESP_OK; }
static esp_err_t esp_wifi_deauth_sta(int aid) { return ESP_OK; }
static esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block) { ++mock_scans; return mock_scan_error; }
static esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *records) {
    *count = mock_count; memcpy(records, mock_aps, mock_count * sizeof(*records)); return ESP_OK;
}
static esp_err_t esp_wifi_clear_ap_list(void) { return ESP_OK; }
static esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { return ESP_ERR_INVALID_STATE; }
static esp_err_t esp_wifi_init(const wifi_init_config_t *config) { return ESP_OK; }
static esp_err_t esp_wifi_deinit(void) { return ESP_OK; }
static esp_err_t esp_wifi_set_storage(int storage) { return ESP_OK; }
static esp_err_t esp_netif_init(void) { return ESP_OK; }
static esp_netif_t *esp_netif_create_default_wifi_sta(void) { return (esp_netif_t *)1; }
static esp_netif_t *esp_netif_create_default_wifi_ap(void) { return (esp_netif_t *)1; }
static void esp_netif_destroy_default_wifi(esp_netif_t *netif) {}
static esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *ip) { return ESP_OK; }
static esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
static esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int id,
    void (*callback)(void *, esp_event_base_t, int32_t, void *), void *arg,
    esp_event_handler_instance_t *instance) { return ESP_OK; }
static esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int id, esp_event_handler_instance_t instance) { return ESP_OK; }
static esp_err_t esp_read_mac(uint8_t *mac, int type) { memset(mac, 0, 6); return ESP_OK; }
static void esp_fill_random(void *data, size_t size) { memset(data, 1, size); }
static void hisu_clock_network_connected(void) {}
