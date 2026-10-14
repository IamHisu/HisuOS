#include "hisu_media.h"
#include "hisu_media_model.h"
#include "hisu_setup_api.h"
#include "hisu_setup_portal.h"
#include "hisu_setup_ports.h"
#include "hisu_network.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

extern const uint8_t media_html_start[] asm("_binary_index_html_start");
extern const uint8_t media_html_end[] asm("_binary_index_html_end");
extern const uint8_t media_js_start[] asm("_binary_app_js_start");
extern const uint8_t media_js_end[] asm("_binary_app_js_end");
extern const uint8_t media_gif_js_start[] asm("_binary_gif_resize_js_start");
extern const uint8_t media_gif_js_end[] asm("_binary_gif_resize_js_end");
extern const uint8_t media_css_start[] asm("_binary_style_css_start");
extern const uint8_t media_css_end[] asm("_binary_style_css_end");

enum {CMD_OPEN = 1, CMD_DEFAULT};
typedef struct { unsigned value; uint32_t generation; } media_command_t;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_commands;
static const esp_partition_t *s_partition;
static httpd_handle_t s_server;
static hisu_media_status_t s_status;
static uint8_t *s_active, *s_pending;
static size_t s_pending_size;
static bool s_pending_gif;
static uint32_t s_sequence, s_generation;
static int s_slot = -1;
static int64_t s_web_deadline, s_leave_deadline, s_disconnected_at;
static bool s_client_seen;
static uint32_t s_page;
static char s_token[65];
static bool s_claimed;
static bool s_setup_requested;

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
static bool window_valid(void)
{
    return s_status.enabled && !hisu_media_session_should_close(esp_timer_get_time(),
        s_web_deadline,s_leave_deadline,s_disconnected_at);
}
static bool portal_active(void) { lock(); bool active = window_valid(); unlock(); return active; }

static void invalidate(void)
{
    s_status.enabled = false;
    s_status.opening = false;
    s_web_deadline = s_leave_deadline = s_disconnected_at = 0;
    s_client_seen = false;
    memset(s_token, 0, sizeof(s_token));
    ++s_generation;
    free(s_pending); s_pending = NULL; s_status.pending = false;
}

void hisu_media_get(hisu_media_status_t *out)
{
    if (!out) return;
    *out = (hisu_media_status_t){0};
    if (!s_lock) return;
    lock();
    *out = s_status;
    unlock();
}

bool hisu_media_copy_image(void *pixels, size_t size, hisu_media_status_t *status)
{
    if (!s_lock || !pixels || size != HISU_MEDIA_BUFFER_BYTES || !status) return false;
    lock();
    bool available = s_active != NULL;
    if (available) { memset(pixels,0,size); memcpy(pixels, s_active, s_status.image_bytes); }
    *status = s_status;
    unlock();
    return available;
}

static esp_err_t command(unsigned value)
{
    if (!s_commands) return ESP_ERR_INVALID_STATE;
    media_command_t request = {.value=value};
    lock();
    if (value == CMD_OPEN && (s_status.opening || s_status.enabled)) { unlock(); return ESP_OK; }
    request.generation = s_generation;
    if (value == CMD_OPEN) s_status.opening = true;
    unlock();
    if (xQueueSend(s_commands, &request, 0) == pdTRUE) return ESP_OK;
    lock();
    if (value == CMD_OPEN && request.generation == s_generation) s_status.opening = false;
    unlock();
    return ESP_ERR_TIMEOUT;
}
esp_err_t hisu_media_open(void) { return command(CMD_OPEN); }
esp_err_t hisu_media_restore_default(void) { return command(CMD_DEFAULT); }
void hisu_media_close(void)
{
    if (!s_lock) return;
    lock(); invalidate(); unlock();
}

static void response_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
        "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' blob: data:; "
        "connect-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'");
}

