#include "friend_http.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include "friend_wifi.h"
#include "friend_wifi_store.h"
#include "friend_media.h"
#include "friend_media_store.h"
#include "friend_ai.h"

static const char *TAG = "HTTP";

static httpd_handle_t server = NULL;

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

extern const char style_css_start[] asm("_binary_style_css_start");
extern const char style_css_end[] asm("_binary_style_css_end");

extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");
extern const char home_html_start[] asm("_binary_home_html_start");
extern const char home_html_end[] asm("_binary_home_html_end");
extern const char home_js_start[] asm("_binary_home_js_start");
extern const char home_js_end[] asm("_binary_home_js_end");
extern const char gif_resize_js_start[] asm("_binary_gif_resize_js_start");
extern const char gif_resize_js_end[] asm("_binary_gif_resize_js_end");

static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t style_css_handler(httpd_req_t *req);
static esp_err_t app_js_handler(httpd_req_t *req);
static esp_err_t wifi_scan_handler(httpd_req_t *req);
static esp_err_t wifi_connect_handler(httpd_req_t *req);
static esp_err_t wifi_forget_handler(httpd_req_t *req);
static esp_err_t home_js_handler(httpd_req_t *req);
static esp_err_t gif_resize_js_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);
static esp_err_t media_upload_handler(httpd_req_t *req);
static esp_err_t media_face_handler(httpd_req_t *req);
static esp_err_t ai_key_handler(httpd_req_t *req);
static esp_err_t ai_ask_handler(httpd_req_t *req);
static esp_err_t ai_status_handler(httpd_req_t *req);
static esp_err_t ai_provider_handler(httpd_req_t *req);
static esp_err_t ai_profile_get_handler(httpd_req_t *req);
static esp_err_t ai_profile_save_handler(httpd_req_t *req);

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }

    return -1;
}

static bool url_decode(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || src == NULL || dst_size == 0)
    {
        return false;
    }

    size_t di = 0;

    for (size_t si = 0; src[si] != '\0'; si++)
    {
        if (di + 1 >= dst_size)
        {
            return false;
        }

        if (src[si] == '+')
        {
            dst[di++] = ' ';
            continue;
        }

        if (src[si] == '%')
        {
            if (src[si + 1] == '\0' || src[si + 2] == '\0')
            {
                return false;
            }

            int high = hex_value(src[si + 1]);
            int low = hex_value(src[si + 2]);

            if (high >= 0 && low >= 0 && (high != 0 || low != 0))
            {
                dst[di++] = (char)((high << 4) | low);
                si += 2;
                continue;
            }

            return false;
        }

        dst[di++] = src[si];
    }

    dst[di] = '\0';
    return true;
}

static esp_err_t read_form(httpd_req_t *req, char *body, size_t body_size)
{
    if (req->content_len <= 0 || req->content_len >= body_size)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received = 0;
    unsigned int timeouts = 0;
    while (received < req->content_len)
    {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            if (++timeouts >= 3)
            {
                return ESP_ERR_TIMEOUT;
            }
            continue;
        }

        if (ret <= 0)
        {
            return ESP_FAIL;
        }
        timeouts = 0;
        received += ret;
    }

    body[received] = '\0';
    return ESP_OK;
}

static bool form_ssid(const char *body, char *ssid, size_t ssid_size)
{
    char encoded[FRIEND_WIFI_SSID_MAX_LEN * 3 + 1] = {0};
    return httpd_query_key_value(body, "ssid", encoded, sizeof(encoded)) == ESP_OK &&
        url_decode(ssid, ssid_size, encoded) && ssid[0] != '\0';
}

