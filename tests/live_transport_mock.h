#pragma once
#include "network_mock.h"
#include <stdlib.h>
typedef int portMUX_TYPE;
typedef int StaticQueue_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define ESP_LOGI(...) ((void)0)
enum { MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_INTERNAL = 4 };
static void *heap_caps_malloc(size_t size, int caps) { return malloc(size); }
static size_t heap_caps_get_free_size(int caps) { return 1024 * 1024; }
static size_t heap_caps_get_minimum_free_size(int caps) { return 512 * 1024; }
static size_t heap_caps_get_largest_free_block(int caps) { return 256 * 1024; }
static QueueHandle_t xQueueCreateStatic(unsigned count, size_t size, uint8_t *storage, StaticQueue_t *control) { return (void *)1; }
static unsigned uxQueueMessagesWaiting(QueueHandle_t queue) { return 0; }
static void vTaskDelay(unsigned ticks) {}
typedef void *i2s_chan_handle_t;
typedef struct { int unused; } i2s_chan_config_t;
typedef struct { int unused; } mock_i2s_clock_t;
typedef struct { int unused; } mock_i2s_slots_t;
typedef struct {
    mock_i2s_clock_t clk_cfg;
    mock_i2s_slots_t slot_cfg;
    struct { int mclk, bclk, ws, dout, din; } gpio_cfg;
} i2s_std_config_t;
enum { I2S_NUM_1, I2S_ROLE_MASTER, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_GPIO_UNUSED };
#define I2S_CHANNEL_DEFAULT_CONFIG(port, role) ((i2s_chan_config_t){0})
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) ((mock_i2s_clock_t){0})
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode) ((mock_i2s_slots_t){0})
static esp_err_t i2s_new_channel(const i2s_chan_config_t *config, i2s_chan_handle_t *tx, i2s_chan_handle_t *rx) { *tx = (void *)1; return ESP_OK; }
static esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t channel, const i2s_std_config_t *config) { return ESP_OK; }
static esp_err_t i2s_channel_enable(i2s_chan_handle_t channel) { return ESP_OK; }
static esp_err_t i2s_channel_disable(i2s_chan_handle_t channel) { return ESP_OK; }
static esp_err_t i2s_del_channel(i2s_chan_handle_t channel) { return ESP_OK; }
static esp_err_t i2s_channel_write(i2s_chan_handle_t channel, const void *data, size_t bytes, size_t *written, unsigned timeout) { *written = bytes; return ESP_OK; }
typedef void *esp_websocket_client_handle_t;
typedef struct {
    const char *uri, *headers;
    esp_err_t (*crt_bundle_attach)(void *);
    bool disable_auto_reconnect;
    int buffer_size, task_stack, task_prio, network_timeout_ms;
} esp_websocket_client_config_t;
typedef struct {
    struct { int esp_ws_handshake_status_code, esp_tls_stack_err; } error_handle;
    int op_code, data_len, payload_offset, payload_len;
    bool fin;
    const char *data_ptr;
} esp_websocket_event_data_t;
enum { WEBSOCKET_EVENT_CONNECTED, WEBSOCKET_EVENT_ERROR, WEBSOCKET_EVENT_DISCONNECTED,
       WEBSOCKET_EVENT_DATA, WEBSOCKET_EVENT_ANY };
static esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config) { return (void *)1; }
static esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client, int id,
    void (*callback)(void *, esp_event_base_t, int32_t, void *), void *arg) { return ESP_OK; }
static esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client) { return ESP_OK; }
static esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client) { return ESP_OK; }
static esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client) { return ESP_OK; }
static int esp_websocket_client_send_text(esp_websocket_client_handle_t client, const char *text, size_t length, int timeout) { return (int)length; }
static int mbedtls_base64_decode(unsigned char *out, size_t capacity, size_t *length, const unsigned char *input, size_t count) {
    if (capacity < 4) return -1;
    memset(out, 0, 4); *length = 4; return 0;
}
static int mbedtls_base64_encode(unsigned char *out, size_t capacity, size_t *length, const unsigned char *input, size_t count) {
    *length = 0; return 0;
}