static esp_err_t fail(httpd_req_t *req, const char *status, const char *message)
{
    response_headers(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t err = httpd_resp_sendstr(req, message);
    httpd_sess_trigger_close(req->handle,httpd_req_to_sockfd(req));
    return err;
}

/* Numeric Host + exact Origin prevent DNS rebinding/CSRF. No CORS or proxy trust. */
static bool request_allowed(httpd_req_t *req)
{
    lock(); bool enabled = window_valid(); unlock();
    if (!enabled) return false;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t info;
    uint32_t peer, local;
    int fd = httpd_req_to_sockfd(req);
    if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK || !info.ip.addr || !info.netmask.addr ||
        !hisu_setup_socket_ipv4(fd,true,&peer) ||
        (peer & info.netmask.addr) != (info.ip.addr & info.netmask.addr) ||
        !hisu_setup_peer_is_associated(peer)) return false;
    if (!hisu_setup_socket_ipv4(fd,false,&local) || local != info.ip.addr) return false;
    char ip[16], actual[64];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
    if (httpd_req_get_hdr_value_str(req, "Host", actual, sizeof(actual)) != ESP_OK ||
        !hisu_media_http_host_valid(actual,ip)) return false;
    if (req->method == HTTP_POST) {
        if (httpd_req_get_hdr_value_str(req, "Origin", actual, sizeof(actual)) != ESP_OK ||
            !hisu_media_http_origin_valid(actual,ip)) return false;
    }
    return true;
}

static bool authorized(httpd_req_t *req)
{
    char header[72];
    if (httpd_req_get_hdr_value_len(req, "Authorization") != 71 ||
        httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK ||
        memcmp(header, "Bearer ", 7)) return false;
    lock();
    bool valid = window_valid() && s_claimed && hisu_media_secret_equal(header + 7, s_token, 64);
    if (valid) s_web_deadline = esp_timer_get_time() + 90000000;
    unlock();
    memset(header, 0, sizeof(header));
    return valid;
}

static esp_err_t asset(httpd_req_t *req)
{
    if (!request_allowed(req)) {
        if (!strcmp(req->uri,"/")) return hisu_setup_portal_redirect(req);
        return fail(req, "403 Forbidden", "Setting closed or invalid local address.");
    }
    response_headers(req);
    const uint8_t *start = media_html_start, *end = media_html_end;
    const char *type = "text/html; charset=utf-8";
    if (!strcmp(req->uri, "/app.js")) { start = media_js_start; end = media_js_end; type = "text/javascript; charset=utf-8"; }
    if (!strcmp(req->uri, "/gif-resize.js")) { start = media_gif_js_start; end = media_gif_js_end; type = "text/javascript; charset=utf-8"; }
    if (!strcmp(req->uri, "/style.css")) { start = media_css_start; end = media_css_end; type = "text/css; charset=utf-8"; }
    httpd_resp_set_type(req, type);
    /* Release asset sockets promptly; phones also open captive-portal probes. */
    httpd_resp_set_hdr(req, "Connection", "close");
    size_t length = end - start, sent = 0;
    int64_t started = esp_timer_get_time();
    ESP_LOGI("HisuSetting", "GET %s: %u bytes, internal free=%u largest=%u",
        req->uri, (unsigned)length,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    esp_err_t err = ESP_OK;
    while (sent < length) {
        size_t chunk = length - sent;
        if (chunk > 1024) chunk = 1024;
        err = httpd_resp_send_chunk(req, (const char *)start + sent, chunk);
        if (err != ESP_OK) break;
        sent += chunk;
    }
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW("HisuSetting", "GET %s failed: %u/%u bytes after %" PRId64 " ms (%s)",
            req->uri, (unsigned)sent, (unsigned)length,
            (esp_timer_get_time() - started) / 1000, esp_err_to_name(err));
    }
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
    return err;
}

static bool setup_authorized(httpd_req_t *req) { return request_allowed(req) && authorized(req); }