static bool json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0'; si++)
    {
        unsigned char c = (unsigned char)src[si];
        if (c == '"' || c == '\\')
        {
            if (di + 2 >= dst_size)
            {
                return false;
            }
            dst[di++] = '\\';
            dst[di++] = (char)c;
        }
        else if (c < 0x20)
        {
            if (di + 6 >= dst_size)
            {
                return false;
            }
            snprintf(dst + di, dst_size - di, "\\u%04x", c);
            di += 6;
        }
        else
        {
            if (di + 1 >= dst_size)
            {
                return false;
            }
            dst[di++] = (char)c;
        }
    }

    dst[di] = '\0';
    return true;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /");

    bool setup = friend_wifi_get_state() == FRIEND_WIFI_STATE_MANUAL;
    const char *start = setup ? index_html_start : home_html_start;
    size_t length = setup ? (size_t)(index_html_end - index_html_start) : (size_t)(home_html_end - home_html_start);

    if (length > 0 && start[length - 1] == '\0')
    {
        length--;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, start, length);
}

static esp_err_t home_js_handler(httpd_req_t *req)
{
    size_t length = home_js_end - home_js_start;
    if (length > 0 && home_js_start[length - 1] == '\0') length--;
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, home_js_start, length);
}

static esp_err_t gif_resize_js_handler(httpd_req_t *req)
{
    size_t length = gif_resize_js_end - gif_resize_js_start;
    if (length > 0 && gif_resize_js_start[length - 1] == '\0') length--;
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, gif_resize_js_start, length);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char ip[16] = {0};
    bool online = friend_wifi_get_sta_ip(ip, sizeof(ip)) == ESP_OK;
    char response[96];
    snprintf(response, sizeof(response), "{\"online\":%s,\"ip\":\"%s\",\"display\":\"%s\",\"gifAvailable\":%s}",
        online ? "true" : "false", ip, friend_media_current(), friend_media_gif_available() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

static esp_err_t ai_status_handler(httpd_req_t *req)
{
    static const char *names[] = { "idle", "working", "ready", "showing", "done", "error" };
    friend_ai_state_t state = friend_ai_state();
    if (state < FRIEND_AI_IDLE || state > FRIEND_AI_ERROR) state = FRIEND_AI_ERROR;
    char response[208];
    snprintf(response, sizeof(response),
        "{\"provider\":\"%s\",\"configured\":%s,\"state\":\"%s\",\"error\":\"%s\"}",
        friend_ai_provider_name(), friend_ai_has_key() ? "true" : "false", names[state],
        state == FRIEND_AI_ERROR ? friend_ai_error() : "");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, response);
}

static esp_err_t ai_provider_handler(httpd_req_t *req)
{
    if (friend_wifi_get_state() != FRIEND_WIFI_STATE_ONLINE)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Home is not online");
    }
    char name[8];
    if (read_form(req, name, sizeof(name)) != ESP_OK ||
        (strcmp(name, "gemini") != 0 && strcmp(name, "openai") != 0))
    {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Invalid AI provider");
    }
    esp_err_t err = friend_ai_set_provider(strcmp(name, "openai") == 0 ?
        FRIEND_AI_OPENAI : FRIEND_AI_GEMINI);
    if (err != ESP_OK)
    {
        httpd_resp_set_status(req, err == ESP_ERR_INVALID_STATE ? "409 Conflict" : "500 Internal Server Error");
        return httpd_resp_sendstr(req, err == ESP_ERR_INVALID_STATE ? "AI busy" : "Could not save provider");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"saved\":true}");
}

