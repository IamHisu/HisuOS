#pragma once
#include "network_mock.h"
#include <stdlib.h>
#include <assert.h>
#include <time.h>
static struct tm *mock_gmtime_r(const time_t *value,struct tm *out) { return gmtime_s(out,value)==0 ? out : NULL; }
#define gmtime_r mock_gmtime_r
enum { ESP_FAIL = -1, ESP_ERR_NOT_FOUND = 100 };

typedef struct { char version[32], project_name[32], idf_ver[32]; } esp_app_desc_t;
static const esp_app_desc_t *esp_app_get_description(void) {
    static const esp_app_desc_t app = {"test-version", "HisuOS", "test-idf"};
    return &app;
}
static unsigned mock_reboots;
static void esp_restart(void) { ++mock_reboots; }
typedef int portMUX_TYPE;
typedef int StaticQueue_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define ESP_LOGI(...) ((void)0)
enum { MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_INTERNAL = 4, MALLOC_CAP_DMA = 8 };
static int mock_live_alloc_left = -1;
static bool mock_live_allocate(void) {
    if (mock_live_alloc_left==0) return false;
    if (mock_live_alloc_left>0) --mock_live_alloc_left;
    return true;
}
static void *heap_caps_malloc(size_t size, int caps) { return mock_live_allocate() ? malloc(size) : NULL; }
static void *heap_caps_calloc(size_t count, size_t size, int caps) { return mock_live_allocate() ? calloc(count,size) : NULL; }
static size_t heap_caps_get_free_size(int caps) { return 1024 * 1024; }
static size_t heap_caps_get_minimum_free_size(int caps) { return 512 * 1024; }
static size_t heap_caps_get_largest_free_block(int caps) { return 256 * 1024; }
static QueueHandle_t xQueueCreateStatic(unsigned count, size_t size, uint8_t *storage, StaticQueue_t *control) { return (void *)1; }
static unsigned uxQueueMessagesWaiting(QueueHandle_t queue) { return 0; }
static int (*mock_queue_peek)(QueueHandle_t queue, void *data);
static int xQueuePeek(QueueHandle_t queue, void *data, int timeout) { return mock_queue_peek ? mock_queue_peek(queue, data) : 0; }
static void (*mock_delay_hook)(void);
static unsigned uxTaskGetStackHighWaterMark(void *task) { return 4096; }
static void vTaskDelay(unsigned ticks) {
    mock_now+=(int64_t)ticks*10000;
    if (mock_delay_hook) {
        void (*hook)(void)=mock_delay_hook;
        mock_delay_hook=NULL; hook();
    }
}
typedef void *i2s_chan_handle_t;
typedef struct { bool auto_clear_after_cb; unsigned dma_desc_num, dma_frame_num; } i2s_chan_config_t;
typedef struct { int unused; } mock_i2s_clock_t;
typedef mock_i2s_clock_t i2s_std_clk_config_t;
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
static i2s_chan_config_t mock_i2s_channel_config;
static esp_err_t mock_i2s_init_error;
static unsigned mock_i2s_deleted;
static unsigned mock_i2s_created, mock_i2s_preloaded, mock_i2s_reconfigured;
static esp_err_t mock_i2s_clock_error;
static esp_err_t mock_i2s_enable_error;
static esp_err_t i2s_new_channel(const i2s_chan_config_t *config, i2s_chan_handle_t *tx, i2s_chan_handle_t *rx) {
    mock_i2s_channel_config = *config;
    ++mock_i2s_created;
    *tx = (void *)1; return ESP_OK;
}
static esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t channel, const i2s_std_config_t *config) { return mock_i2s_init_error; }
static esp_err_t i2s_channel_enable(i2s_chan_handle_t channel) { return mock_i2s_enable_error; }
static esp_err_t i2s_channel_reconfig_std_clock(i2s_chan_handle_t channel,const i2s_std_clk_config_t *clock) {
    ++mock_i2s_reconfigured; return mock_i2s_clock_error;
}
static esp_err_t i2s_channel_preload_data(i2s_chan_handle_t channel,const void *src,size_t bytes,size_t *loaded) {
    assert(bytes==2560);
    const uint8_t *data=src;
    for (size_t i=0;i<bytes;++i) assert(!data[i]);
    ++mock_i2s_preloaded; *loaded=bytes; return ESP_OK;
}
static esp_err_t i2s_channel_disable(i2s_chan_handle_t channel) { return ESP_OK; }
static esp_err_t i2s_del_channel(i2s_chan_handle_t channel) { ++mock_i2s_deleted; return ESP_OK; }
static unsigned mock_i2s_write_timeout;
static bool mock_i2s_short_write;
static size_t mock_i2s_written_bytes;
static void (*mock_i2s_write_hook)(void);
static int16_t mock_i2s_first_sample;
static esp_err_t i2s_channel_write(i2s_chan_handle_t channel, const void *data, size_t bytes, size_t *written, unsigned timeout) {
    mock_i2s_write_timeout = timeout;
    *written = mock_i2s_short_write ? bytes / 2 : bytes;
    mock_i2s_written_bytes+=*written;
    if (bytes>=2) mock_i2s_first_sample=((const int16_t *)data)[0];
    if (mock_i2s_write_hook) { void (*hook)(void)=mock_i2s_write_hook; mock_i2s_write_hook=NULL; hook(); }
    return ESP_OK;
}
typedef void *esp_websocket_client_handle_t;
typedef struct mock_transport {
    struct mock_transport *parent;
    bool bundle, control;
    int port;
    char path[160], headers[160];
} *esp_transport_handle_t;
typedef struct { const char *ws_path, *headers; bool propagate_control_frames; } esp_transport_ws_config_t;
static int mock_transport_failure, mock_transport_alive, mock_nodelay_calls, mock_nodelay_result;
static esp_transport_handle_t esp_transport_ssl_init(void) {
    if (mock_transport_failure == 1) return NULL;
    ++mock_transport_alive; return calloc(1,sizeof(struct mock_transport));
}
static esp_transport_handle_t esp_transport_ws_init(esp_transport_handle_t parent) {
    if (mock_transport_failure == 2) return NULL;
    esp_transport_handle_t ws=calloc(1,sizeof(struct mock_transport));
    ws->parent=parent; ++mock_transport_alive; return ws;
}
static void esp_transport_ssl_crt_bundle_attach(esp_transport_handle_t tls, esp_err_t (*bundle)(void *)) {
    tls->bundle = bundle == esp_crt_bundle_attach;
}
static void esp_transport_set_default_port(esp_transport_handle_t t, int port) { t->port=port; }
static esp_err_t esp_transport_ws_set_config(esp_transport_handle_t t, const esp_transport_ws_config_t *config) {
    if (mock_transport_failure == 3) return ESP_ERR_NO_MEM;
    assert(strlen(config->ws_path)<sizeof(t->path) && strlen(config->headers)<sizeof(t->headers));
    memcpy(t->path,config->ws_path,strlen(config->ws_path)+1);
    memcpy(t->headers,config->headers,strlen(config->headers)+1);
    t->control=config->propagate_control_frames; return ESP_OK;
}
static int esp_transport_destroy(esp_transport_handle_t t) { --mock_transport_alive; free(t); return 0; }
static int esp_transport_get_socket(esp_transport_handle_t t) { return 7; }
enum { IPPROTO_TCP=6, TCP_NODELAY=1 };
static int setsockopt(int socket, int protocol, int option, const void *value, size_t size) {
    assert(socket==7 && protocol==IPPROTO_TCP && option==TCP_NODELAY);
    assert(size==sizeof(int) && *(const int *)value==1);
    ++mock_nodelay_calls; return mock_nodelay_result;
}
typedef struct {
    const char *uri, *headers;
    esp_err_t (*crt_bundle_attach)(void *);
    bool disable_auto_reconnect;
    int buffer_size, task_stack, task_prio, network_timeout_ms;
    esp_transport_handle_t ext_transport;
} esp_websocket_client_config_t;
enum { WEBSOCKET_ERROR_TYPE_NONE=0, WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT=1 };
typedef struct {
    struct { int esp_ws_handshake_status_code, esp_tls_stack_err, error_type,
        esp_tls_last_esp_err, esp_tls_cert_verify_flags, esp_transport_sock_errno; } error_handle;
    int op_code, data_len, payload_offset, payload_len;
    bool fin;
    const char *data_ptr;
} esp_websocket_event_data_t;
enum { WEBSOCKET_EVENT_CONNECTED, WEBSOCKET_EVENT_ERROR, WEBSOCKET_EVENT_DISCONNECTED,
       WEBSOCKET_EVENT_DATA, WEBSOCKET_EVENT_ANY };