static esp_err_t login(httpd_req_t *req)
{
    if (!request_allowed(req)) return fail(req, "403 Forbidden", "Setting closed or invalid origin.");
    if (req->content_len != 0) return fail(req, "400 Bad Request", "Empty session request required.");
    lock();
    bool valid = window_valid();
    char json[128] = {0};
    if (valid) {
        s_leave_deadline = 0;
        s_web_deadline = esp_timer_get_time() + 90000000;
        /* Hotspot access is the credential; the session token never enters a QR. */
        if (!s_claimed) {
            uint8_t random[32]; esp_fill_random(random, sizeof(random));
            for (unsigned i = 0; i < sizeof(random); ++i) snprintf(s_token + i * 2, 3, "%02x", random[i]);
            memset(random, 0, sizeof(random));
            s_claimed = true;
        }
        if (++s_page == 0) ++s_page;
        snprintf(json, sizeof(json), "{\"token\":\"%s\",\"page\":%" PRIu32 "}", s_token,s_page);
    }
    unlock();
    if (!valid) return fail(req, "403 Forbidden", "Setting expired.");
    response_headers(req); httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    memset(json, 0, sizeof(json));
    return err;
}

static esp_err_t upload(httpd_req_t *req)
{
    if (!request_allowed(req) || !authorized(req)) return fail(req, "403 Forbidden", "Session invalid or expired.");
    char type[48];
    if (httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type)) != ESP_OK)
        return fail(req,"400 Bad Request","Missing image type.");
    bool gif = !strcmp(type,"image/gif");
    size_t length = req->content_len;
    if ((gif && (length < 14 || length > HISU_MEDIA_GIF_MAX_BYTES)) ||
        (!gif && (length != HISU_MEDIA_BYTES || strcmp(type,"application/octet-stream"))))
        return fail(req,"400 Bad Request","Expected RGB565 or GIF up to 1 MB and at most 240x240.");
    lock();
    bool busy = s_status.pending || s_status.receiving || s_status.saving;
    uint32_t generation = s_generation;
    if (!busy) s_status.receiving = true;
    unlock();
    if (busy) return fail(req, "409 Conflict", "Image storage is busy.");
    uint8_t *pixels = heap_caps_malloc(HISU_MEDIA_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t received = 0;
    int64_t timeout = esp_timer_get_time() + 30000000;
    while (pixels && received < length && esp_timer_get_time() < timeout) {
        lock(); bool valid = window_valid() && generation == s_generation; unlock();
        if (!valid) break;
        size_t chunk = length - received;
        if (chunk > 2048) chunk = 2048;
        int count = httpd_req_recv(req, (char *)pixels + received, chunk);
        if (count <= 0) break;
        received += count;
    }
    bool valid_gif = !gif || (pixels && received == length && hisu_media_gif_valid(pixels,length));
    lock();
    bool accepted = pixels && received == length && valid_gif && window_valid() && generation == s_generation;
    s_status.receiving = false;
    if (accepted) { s_pending = pixels; s_pending_size = length; s_pending_gif = gif; s_status.pending = true; s_status.error = ESP_OK; }
    unlock();
    if (!accepted) {
        free(pixels);
        return fail(req,valid_gif ? "408 Request Timeout" : "400 Bad Request",
                    valid_gif ? "Upload incomplete, unavailable memory, or session expired." : "Invalid GIF; expected at most 240x240, complete blocks, at most 256 frames.");
    }
    response_headers(req); httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"saving\":true}");
}