static esp_err_t ai_profile_get_handler(httpd_req_t *req)
{
    friend_ai_profile_t profile;
    friend_ai_get_profile(&profile);
    cJSON *body = cJSON_CreateObject();
    if (body == NULL || cJSON_AddStringToObject(body, "name", profile.name) == NULL ||
        cJSON_AddStringToObject(body, "personality", profile.personality) == NULL)
    {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not read profile");
        return ESP_FAIL;
    }
    char *response = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (response == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not read profile");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, response);
    cJSON_free(response);
    return err;
}

static esp_err_t ai_profile_save_handler(httpd_req_t *req)
{
    if (friend_wifi_get_state() != FRIEND_WIFI_STATE_ONLINE)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Home is not online");
    }
    char body[2048];
    if (read_form(req, body, sizeof(body)) != ESP_OK ||
        strlen(body) != (size_t)req->content_len)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid AI profile");
        return ESP_FAIL;
    }
    cJSON *root = cJSON_ParseWithLength(body, req->content_len);
    cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *personality = cJSON_GetObjectItemCaseSensitive(root, "personality");
    if (!cJSON_IsString(name) || !cJSON_IsString(personality))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid AI profile");
        return ESP_FAIL;
    }
    friend_ai_profile_t profile = {0};
    size_t name_length = strlen(name->valuestring);
    size_t personality_length = strlen(personality->valuestring);
    if (name_length > FRIEND_AI_NAME_MAX || personality_length > FRIEND_AI_PERSONALITY_MAX)
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "AI profile too long");
        return ESP_FAIL;
    }
    memcpy(profile.name, name->valuestring, name_length + 1);
    memcpy(profile.personality, personality->valuestring, personality_length + 1);
    cJSON_Delete(root);
    esp_err_t err = friend_ai_save_profile(&profile);
    if (err != ESP_OK)
    {
        httpd_resp_set_status(req, err == ESP_ERR_INVALID_STATE ? "409 Conflict" :
            err == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "500 Internal Server Error");
        return httpd_resp_sendstr(req, err == ESP_ERR_INVALID_STATE ? "AI busy" :
            err == ESP_ERR_INVALID_ARG ? "Invalid AI profile" : "Could not save AI profile");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"saved\":true}");
}

static esp_err_t ai_key_handler(httpd_req_t *req)
{
    if (friend_wifi_get_state() != FRIEND_WIFI_STATE_ONLINE)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Home is not online");
    }
    char key[FRIEND_AI_KEY_MAX + 2] = {0};
    esp_err_t err = req->content_len == 0 ? ESP_OK : read_form(req, key, sizeof(key));
    if (err == ESP_OK && strlen(key) != (size_t)req->content_len) err = ESP_ERR_INVALID_ARG;
    if (err == ESP_OK) err = friend_ai_save_key(key);
    memset(key, 0, sizeof(key));
    if (err != ESP_OK)
    {
        httpd_resp_set_status(req, err == ESP_ERR_INVALID_STATE ? "409 Conflict" : "400 Bad Request");
        return httpd_resp_sendstr(req, err == ESP_ERR_INVALID_STATE ? "AI busy" : "Invalid API key");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"saved\":true}");
}

static esp_err_t ai_ask_handler(httpd_req_t *req)
{
    if (friend_wifi_get_state() != FRIEND_WIFI_STATE_ONLINE)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Home is not online");
    }
    if (strcmp(friend_media_current(), "face") != 0)
    {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "AI works only in face mode");
    }
    char question[FRIEND_AI_QUESTION_MAX + 1];
    if (read_form(req, question, sizeof(question)) != ESP_OK)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Invalid question");
    }
    if (strlen(question) != (size_t)req->content_len)
    {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Invalid question");
    }
    if (httpd_req_get_url_query_len(req) != 0)
    {
        memset(question, 0, sizeof(question));
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Unsupported AI option");
    }
    esp_err_t err = friend_ai_submit(question);
    memset(question, 0, sizeof(question));
    if (err != ESP_OK)
    {
        httpd_resp_set_status(req, err == ESP_ERR_NOT_FOUND ? "428 Precondition Required" :
            err == ESP_ERR_INVALID_STATE ? "409 Conflict" : "400 Bad Request");
        return httpd_resp_sendstr(req, err == ESP_ERR_NOT_FOUND ? "Set API key first" :
            err == ESP_ERR_INVALID_STATE ? "AI busy or not in face mode" : "Invalid question");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"queued\":true}");
}

static esp_err_t ai_subtitle_test_handler(httpd_req_t *req)
{
    if (friend_wifi_get_state() != FRIEND_WIFI_STATE_ONLINE)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Home is not online");
    }
    esp_err_t err = friend_ai_test_subtitle();
    if (err != ESP_OK)
    {
        httpd_resp_set_status(req, err == ESP_ERR_INVALID_STATE ? "409 Conflict" : "500 Internal Server Error");
        return httpd_resp_sendstr(req, err == ESP_ERR_INVALID_STATE ?
            "AI busy or not in face mode" : "Could not show subtitle");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"queued\":true}");
}

