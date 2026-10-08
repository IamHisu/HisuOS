#include "hisu_setup_api.h"
#include "hisu_settings.h"
#include "hisu_network.h"
#include "hisu_clock.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>

static bool (*s_guard)(httpd_req_t *);
static void (*s_headers)(httpd_req_t *);
static const char *const keys[] = {"brightness", "volume", "speaker", "timeout", "theme", "timezone"};
static const hisu_setting_t settings[] = {HISU_SETTING_BRIGHTNESS, HISU_SETTING_VOLUME,
    HISU_SETTING_SPEAKER, HISU_SETTING_TIMEOUT, HISU_SETTING_THEME, HISU_SETTING_TIMEZONE};

static esp_err_t error(httpd_req_t *req, const char *status, const char *message)
{
    s_headers(req); httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t result = httpd_resp_sendstr(req, message);
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
    return result;
}

static esp_err_t json_response(httpd_req_t *req, cJSON *json)
{
    char *text = json ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (!text) return error(req, "500 Internal Server Error", "Not enough memory.");
    s_headers(req); httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_sendstr(req, text);
    cJSON_free(text); return result;
}

static cJSON *body(httpd_req_t *req)
{
    char type[32], buffer[385] = {0};
    if (req->content_len < 1 || req->content_len > 384 ||
        httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strcmp(type, "application/json")) return NULL;
    unsigned received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, buffer + received, req->content_len - received);
        if (count <= 0) { memset(buffer, 0, sizeof(buffer)); return NULL; }
        received += count;
    }
    /* Bound parser recursion before cJSON touches an untrusted request. */
    unsigned depth = 0;
    bool quoted = false, escaped = false, bounded = true;
    for (unsigned i = 0; i < received; ++i) {
        char c = buffer[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 2) { bounded = false; break; } }
        else if ((c == '}' || c == ']') && depth) --depth;
    }
    cJSON *json = !bounded || memchr(buffer, 0, received) ? NULL :
        cJSON_ParseWithLengthOpts(buffer, received + 1, NULL, true);
    memset(buffer, 0, sizeof(buffer));
    if (!cJSON_IsObject(json)) { cJSON_Delete(json); return NULL; }
    return json;
}

static esp_err_t settings_api(httpd_req_t *req)
{
    if (!s_guard(req)) return error(req, "403 Forbidden", "Session expired.");
    if (req->method == HTTP_GET) {
        cJSON *json = cJSON_CreateObject();
        for (unsigned i = 0; json && i < sizeof(settings) / sizeof(settings[0]); ++i)
            if (!cJSON_AddNumberToObject(json, keys[i], hisu_settings_get(settings[i]))) {
                cJSON_Delete(json); json = NULL; break;
            }
        return json_response(req, json);
    }
    cJSON *json = body(req);
    cJSON *key = cJSON_GetObjectItemCaseSensitive(json, "key");
    cJSON *value = cJSON_GetObjectItemCaseSensitive(json, "value");
    int index = -1;
    if (json && cJSON_GetArraySize(json) == 2 && cJSON_IsString(key) && cJSON_IsNumber(value) &&
        isfinite(value->valuedouble) && value->valuedouble >= -720 && value->valuedouble <= 840 &&
        value->valuedouble == value->valueint)
        for (unsigned i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i)
            if (!strcmp(key->valuestring, keys[i])) index = i;
    if (!s_guard(req)) { cJSON_Delete(json); return error(req, "403 Forbidden", "Session expired."); }
    esp_err_t result = index < 0 ? ESP_ERR_INVALID_ARG : hisu_settings_set(settings[index], value->valueint);
    cJSON_Delete(json);
    if (result != ESP_OK) return error(req, result == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "500 Internal Server Error", "Setting rejected.");
    s_headers(req); httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"saved\":true}");
}

static esp_err_t wifi_status(httpd_req_t *req)
{
    if (!s_guard(req)) return error(req, "403 Forbidden", "Session expired.");
    hisu_network_status_t state; hisu_network_get(&state);
    cJSON *json = cJSON_CreateObject(), *aps = cJSON_CreateArray();
    bool ok = json && aps && cJSON_AddBoolToObject(json, "enabled", state.enabled) &&
        cJSON_AddBoolToObject(json, "scanning", state.scanning) && cJSON_AddBoolToObject(json, "joining", state.joining) &&
        cJSON_AddBoolToObject(json, "connected", state.connected) && cJSON_AddStringToObject(json, "ssid", state.ssid) &&
        cJSON_AddStringToObject(json, "ip", state.ip) && cJSON_AddNumberToObject(json, "error", state.error);
    for (int i = 0; ok && i < state.count; ++i) {
        cJSON *ap = cJSON_CreateObject();
        ok = ap && cJSON_AddStringToObject(ap, "ssid", state.aps[i].ssid) &&
            cJSON_AddNumberToObject(ap, "rssi", state.aps[i].rssi) && cJSON_AddBoolToObject(ap, "saved", state.aps[i].saved) &&
            cJSON_AddBoolToObject(ap, "secure", state.aps[i].secure) && cJSON_AddBoolToObject(ap, "supported", state.aps[i].supported);
        if (ok) ok = cJSON_AddItemToArray(aps, ap);
        if (!ok) cJSON_Delete(ap);
    }
    if (ok) ok = cJSON_AddItemToObject(json, "aps", aps);
    if (!ok) { cJSON_Delete(aps); cJSON_Delete(json); json = NULL; }
    memset(&state, 0, sizeof(state));
    return json_response(req, json);
}