static esp_err_t status(httpd_req_t *req)
{
    if (!request_allowed(req) || !authorized(req)) return fail(req, "403 Forbidden", "Session expired.");
    hisu_media_status_t state; hisu_media_get(&state);
    char json[160];
    snprintf(json, sizeof(json), "{\"pending\":%s,\"saving\":%s,\"revision\":%" PRIu32 ",\"error\":%d}",
             state.pending ? "true" : "false", state.saving ? "true" : "false", state.revision, state.error);
    response_headers(req); httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t leave(httpd_req_t *req)
{
    if (!request_allowed(req) || !authorized(req)) return fail(req,"403 Forbidden","Session closed.");
    if (req->content_len) return fail(req,"400 Bad Request","Empty leave request required.");
    char page[11], expected[11];
    if (httpd_req_get_hdr_value_str(req,"X-Hisu-Page",page,sizeof(page)) != ESP_OK)
        return fail(req,"400 Bad Request","Page identifier required.");
    lock();
    snprintf(expected,sizeof(expected),"%" PRIu32,s_page);
    /* A delayed unload from an old page must not close a reloaded page. */
    if (!strcmp(page,expected) && !s_leave_deadline) s_leave_deadline = esp_timer_get_time() + 8000000;
    unlock();
    response_headers(req); httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,"{\"closing\":true}");
}

static esp_err_t logout(httpd_req_t *req)
{
    if (!request_allowed(req) || !authorized(req)) return fail(req,"403 Forbidden","Session expired.");
    hisu_media_close();
    response_headers(req); httpd_resp_set_type(req,"application/json");
    return httpd_resp_sendstr(req,"{\"closed\":true}");
}

static esp_err_t server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HISU_SETUP_HTTP_PORT;
    config.ctrl_port = HISU_SETUP_HTTP_CTRL_PORT;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 26;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 10;
    config.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri="/", .method=HTTP_GET, .handler=asset},
        {.uri="/app.js", .method=HTTP_GET, .handler=asset},
        {.uri="/gif-resize.js", .method=HTTP_GET, .handler=asset},
        {.uri="/style.css", .method=HTTP_GET, .handler=asset},
        {.uri="/session", .method=HTTP_POST, .handler=login},
        {.uri="/image", .method=HTTP_POST, .handler=upload},
        {.uri="/status", .method=HTTP_GET, .handler=status},
        {.uri="/logout", .method=HTTP_POST, .handler=logout},
        {.uri="/leave", .method=HTTP_POST, .handler=leave},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(s_server, &routes[i]);
        if (err != ESP_OK) { httpd_stop(s_server); s_server = NULL; return err; }
    }
    err = hisu_setup_api_register(s_server, setup_authorized, response_headers);
    if (err == ESP_OK) err = hisu_setup_portal_register(s_server);
    if (err != ESP_OK) { httpd_stop(s_server); s_server = NULL; }
    return err;
}

/* Write payload first and CRC-checked header last in the inactive slot. */
static esp_err_t persist(uint8_t *pixels, size_t length, bool gif)
{
    if (s_sequence == UINT32_MAX) return ESP_ERR_INVALID_STATE;
    int slot = s_slot == 0 ? 1 : 0;
    size_t offset = slot * HISU_MEDIA_SLOT_BYTES;
    hisu_media_record_t record = {
        .magic = HISU_MEDIA_MAGIC, .version = 1, .sequence = s_sequence + 1,
        .width = 240, .height = 240, .length = pixels ? length : 0,
        .crc = pixels ? hisu_media_crc(pixels, length) : 0, .flags = pixels ? (gif ? 2 : 1) : 0,
    };
    record.header_crc = hisu_media_crc(&record, offsetof(hisu_media_record_t, header_crc));
    esp_err_t err = esp_partition_erase_range(s_partition, offset, HISU_MEDIA_SLOT_BYTES);
    if (err == ESP_OK && pixels) err = esp_partition_write(s_partition, offset + HISU_MEDIA_DATA_OFFSET, pixels, length);
    if (err == ESP_OK && pixels) {
        uint8_t chunk[1024];
        uint32_t crc = 0;
        for (size_t pos = 0; pos < length && err == ESP_OK; pos += sizeof(chunk)) {
            size_t size = length - pos;
            if (size > sizeof(chunk)) size = sizeof(chunk);
            err = esp_partition_read(s_partition,offset+HISU_MEDIA_DATA_OFFSET+pos,chunk,size);
            if (err == ESP_OK) crc = hisu_media_crc_update(crc,chunk,size);
        }
        if (err == ESP_OK && crc != record.crc) err = ESP_ERR_INVALID_CRC;
    }
    if (err == ESP_OK) err = esp_partition_write(s_partition, offset, &record, sizeof(record));
    if (err == ESP_OK) {
        hisu_media_record_t verified;
        err = esp_partition_read(s_partition,offset,&verified,sizeof(verified));
        if (err == ESP_OK && memcmp(&verified,&record,sizeof(record))) err = ESP_ERR_INVALID_CRC;
    }
    if (err == ESP_OK) { s_slot = slot; s_sequence = record.sequence; }
    return err;
}