static bool mock_client_init_failure;
static esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config) {
    return mock_client_init_failure ? NULL : (void *)1;
}
static esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client, int id,
    void (*callback)(void *, esp_event_base_t, int32_t, void *), void *arg) { return ESP_OK; }
static esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client) { return ESP_OK; }
static esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client) { return ESP_OK; }
static esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client) { return ESP_OK; }
static bool mock_send_failure;
static bool mock_short_send;
static int mock_send_timeout;
static unsigned mock_text_sends;
static void (*mock_on_send)(void);
static char mock_last_text[16384];
static int esp_websocket_client_send_text(esp_websocket_client_handle_t client, const char *text, size_t length, int timeout) {
    mock_send_timeout=timeout;
    ++mock_text_sends;
    size_t copy = length < sizeof(mock_last_text) - 1 ? length : sizeof(mock_last_text) - 1;
    memcpy(mock_last_text,text,copy); mock_last_text[copy]=0;
    if (mock_on_send) {
        void (*callback)(void)=mock_on_send;
        mock_on_send=NULL; callback();
    }
    return mock_send_failure ? -1 : (int)length - (mock_short_send ? 1 : 0);
}
static int mbedtls_base64_decode(unsigned char *out, size_t capacity, size_t *length, const unsigned char *input, size_t count) {
    if (capacity < 4) return -1;
    memset(out, 0, 4); *length = 4; return 0;
}
static int mbedtls_base64_encode(unsigned char *out, size_t capacity, size_t *length, const unsigned char *input, size_t count) {
    *length = 0; return 0;
}