static esp_err_t media_upload_handler(httpd_req_t *req)
{
    if (friend_wifi_get_state() != FRIEND_WIFI_STATE_ONLINE)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Home is not online");
        return ESP_FAIL;
    }

    bool gif = strcmp(req->uri, "/api/media/gif") == 0;
    if (gif && !friend_media_gif_available())
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "GIF memory unavailable");
    }
    size_t limit = gif ? FRIEND_MEDIA_GIF_MAX_BYTES : FRIEND_MEDIA_STILL_BYTES;
    if (req->content_len <= 0 || req->content_len > limit || (!gif && req->content_len != FRIEND_MEDIA_STILL_BYTES))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid image size");
        return ESP_FAIL;
    }

    uint8_t *data = heap_caps_malloc(req->content_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not enough image memory");
        return ESP_FAIL;
    }

    size_t received = 0;
    unsigned int timeouts = 0;
    while (received < req->content_len)
    {
        int count = httpd_req_recv(req, (char *)data + received, req->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
        if (count <= 0)
        {
            free(data);
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Upload interrupted");
            return ESP_FAIL;
        }
        timeouts = 0;
        received += count;
    }

    if (gif)
    {
        bool signature = received >= 13 && memcmp(data, "GIF87a", 6) == 0;
        signature = signature || (received >= 13 && memcmp(data, "GIF89a", 6) == 0);
        unsigned int width = signature ? (unsigned int)data[6] | ((unsigned int)data[7] << 8) : 0;
        unsigned int height = signature ? (unsigned int)data[8] | ((unsigned int)data[9] << 8) : 0;
        if (!signature || width == 0 || height == 0 || width > FRIEND_MEDIA_WIDTH || height > FRIEND_MEDIA_HEIGHT || data[received - 1] != 0x3b)
        {
            free(data);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "GIF must be valid and at most 240x240");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "GIF upload: %u bytes, %ux%u", (unsigned int)received, width, height);
    }

    friend_media_kind_t kind = gif ? FRIEND_MEDIA_GIF : FRIEND_MEDIA_STILL;
    esp_err_t ret = friend_media_store_prepare(kind, data, received);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not save media: %s", esp_err_to_name(ret));
        free(data);
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req, "Media could not be saved");
    }
    ret = friend_media_submit(kind, data, received);
    if (ret != ESP_OK)
    {
        friend_media_store_abort();
        free(data);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Display is busy");
    }

    ret = friend_media_store_commit();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Media displayed but save commit failed: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req, "Media displayed but not saved");
    }
    ret = friend_media_store_set_image_visible(true);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Media saved but display mode not saved: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req, "Media saved but display mode not saved");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"queued\":true}");
}

static esp_err_t media_face_handler(httpd_req_t *req)
{
    if (friend_wifi_get_state() != FRIEND_WIFI_STATE_ONLINE)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Home is not online");
    }
    if (friend_media_submit(FRIEND_MEDIA_FACE, NULL, 0) != ESP_OK)
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Display is busy");
    }
    esp_err_t ret = friend_media_store_set_image_visible(false);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Face displayed but display mode was not saved: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req, "Face displayed but not saved");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"queued\":true}");
}

static esp_err_t style_css_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /style.css");

    size_t length = style_css_end - style_css_start;

    if (length > 0 && style_css_start[length - 1] == '\0')
    {
        length--;
    }

    httpd_resp_set_type(req, "text/css; charset=utf-8");
    return httpd_resp_send(req, style_css_start, length);
}

static esp_err_t app_js_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /app.js");

    size_t length = app_js_end - app_js_start;

    if (length > 0 && app_js_start[length - 1] == '\0')
    {
        length--;
    }

    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    return httpd_resp_send(req, app_js_start, length);
}