static esp_err_t load_image(void)
{
    /* Also inspect the former second slot so an upgrade keeps the latest
     * 115200-byte wallpaper. The first new write goes to the enlarged slot 1,
     * leaving both legacy records intact until the new header is committed. */
    const size_t offsets[] = {0, HISU_MEDIA_LEGACY_SLOT_BYTES, HISU_MEDIA_SLOT_BYTES};
    const int slots[] = {0, 0, 1};
    for (unsigned index = 0; index < sizeof(offsets)/sizeof(offsets[0]); ++index) {
        hisu_media_record_t record;
        size_t offset = offsets[index];
        esp_err_t err = esp_partition_read(s_partition, offset, &record, sizeof(record));
        if (err != ESP_OK) return err;
        if (!hisu_media_record_header_valid(&record) || record.sequence <= s_sequence) continue;
        uint8_t *pixels = NULL;
        if (record.flags) {
            pixels = heap_caps_malloc(HISU_MEDIA_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!pixels) return ESP_ERR_NO_MEM;
            err = esp_partition_read(s_partition, offset + HISU_MEDIA_DATA_OFFSET, pixels, record.length);
            if (err != ESP_OK) { free(pixels); return err; }
            if (!hisu_media_record_valid(&record, pixels)) { free(pixels); continue; }
        }
        free(s_active); s_active = pixels;
        s_status.image_bytes = record.length; s_status.gif = record.flags == 2;
        s_slot = slots[index]; s_sequence = record.sequence;
    }
    s_status.has_image = s_active != NULL;
    s_status.revision = s_sequence;
    return ESP_OK;
}