static esp_err_t time_api(httpd_req_t *req)
{
    if (!s_guard(req)) return error(req, "403 Forbidden", "Session expired.");
    cJSON *json = body(req);
    cJSON *utc = cJSON_GetObjectItemCaseSensitive(json, "utc");
    bool valid = json && cJSON_GetArraySize(json) == 1 && cJSON_IsNumber(utc) &&
        isfinite(utc->valuedouble) && utc->valuedouble >= 1609459200 && utc->valuedouble <= 4102444800 &&
        floor(utc->valuedouble) == utc->valuedouble;
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (valid && s_guard(req)) result = hisu_clock_set((uint64_t)utc->valuedouble, hisu_settings_get(HISU_SETTING_TIMEZONE));
    cJSON_Delete(json);
    if (result != ESP_OK) return error(req, "400 Bad Request", "Time request rejected.");
    s_headers(req); httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"saved\":true}");
}

static esp_err_t asr_key_api(httpd_req_t *req)
{
    if (!s_guard(req)) return error(req, "403 Forbidden", "Session expired.");
    bool gemini = strcmp(req->uri, "/gemini-key") == 0;
    if (req->method == HTTP_GET) {
        cJSON *json = cJSON_CreateObject();
        if (json) cJSON_AddBoolToObject(json, "configured",
                                         gemini ? hisu_settings_has_gemini_key() : hisu_settings_has_asr_key());
        return json_response(req, json);
    }
    cJSON *json = body(req);
    cJSON *key = cJSON_GetObjectItemCaseSensitive(json, "key");
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (s_guard(req) && json && cJSON_GetArraySize(json) == 1 && cJSON_IsString(key))
        result = gemini ? hisu_settings_set_gemini_key(key->valuestring) :
                          hisu_settings_set_asr_key(key->valuestring);
    if (cJSON_IsString(key)) memset(key->valuestring, 0, strlen(key->valuestring));
    cJSON_Delete(json);
    if (result != ESP_OK) return error(req, "400 Bad Request", "API key rejected.");
    s_headers(req); httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"saved\":true}");
}

static esp_err_t wifi_action(httpd_req_t *req)
{
    if (!s_guard(req)) return error(req, "403 Forbidden", "Session expired.");
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (!strcmp(req->uri, "/wifi/scan")) {
        if (req->content_len == 0) result = hisu_network_provision_scan();
    } else {
        cJSON *json = body(req);
        if (!s_guard(req)) {
            cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
            if (cJSON_IsString(password)) memset(password->valuestring, 0, strlen(password->valuestring));
            cJSON_Delete(json); return error(req, "403 Forbidden", "Session expired.");
        }
        if (!strcmp(req->uri, "/wifi/power")) {
            cJSON *enabled = cJSON_GetObjectItemCaseSensitive(json, "enabled");
            if (json && cJSON_GetArraySize(json) == 1 && cJSON_IsBool(enabled))
                result = hisu_network_enable(cJSON_IsTrue(enabled));
        } else {
            cJSON *ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid");
            cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
            cJSON *saved = cJSON_GetObjectItemCaseSensitive(json, "saved");
            if (json && cJSON_IsString(ssid) && cJSON_IsString(password)) {
                if (cJSON_GetArraySize(json) == 2 && !saved)
                    result = hisu_network_provision(ssid->valuestring, password->valuestring);
                else if (cJSON_GetArraySize(json) == 3 && cJSON_IsTrue(saved) && !password->valuestring[0]) {
                    char stored[65] = {0};
                    if (hisu_settings_find_wifi(ssid->valuestring, stored) == ESP_OK)
                        result = hisu_network_provision(ssid->valuestring, stored);
                    memset(stored, 0, sizeof(stored));
                }
            }
            if (cJSON_IsString(password)) memset(password->valuestring, 0, strlen(password->valuestring));
        }
        cJSON_Delete(json);
    }
    if (result != ESP_OK) return error(req, result == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "409 Conflict", "Wi-Fi request rejected; retry when idle.");
    s_headers(req); httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"accepted\":true}");
}

esp_err_t hisu_setup_api_register(httpd_handle_t server, bool (*guard)(httpd_req_t *), void (*headers)(httpd_req_t *))
{
    s_guard = guard; s_headers = headers;
    const httpd_uri_t routes[] = {
        {.uri="/settings", .method=HTTP_GET, .handler=settings_api},
        {.uri="/settings", .method=HTTP_POST, .handler=settings_api},
        {.uri="/wifi", .method=HTTP_GET, .handler=wifi_status},
        {.uri="/wifi/scan", .method=HTTP_POST, .handler=wifi_action},
        {.uri="/wifi/join", .method=HTTP_POST, .handler=wifi_action},
        {.uri="/wifi/power", .method=HTTP_POST, .handler=wifi_action},
        {.uri="/time", .method=HTTP_POST, .handler=time_api},
        {.uri="/asr-key", .method=HTTP_GET, .handler=asr_key_api},
        {.uri="/asr-key", .method=HTTP_POST, .handler=asr_key_api},
        {.uri="/gemini-key", .method=HTTP_GET, .handler=asr_key_api},
        {.uri="/gemini-key", .method=HTTP_POST, .handler=asr_key_api},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