static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    wifi_ap_record_t records[20];
    uint16_t count = 20;

    esp_err_t ret = friend_wifi_get_scan_results(records, &count);

    if (ret != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan failed");
        return ret;
    }

    httpd_resp_set_type(req, "application/json");
    if (httpd_resp_sendstr_chunk(req, "{\"networks\":[") != ESP_OK) return ESP_FAIL;

    uint16_t visible_count = 0;
    for (uint16_t i = 0; i < count; i++)
    {
        records[i].ssid[sizeof(records[i].ssid) - 1] = '\0';
        if (records[i].ssid[0] == '\0') continue;

        bool strongest = true;
        for (uint16_t j = 0; j < count; j++)
        {
            if (j == i) continue;
            records[j].ssid[sizeof(records[j].ssid) - 1] = '\0';
            if (strcmp((const char *)records[i].ssid, (const char *)records[j].ssid) == 0 &&
                (records[j].rssi > records[i].rssi || (records[j].rssi == records[i].rssi && j < i)))
            {
                strongest = false;
                break;
            }
        }
        if (!strongest) continue;

        char escaped[FRIEND_WIFI_SSID_MAX_LEN * 6 + 1];
        if (!json_escape((const char *)records[i].ssid, escaped, sizeof(escaped))) continue;

        char item[280];
        int len = snprintf(item, sizeof(item), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%u,\"saved\":%s}",
            visible_count > 0 ? "," : "", escaped, records[i].rssi, records[i].primary,
            friend_wifi_store_is_saved((const char *)records[i].ssid) ? "true" : "false");
        if (len < 0 || len >= (int)sizeof(item) || httpd_resp_sendstr_chunk(req, item) != ESP_OK) return ESP_FAIL;
        visible_count++;
    }

    if (httpd_resp_sendstr_chunk(req, "]}") != ESP_OK) return ESP_FAIL;
    if (httpd_resp_sendstr_chunk(req, NULL) != ESP_OK) return ESP_FAIL;

    ESP_LOGI(TAG, "GET /api/wifi/scan -> %u networks", count);

    return ESP_OK;
}

static esp_err_t wifi_connect_handler(httpd_req_t *req)
{
    if (friend_wifi_get_state() != FRIEND_WIFI_STATE_MANUAL)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Manual setup is not active");
        return ESP_FAIL;
    }

    char body[320] = {0};
    if (read_form(req, body, sizeof(body)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    char ssid[FRIEND_WIFI_SSID_MAX_LEN + 1] = {0};
    if (!form_ssid(body, ssid, sizeof(ssid)))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID");
        return ESP_FAIL;
    }

    char password[FRIEND_WIFI_PASSWORD_MAX_LEN + 1] = {0};
    bool saved = friend_wifi_store_is_saved(ssid);
    if (saved)
    {
        if (friend_wifi_store_get(ssid, password, sizeof(password)) != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Saved network unavailable");
            return ESP_FAIL;
        }
    }
    else
    {
        char encoded[FRIEND_WIFI_PASSWORD_MAX_LEN * 3 + 1] = {0};
        if (httpd_query_key_value(body, "password", encoded, sizeof(encoded)) != ESP_OK ||
            !url_decode(password, sizeof(password), encoded))
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid password");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "POST /api/wifi/connect SSID=%s", ssid);

    esp_err_t ret = saved && friend_wifi_is_connected_to(ssid) ? ESP_OK : friend_wifi_connect(ssid, password, 15000);

    if (ret == ESP_OK)
    {
        if (!saved) ret = friend_wifi_store_save(ssid, password);
        memset(password, 0, sizeof(password));
        memset(body, 0, sizeof(body));

        if (ret == ESP_OK)
        {
            httpd_resp_set_type(req, "application/json");
            char ip[16] = {0};
            if (friend_wifi_get_sta_ip(ip, sizeof(ip)) != ESP_OK) return ESP_FAIL;
            char response[64];
            snprintf(response, sizeof(response), "{\"success\":true,\"ip\":\"%s\"}", ip);
            ret = httpd_resp_sendstr(req, response);
            if (ret == ESP_OK) friend_wifi_finish_manual_mode();
            return ret;
        }

        ESP_LOGE(TAG, "Wi-Fi connected but failed to save credentials: %s", esp_err_to_name(ret));

        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"reason\":\"save_failed\"}");

        return ESP_OK;
    }

    memset(password, 0, sizeof(password));
    memset(body, 0, sizeof(body));
    ESP_LOGW(TAG, "Wi-Fi connection failed: %s", esp_err_to_name(ret));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":false}");

    return ESP_OK;
}