static void media_task(void *arg)
{
    (void)arg;
    for (;;) {
        media_command_t request = {0};
        xQueueReceive(s_commands, &request, pdMS_TO_TICKS(200));
        unsigned cmd = request.value;
        bool save_default = false;
        if (cmd == CMD_OPEN) {
            lock(); bool current = request.generation == s_generation; unlock();
            if (!current) continue;
            hisu_network_status_t network; hisu_network_get(&network);
            lock(); bool busy = s_status.receiving || s_status.saving; unlock();
            if (!busy) {
                lock(); invalidate(); s_status.opening = true; s_claimed = false; s_status.error = ESP_OK;
                uint32_t generation = s_generation;
                unlock();
                /* Finish closing the old AP before opening a fresh random-password window. */
                hisu_setup_portal_stop();
                if (s_server) { httpd_stop(s_server); s_server = NULL; }
                esp_err_t err = hisu_network_setup(false);
                int64_t closed_until = esp_timer_get_time() + 15000000;
                while (err == ESP_OK) {
                    hisu_network_get(&network);
                    if (!network.setup_enabled) break;
                    if (esp_timer_get_time() >= closed_until) { err = ESP_ERR_TIMEOUT; break; }
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                if (err == ESP_OK) err = hisu_network_setup(true);
                if (err == ESP_OK) {
                    s_setup_requested = true;
                    int64_t until = esp_timer_get_time() + 15000000;
                    do {
                        hisu_network_get(&network);
                        if (network.setup_enabled) break;
                        vTaskDelay(pdMS_TO_TICKS(50));
                    } while (esp_timer_get_time() < until);
                    if (!network.setup_enabled) err = ESP_ERR_TIMEOUT;
                }
                if (err == ESP_OK) err = s_server ? ESP_OK : server_start();
                if (err == ESP_OK) err = hisu_setup_portal_start(portal_active);
                if (err != ESP_OK) ESP_LOGE("HisuSetting", "Startup failed: %s (0x%x)",
                    esp_err_to_name(err), (unsigned)err);
                lock();
                s_status.error = err;
                if (generation == s_generation) s_status.opening = false;
                if (err == ESP_OK && generation == s_generation) {
                    s_status.enabled = true;
                }
                unlock();
            } else { lock(); s_status.opening = false; s_status.error = ESP_ERR_INVALID_STATE; unlock(); }
        } else if (cmd == CMD_DEFAULT) {
            lock();
            bool valid = request.generation == s_generation && !s_status.receiving && !s_status.saving;
            if (valid) {
                free(s_pending); s_pending = NULL;
                s_status.pending = false; s_status.saving = true;
                save_default = true;
            }
            unlock();
        }
        /* Take ownership under the lock; closing a window cannot free this buffer. */
        lock();
        uint8_t *pixels = NULL;
        size_t image_bytes = 0; bool gif = false;
        bool save = save_default;
        if (window_valid() && s_pending && !s_status.saving && !s_status.receiving) {
            pixels = s_pending; s_pending = NULL;
            image_bytes = s_pending_size; gif = s_pending_gif;
            s_status.pending = false; s_status.saving = true;
            save = true;
        }
        unlock();
        if (save) {
            esp_err_t err = persist(pixels,image_bytes,gif);
            lock();
            s_status.error = err; s_status.saving = false;
            if (err == ESP_OK) {
                free(s_active); s_active = pixels; pixels = NULL;
                s_status.image_bytes = image_bytes; s_status.gif = gif;
                s_status.has_image = s_active != NULL; s_status.revision = s_sequence;
            }
            unlock();
            free(pixels);
        }
        hisu_network_status_t network; hisu_network_get(&network);
        lock();
        if (s_status.enabled) {
            if (network.setup_client_connected) {
                s_client_seen = true;
                s_disconnected_at = 0;
            } else if (s_client_seen && !s_disconnected_at) s_disconnected_at = esp_timer_get_time();
        }
        if (s_status.enabled && (!window_valid() || !network.setup_enabled)) invalidate();
        bool enabled = s_status.enabled;
        unlock();
        if (!enabled) hisu_setup_portal_stop();
        if (!enabled && s_server) { httpd_stop(s_server); s_server = NULL; }
        if (!enabled && s_setup_requested && hisu_network_setup(false) == ESP_OK && !network.setup_enabled)
            s_setup_requested = false;
    }
}

esp_err_t hisu_media_start(void)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;
    s_sequence = 0; s_slot = -1; s_status = (hisu_media_status_t){0};
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "storage");
    if (!s_partition || s_partition->size < 2 * HISU_MEDIA_SLOT_BYTES) return ESP_ERR_NOT_FOUND;
    s_lock = xSemaphoreCreateMutex();
    s_commands = xQueueCreate(4, sizeof(media_command_t));
    if (!s_lock || !s_commands) {
        if (s_lock) vSemaphoreDelete(s_lock);
        if (s_commands) vQueueDelete(s_commands);
        s_lock = NULL; s_commands = NULL;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = load_image();
    if (err == ESP_OK && xTaskCreate(media_task, "hisu_media", 12288, NULL, 3, NULL) != pdPASS) err = ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        free(s_active); s_active = NULL;
        vSemaphoreDelete(s_lock); vQueueDelete(s_commands); s_lock = NULL; s_commands = NULL;
        return err;
    }
    lock(); s_status.ready = true; unlock();
    return ESP_OK;
}