static esp_err_t wifi_forget_handler(httpd_req_t *req)
{
    char body[128] = {0};
    if (friend_wifi_get_state() != FRIEND_WIFI_STATE_MANUAL || read_form(req, body, sizeof(body)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }

    char ssid[FRIEND_WIFI_SSID_MAX_LEN + 1] = {0};
    if (!form_ssid(body, ssid, sizeof(ssid)))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID");
        return ESP_FAIL;
    }

    esp_err_t ret = friend_wifi_store_forget(ssid);
    if (ret == ESP_ERR_NOT_FOUND)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Network is not saved");
        return ESP_FAIL;
    }

    if (ret != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not forget network");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"success\":true}");
}

esp_err_t friend_http_start(void)
{
    if (server != NULL)
    {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 20;

    ESP_LOGI(TAG, "Starting HTTP server with stack=%u", config.stack_size);

    esp_err_t ret = httpd_start(&server, &config);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        server = NULL;
        return ret;
    }

    const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t style_css_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = style_css_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t app_js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = app_js_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t wifi_scan_uri = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = wifi_scan_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t wifi_connect_uri = {
        .uri = "/api/wifi/connect",
        .method = HTTP_POST,
        .handler = wifi_connect_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t wifi_forget_uri = {
        .uri = "/api/wifi/forget",
        .method = HTTP_POST,
        .handler = wifi_forget_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t home_js_uri = {
        .uri = "/home.js", .method = HTTP_GET, .handler = home_js_handler,
    };
    const httpd_uri_t gif_resize_js_uri = {
        .uri = "/gif-resize.js", .method = HTTP_GET, .handler = gif_resize_js_handler,
    };
    const httpd_uri_t status_uri = {
        .uri = "/api/status", .method = HTTP_GET, .handler = status_handler,
    };
    const httpd_uri_t still_uri = {
        .uri = "/api/media/still", .method = HTTP_POST, .handler = media_upload_handler,
    };
    const httpd_uri_t gif_uri = {
        .uri = "/api/media/gif", .method = HTTP_POST, .handler = media_upload_handler,
    };
    const httpd_uri_t face_uri = {
        .uri = "/api/media/face", .method = HTTP_POST, .handler = media_face_handler,
    };
    const httpd_uri_t ai_status_uri = {
        .uri = "/api/ai/status", .method = HTTP_GET, .handler = ai_status_handler,
    };
    const httpd_uri_t ai_key_uri = {
        .uri = "/api/ai/key", .method = HTTP_POST, .handler = ai_key_handler,
    };
    const httpd_uri_t ai_ask_uri = {
        .uri = "/api/ai/ask", .method = HTTP_POST, .handler = ai_ask_handler,
    };
    const httpd_uri_t ai_provider_uri = {
        .uri = "/api/ai/provider", .method = HTTP_POST, .handler = ai_provider_handler,
    };
    const httpd_uri_t ai_subtitle_test_uri = {
        .uri = "/api/ai/subtitle-test", .method = HTTP_POST, .handler = ai_subtitle_test_handler,
    };
    const httpd_uri_t ai_profile_get_uri = {
        .uri = "/api/ai/profile", .method = HTTP_GET, .handler = ai_profile_get_handler,
    };
    const httpd_uri_t ai_profile_save_uri = {
        .uri = "/api/ai/profile", .method = HTTP_POST, .handler = ai_profile_save_handler,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &style_css_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &app_js_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &wifi_scan_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &wifi_connect_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &wifi_forget_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &home_js_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &gif_resize_js_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &still_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &gif_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &face_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ai_status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ai_key_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ai_ask_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ai_provider_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ai_subtitle_test_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ai_profile_get_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ai_profile_save_uri));

    ESP_LOGI(TAG, "HTTP server ready");
    ESP_LOGI(TAG, "Home on STA IP; setup at http://192.168.4.1 while AP is active");

    return ESP_OK;
}

esp_err_t friend_http_stop(void)
{
    if (server == NULL) return ESP_OK;

    esp_err_t ret = httpd_stop(server);
    if (ret == ESP_OK) server = NULL;
    return ret;
}
