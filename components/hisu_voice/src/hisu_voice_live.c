#include "hisu_voice_live.h"
#include "hisu_voice_live_message.h"
#include "hisu_voice_live_turn.h"
#include "hisu_voice_live_setup.h"
#include "hisu_network.h"
#include "hisu_settings.h"
#include "hisu_media.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "board_config.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "mbedtls/base64.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MESSAGE_MAX (128 * 1024)
#define INPUT_SAMPLES 640
#define INPUT_QUEUE_FRAMES 256
#define SEND_SAMPLES 1600
#define ENCODED_CAPACITY (((SEND_SAMPLES * 2 + 2) / 3) * 4 + 1)
#define SEND_BODY_CAPACITY (ENCODED_CAPACITY + 128)
typedef struct { uint32_t epoch, generation; unsigned kind; size_t count; int16_t pcm[INPUT_SAMPLES]; } input_t;
typedef struct { uint32_t epoch, generation; int16_t *pcm; size_t bytes; bool done; } output_t;
static const char *TAG = "LIVE";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_epoch, s_generation;
static bool s_active, s_ready, s_failed;
static live_turn_t s_turn;
static int64_t s_interrupt_deadline;
static QueueHandle_t s_input, s_output;
static StaticQueue_t s_input_control;
static uint8_t *s_input_storage;
static char *s_send_storage;
static TaskHandle_t s_transport_task, s_playback_task;
static live_event_fn s_callback;
static i2s_chan_handle_t s_speaker;
static int64_t s_end_us;
static size_t s_queued_audio_bytes;
static bool s_reboot_pending;
static uint32_t s_reboot_generation;
static int64_t s_reboot_deadline;
static char s_confirmation[160];

/* Compare the entire user transcription, never a model-generated argument. */
static bool reboot_confirmed(void)
{
    const char *start = s_confirmation;
    while (*start && strchr(" \r\n\t", *start)) ++start;
    size_t length = strlen(start);
    while (length && strchr(" .!?\r\n\t", start[length - 1])) --length;
    static const char *const phrases[] = {"xác nhận khởi động lại", "Xác nhận khởi động lại",
        "xac nhan khoi dong lai", "Xac nhan khoi dong lai"};
    for (unsigned i = 0; i < sizeof(phrases) / sizeof(phrases[0]); ++i)
        if (strlen(phrases[i]) == length && !memcmp(start, phrases[i], length)) return true;
    return false;
}

static bool current(uint32_t epoch, uint32_t generation)
{
    portENTER_CRITICAL(&s_mux);
    bool ok = s_active && !s_failed && epoch == s_epoch && generation == s_generation;
    portEXIT_CRITICAL(&s_mux);
    return ok;
}
static void fail(uint32_t epoch, uint32_t generation)
{
    portENTER_CRITICAL(&s_mux);
    bool notify = s_active && epoch == s_epoch && !s_failed;
    generation = s_generation;
    if (notify) { s_failed = true; s_ready = false; }
    portEXIT_CRITICAL(&s_mux);
    if (notify) s_callback(LIVE_ERROR, generation, "Gemini Live bị ngắt, gọi Hi Su để thử lại");
}
void voice_live_open(uint32_t generation)
{
    portENTER_CRITICAL(&s_mux);
    ++s_epoch; s_generation = generation; s_active = true; s_ready = s_failed = false;
    s_reboot_pending = false; s_confirmation[0] = 0;
    live_turn_reset(&s_turn); s_interrupt_deadline = 0;
    portEXIT_CRITICAL(&s_mux);
}
void voice_live_close(void)
{
    portENTER_CRITICAL(&s_mux);
    ++s_epoch; s_active = s_ready = false;
    s_reboot_pending = false; s_confirmation[0] = 0;
    live_turn_reset(&s_turn); s_interrupt_deadline = 0;
    portEXIT_CRITICAL(&s_mux);
}
bool voice_live_ready(void)
{
    portENTER_CRITICAL(&s_mux);
    bool ready = s_active && s_ready && !s_failed;
    portEXIT_CRITICAL(&s_mux);
    return ready;
}
static bool enqueue(uint32_t generation, unsigned kind, const int16_t *pcm, size_t count)
{
    input_t frame = {.generation = generation, .kind = kind, .count = count};
    portENTER_CRITICAL(&s_mux);
    frame.epoch = s_epoch;
    bool ready = s_ready && s_active && !s_failed && generation == s_generation;
    portEXIT_CRITICAL(&s_mux);
    if (!ready || count > INPUT_SAMPLES) return false;
    if (count) memcpy(frame.pcm, pcm, count * 2);
    if (xQueueSend(s_input, &frame, 0) == pdTRUE) return true;
    ESP_LOGW(TAG, "Input queue full; abort turn instead of losing audio");
    fail(frame.epoch, generation);
    return false;
}
bool voice_live_interrupt(uint32_t generation)
{
    input_t frame = {.generation = generation, .kind = 4};
    portENTER_CRITICAL(&s_mux);
    bool active = s_active && !s_failed;
    bool ready = s_ready;
    if (active) {
        frame.epoch = s_epoch;
        s_generation = generation;
        live_turn_interrupt(&s_turn);
        if (s_turn.obsolete_turns || s_turn.input_open)
            s_interrupt_deadline = esp_timer_get_time() + 10000000;
    }
    portEXIT_CRITICAL(&s_mux);
    /* During setup, retain the connection and deliver LIVE_READY to this generation. */
    return active && (!ready || xQueueSend(s_input, &frame, 0) == pdTRUE);
}
bool voice_live_begin(uint32_t generation)
{
    portENTER_CRITICAL(&s_mux);
    bool ready = s_active && s_ready && !s_failed &&
        (generation == s_generation || generation == s_generation + 1);
    if (ready) { s_generation = generation; s_confirmation[0] = 0; }
    portEXIT_CRITICAL(&s_mux);
    return ready && enqueue(generation, 1, NULL, 0);
}
bool voice_live_feed(uint32_t generation, const int16_t *pcm, size_t samples)
{
    while (samples) {
        size_t count = samples > INPUT_SAMPLES ? INPUT_SAMPLES : samples;
        if (!enqueue(generation, 0, pcm, count)) return false;
        pcm += count; samples -= count;
    }
    return true;
}
bool voice_live_end(uint32_t generation) { return enqueue(generation, 2, NULL, 0); }

typedef struct { uint32_t epoch; char *message; live_message_t parser; bool connected; } connection_t;
static void parse_message(connection_t *connection)
{
    portENTER_CRITICAL(&s_mux);
    uint32_t generation = s_generation;
    portEXIT_CRITICAL(&s_mux);
    if (!current(connection->epoch, generation)) return;
    cJSON *json = cJSON_ParseWithLength(connection->message, connection->parser.used);
    if (!json) { fail(connection->epoch, generation); return; }
    if (cJSON_GetObjectItem(json, "error") || cJSON_GetObjectItem(json, "goAway")) {
        cJSON *code = cJSON_GetObjectItem(cJSON_GetObjectItem(json, "error"), "code");
        ESP_LOGW(TAG, "Server error code=%d or session expiry", cJSON_IsNumber(code) ? code->valueint : 0);
        fail(connection->epoch, generation);
    } else if (cJSON_GetObjectItem(json, "setupComplete")) {
        ESP_LOGI(TAG, "Gemini Live setup ready");
        portENTER_CRITICAL(&s_mux);
        if (connection->epoch == s_epoch) s_ready = true;
        portEXIT_CRITICAL(&s_mux);
        ESP_LOGI(TAG, "RAM internal free=%u min=%u largest=%u; PSRAM free=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        s_callback(LIVE_READY, generation, "Mình đang nghe...");
    } else if (cJSON_GetObjectItem(json, "toolCall")) {
        portENTER_CRITICAL(&s_mux);
        bool accept = live_turn_accept_output(&s_turn);
        portEXIT_CRITICAL(&s_mux);
        if (!accept) { cJSON_Delete(json); return; }
        cJSON *calls = cJSON_GetObjectItem(cJSON_GetObjectItem(json, "toolCall"), "functionCalls");
        cJSON *call;
        cJSON_ArrayForEach(call, calls) {
            cJSON *name = cJSON_GetObjectItem(call, "name"), *id = cJSON_GetObjectItem(call, "id");
            if (!cJSON_IsString(name) || !cJSON_IsString(id)) {
                fail(connection->epoch, generation); break;
            }
            /* NVS writes run on the transport worker, never the WebSocket callback. */
            char *body = cJSON_PrintUnformatted(call);
            input_t frame = {.epoch = connection->epoch, .generation = generation, .kind = 5};
            if (!body || strlen(body) >= sizeof(frame.pcm)) {
                cJSON_free(body); fail(connection->epoch, generation); break;
            }
            strcpy((char *)frame.pcm, body); cJSON_free(body);
            if (xQueueSend(s_input, &frame, 0) != pdTRUE) { fail(connection->epoch, generation); break; }
        }
    } else {
        cJSON *content = cJSON_GetObjectItem(json, "serverContent");
        portENTER_CRITICAL(&s_mux);
        bool accept_input = live_turn_accept_input(&s_turn);
        bool accept_output = live_turn_accept_output(&s_turn);
        bool interrupted = cJSON_IsTrue(cJSON_GetObjectItem(content, "interrupted"));
        if (interrupted) accept_input = accept_output = false;
        portEXIT_CRITICAL(&s_mux);
        cJSON *input = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "inputTranscription"), "text");
        if (accept_input && cJSON_IsString(input)) {
            portENTER_CRITICAL(&s_mux);
            if (connection->epoch == s_epoch && generation == s_generation) {
                size_t used = strlen(s_confirmation), extra = strlen(input->valuestring);
                if (extra >= sizeof(s_confirmation) - used) {
                    s_reboot_pending = false; /* Truncation must never create a confirmation. */
                } else memcpy(s_confirmation + used, input->valuestring, extra + 1);
            }
            portEXIT_CRITICAL(&s_mux);
            s_callback(LIVE_INPUT, generation, input->valuestring);
        }
        cJSON *text = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "outputTranscription"), "text");
        if (accept_output && cJSON_IsString(text)) s_callback(LIVE_TEXT, generation, text->valuestring);
        cJSON *parts = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "modelTurn"), "parts");
        cJSON *part;
        cJSON_ArrayForEach(part, parts) {
            if (!accept_output) break;
            cJSON *blob = cJSON_GetObjectItem(part, "inlineData");
            cJSON *encoded = cJSON_GetObjectItem(blob, "data");
            cJSON *mime = cJSON_GetObjectItem(blob, "mimeType");
            if (!cJSON_IsString(encoded)) continue;
            if (!cJSON_IsString(mime) || strcmp(mime->valuestring, "audio/pcm;rate=24000")) {
                fail(connection->epoch, generation); break;
            }
            size_t n = strlen(encoded->valuestring), bytes = 0;
            int16_t *pcm = heap_caps_malloc(n * 3 / 4 + 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!pcm) { fail(connection->epoch, generation); break; }
            int err = mbedtls_base64_decode((unsigned char *)pcm, n * 3 / 4 + 4, &bytes,
                                            (unsigned char *)encoded->valuestring, n);
            output_t audio = {.epoch = connection->epoch, .generation = generation, .pcm = pcm, .bytes = bytes};
            portENTER_CRITICAL(&s_mux);
            bool room = s_queued_audio_bytes + bytes <= 512 * 1024;
            if (room) s_queued_audio_bytes += bytes;
            portEXIT_CRITICAL(&s_mux);
            if (err || bytes % 2 || !room || xQueueSend(s_output, &audio, 0) != pdTRUE) {
                if (room) { portENTER_CRITICAL(&s_mux); s_queued_audio_bytes -= bytes; portEXIT_CRITICAL(&s_mux); }
                free(pcm); fail(connection->epoch, generation); break;
            }
        }
        if (cJSON_IsTrue(cJSON_GetObjectItem(content, "turnComplete"))) {
            portENTER_CRITICAL(&s_mux);
            bool completed = connection->epoch == s_epoch && s_active &&
                (generation == s_generation || s_turn.obsolete_turns) && live_turn_complete(&s_turn);
            bool done_current = completed && generation == s_generation;
            if (!s_turn.obsolete_turns) s_interrupt_deadline = 0;
            portEXIT_CRITICAL(&s_mux);
            if (!done_current || interrupted) { cJSON_Delete(json); return; }
            output_t done = {.epoch = connection->epoch, .generation = generation, .done = true};
            if (xQueueSend(s_output, &done, 0) != pdTRUE) fail(connection->epoch, generation);
        }
    }
    cJSON_Delete(json);
}
static void websocket_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    connection_t *connection = arg;
    esp_websocket_event_data_t *event = data;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        portENTER_CRITICAL(&s_mux); connection->connected = true; portEXIT_CRITICAL(&s_mux);
    }
    else if (id == WEBSOCKET_EVENT_ERROR || id == WEBSOCKET_EVENT_DISCONNECTED) {
        if (id == WEBSOCKET_EVENT_ERROR && event)
            ESP_LOGW(TAG, "WSS error: handshake=%d TLS=0x%x",
                     event->error_handle.esp_ws_handshake_status_code,
                     (unsigned)event->error_handle.esp_tls_stack_err);
        portENTER_CRITICAL(&s_mux); uint32_t generation = s_generation; portEXIT_CRITICAL(&s_mux);
        fail(connection->epoch, generation);
    } else if (id == WEBSOCKET_EVENT_DATA && (event->op_code == 1 || event->op_code == 2 || event->op_code == 0)) {
        int result = live_message_feed(&connection->parser, connection->message, MESSAGE_MAX + 1,
                                      event->data_ptr, event->data_len, event->op_code, event->fin,
                                      event->payload_offset, event->payload_len);
        if (result < 0) {
            portENTER_CRITICAL(&s_mux); uint32_t generation = s_generation; portEXIT_CRITICAL(&s_mux);
            fail(connection->epoch, generation); return;
        }
        if (result == 1) parse_message(connection);
    } else if (id == WEBSOCKET_EVENT_DATA && event->op_code == 8) {
        portENTER_CRITICAL(&s_mux); uint32_t generation = s_generation; portEXIT_CRITICAL(&s_mux);
        fail(connection->epoch, generation);
    }
}
static bool send_text(esp_websocket_client_handle_t client, const char *text)
{
    size_t length = strlen(text);
    int64_t started = esp_timer_get_time();
    int sent = esp_websocket_client_send_text(client, text, length, pdMS_TO_TICKS(1000));
    int64_t elapsed = (esp_timer_get_time() - started) / 1000;
    if (elapsed > 100) ESP_LOGW(TAG, "Slow WSS send: %lld ms, pending=%u", (long long)elapsed,
                                (unsigned)uxQueueMessagesWaiting(s_input));
    return sent == (int)length;
}
static size_t collect_audio(const input_t *first, int16_t *pcm)
{
    size_t samples = first->count;
    memcpy(pcm, first->pcm, samples * sizeof(*pcm));
    input_t next;
    /* Drain only queued audio from this turn, leaving control messages in order. */
    while (xQueuePeek(s_input, &next, 0) == pdTRUE && next.kind == 0 &&
           next.epoch == first->epoch && next.generation == first->generation &&
           next.count <= SEND_SAMPLES - samples) {
        if (xQueueReceive(s_input, &next, 0) != pdTRUE) break;
        memcpy(pcm + samples, next.pcm, next.count * sizeof(*pcm));
        samples += next.count;
    }
    return samples;
}
static void format_audio(char *body, const char *encoded, size_t length)
{
    static const char prefix[] = "{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"";
    static const char suffix[] = "\"}}}";
    _Static_assert(sizeof(prefix) - 1 + ENCODED_CAPACITY - 1 + sizeof(suffix) <= SEND_BODY_CAPACITY,
                   "Audio JSON must fit the send buffer");
    memcpy(body, prefix, sizeof(prefix) - 1);
    memcpy(body + sizeof(prefix) - 1, encoded, length);
    memcpy(body + sizeof(prefix) - 1 + length, suffix, sizeof(suffix));
}
static bool tool_integer(const cJSON *args, const char *key, int minimum, int maximum, int *value)
{
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(args, key);
    if (!cJSON_IsObject(args) || cJSON_GetArraySize(args) != 1 || !cJSON_IsNumber(number) ||
        number->valuedouble < minimum || number->valuedouble > maximum ||
        number->valuedouble != number->valueint) return false;
    *value = number->valueint;
    return true;
}

static char *execute_tool(const input_t *frame, bool *end_session)
{
    *end_session = false;
    if (!current(frame->epoch, frame->generation)) return NULL;
    cJSON *call = cJSON_Parse((const char *)frame->pcm);
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(call, "name");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(call, "id");
    const cJSON *args = cJSON_GetObjectItemCaseSensitive(call, "args");
    if (!cJSON_IsString(name) || !cJSON_IsString(id)) { cJSON_Delete(call); return NULL; }
    cJSON *response = cJSON_CreateObject();
    cJSON *tool = cJSON_AddObjectToObject(response, "toolResponse");
    cJSON *results = cJSON_AddArrayToObject(tool, "functionResponses");
    cJSON *result = cJSON_CreateObject();
    if (!response || !tool || !results || !result || !cJSON_AddItemToArray(results, result)) {
        cJSON_Delete(result); cJSON_Delete(response); cJSON_Delete(call); return NULL;
    }
    cJSON *value = cJSON_AddObjectToObject(result, "response");
    if (!value || !cJSON_AddStringToObject(result,"id",id->valuestring) ||
        !cJSON_AddStringToObject(result,"name",name->valuestring)) {
        cJSON_Delete(response); cJSON_Delete(call); return NULL;
    }
    bool no_args = !args || (cJSON_IsObject(args) && cJSON_GetArraySize(args) == 0);
    bool valid = false, changed = false;
    int setting_value = 0;
    hisu_setting_t setting = HISU_SETTING_COUNT;
    esp_err_t err = ESP_OK;
    if (!strcmp(name->valuestring,"get_network_status") && no_args) {
        hisu_network_status_t network;
        hisu_network_get(&network);
        static const char *const states[] = {"unknown", "checking", "online", "captive_portal", "offline"};
        valid = true;
        cJSON_AddBoolToObject(value,"enabled",network.enabled);
        cJSON_AddBoolToObject(value,"connected",network.connected);
        cJSON_AddBoolToObject(value,"joining",network.joining);
        cJSON_AddStringToObject(value,"ssid",network.connected ? network.ssid : "");
        cJSON_AddStringToObject(value,"ip",network.connected ? network.ip : "");
        if (network.connected) cJSON_AddNumberToObject(value,"rssi_dbm",network.rssi);
        cJSON_AddStringToObject(value,"internet",!network.connected ? "disconnected" :
            network.internet >= HISU_INTERNET_UNKNOWN && network.internet <= HISU_INTERNET_OFFLINE ?
            states[network.internet] : "unknown");
        cJSON_AddStringToObject(value,"note","Last observed state; connectivity probes pause during voice calls");
    } else if (!strcmp(name->valuestring,"get_system_info") && no_args) {
        const esp_app_desc_t *app = esp_app_get_description();
        valid = true;
        cJSON_AddStringToObject(value,"firmware",app->version);
        cJSON_AddStringToObject(value,"project",app->project_name);
        cJSON_AddStringToObject(value,"idf",app->idf_ver);
        cJSON_AddNumberToObject(value,"uptime_seconds",(double)(esp_timer_get_time() / 1000000));
        cJSON_AddNumberToObject(value,"internal_free_bytes",(double)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        cJSON_AddNumberToObject(value,"internal_largest_bytes",(double)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        cJSON_AddNumberToObject(value,"psram_free_bytes",(double)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        cJSON_AddNumberToObject(value,"screen_width",HISU_MEDIA_WIDTH);
        cJSON_AddNumberToObject(value,"screen_height",HISU_MEDIA_HEIGHT);
    } else if (!strcmp(name->valuestring,"get_wallpaper") && no_args) {
        hisu_media_status_t media;
        hisu_media_get(&media);
        valid = true;
        cJSON_AddBoolToObject(value,"uploaded_available",media.has_image);
        cJSON_AddStringToObject(value,"source",media.has_image &&
            hisu_settings_get(HISU_SETTING_WALLPAPER_DEFAULT) != 1 ? "uploaded" : "default");
        if (media.has_image) cJSON_AddStringToObject(value,"uploaded_type",media.gif ? "gif" : "image");
    } else if (!strcmp(name->valuestring,"set_wallpaper")) {
        const cJSON *source = cJSON_GetObjectItemCaseSensitive(args,"source");
        valid = cJSON_IsObject(args) && cJSON_GetArraySize(args) == 1 && cJSON_IsString(source) &&
            (!strcmp(source->valuestring,"default") || !strcmp(source->valuestring,"uploaded"));
        if (valid) {
            hisu_media_status_t media;
            hisu_media_get(&media);
            setting = HISU_SETTING_WALLPAPER_DEFAULT;
            setting_value = !strcmp(source->valuestring,"default");
            if (!setting_value && !media.has_image) {
                err = ESP_ERR_NOT_FOUND;
                cJSON_AddStringToObject(value,"detail","No uploaded wallpaper is stored");
            }
        }
    } else if (!strcmp(name->valuestring,"request_reboot") && no_args) {
        portENTER_CRITICAL(&s_mux);
        valid = s_active && frame->epoch == s_epoch && frame->generation == s_generation;
        if (valid) {
            s_reboot_pending = true; s_reboot_generation = frame->generation;
            s_reboot_deadline = esp_timer_get_time() + 60000000;
        }
        portEXIT_CRITICAL(&s_mux);
        cJSON_AddBoolToObject(value,"confirmation_required",true);
        cJSON_AddStringToObject(value,"confirmation_phrase","xác nhận khởi động lại");
    } else if (!strcmp(name->valuestring,"confirm_reboot") && no_args) {
        portENTER_CRITICAL(&s_mux);
        valid = s_reboot_pending && s_active && frame->epoch == s_epoch &&
            frame->generation == s_generation && frame->generation != s_reboot_generation &&
            esp_timer_get_time() <= s_reboot_deadline && reboot_confirmed();
        portEXIT_CRITICAL(&s_mux);
        if (valid) *end_session = true;
        else cJSON_AddStringToObject(value,"detail","A pending request and a new explicit user confirmation are required");
    } else if (!strcmp(name->valuestring,"get_device_settings") && no_args) {
        valid = true;
        cJSON_AddNumberToObject(value,"volume",hisu_settings_get(HISU_SETTING_VOLUME));
        cJSON_AddNumberToObject(value,"brightness",hisu_settings_get(HISU_SETTING_BRIGHTNESS));
        cJSON_AddBoolToObject(value,"speaker",hisu_settings_get(HISU_SETTING_SPEAKER) == 1);
        cJSON_AddStringToObject(value,"theme",hisu_settings_get(HISU_SETTING_THEME) == 1 ? "light" : "dark");
        cJSON_AddNumberToObject(value,"screen_timeout",hisu_settings_get(HISU_SETTING_TIMEOUT));
    } else if (!strcmp(name->valuestring,"end_conversation") && no_args) {
        valid = true;
        *end_session = true;
    } else if (!strcmp(name->valuestring,"set_volume")) {
        setting = HISU_SETTING_VOLUME;
        valid = tool_integer(args,"percent",0,100,&setting_value);
    } else if (!strcmp(name->valuestring,"set_brightness")) {
        setting = HISU_SETTING_BRIGHTNESS;
        valid = tool_integer(args,"percent",10,100,&setting_value);
    } else if (!strcmp(name->valuestring,"set_screen_timeout")) {
        setting = HISU_SETTING_TIMEOUT;
        valid = tool_integer(args,"seconds",0,600,&setting_value) &&
            (setting_value == 0 || setting_value == 30 || setting_value == 60 ||
             setting_value == 120 || setting_value == 300 || setting_value == 600);
    } else if (!strcmp(name->valuestring,"set_theme")) {
        const cJSON *theme = cJSON_GetObjectItemCaseSensitive(args,"theme");
        setting = HISU_SETTING_THEME;
        valid = cJSON_IsObject(args) && cJSON_GetArraySize(args) == 1 && cJSON_IsString(theme) &&
            (!strcmp(theme->valuestring,"light") || !strcmp(theme->valuestring,"dark"));
        if (valid) setting_value = !strcmp(theme->valuestring,"light");
    } else if (!strcmp(name->valuestring,"set_speaker") || !strcmp(name->valuestring,"set_flashlight")) {
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(args,"enabled");
        valid = cJSON_IsObject(args) && cJSON_GetArraySize(args) == 1 && cJSON_IsBool(enabled);
        if (valid && !strcmp(name->valuestring,"set_flashlight")) {
            s_callback(LIVE_FLASHLIGHT,frame->generation,cJSON_IsTrue(enabled) ? "on" : "off");
            cJSON_AddBoolToObject(value,"enabled",cJSON_IsTrue(enabled));
        } else if (valid) {
            setting = HISU_SETTING_SPEAKER;
            setting_value = cJSON_IsTrue(enabled);
        }
    }
    if (valid && err == ESP_OK && setting != HISU_SETTING_COUNT) {
        err = hisu_settings_set(setting,setting_value);
        changed = err == ESP_OK;
        if (changed) cJSON_AddNumberToObject(value,"value",hisu_settings_get(setting));
    }
    cJSON_AddBoolToObject(value,"ok",valid && err == ESP_OK);
    ESP_LOGI(TAG,"Device tool %s: %s",name->valuestring, valid && err == ESP_OK ? "ok" : "rejected");
    if (!valid) cJSON_AddStringToObject(value,"error","Unsupported function or invalid arguments");
    else if (err != ESP_OK) cJSON_AddStringToObject(value,"error","Device could not apply the request");
    char *body = cJSON_PrintUnformatted(response);
    if (changed) s_callback(LIVE_DEVICE,frame->generation,"");
    cJSON_Delete(response); cJSON_Delete(call);
    return body;
}

static bool run_tool(esp_websocket_client_handle_t client, const input_t *frame)
{
    bool end_session;
    char *response = execute_tool(frame,&end_session);
    if (!response && !current(frame->epoch,frame->generation)) return true;
    bool ok = response && send_text(client,response);
    cJSON_free(response);
    /* Reboot only after acknowledging a confirmed request on this still-current turn. */
    cJSON *call = end_session ? cJSON_Parse((const char *)frame->pcm) : NULL;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(call,"name");
    bool reboot = ok && current(frame->epoch,frame->generation) && cJSON_IsString(name) &&
        !strcmp(name->valuestring,"confirm_reboot");
    cJSON_Delete(call);
    if (end_session) s_callback(LIVE_END,frame->generation,"");
    if (reboot) { vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); }
    return ok;
}

static void transport_task(void *arg)
{
    (void)arg;
    input_t frame;
    int16_t *pcm = (int16_t *)s_send_storage;
    char *encoded = s_send_storage + SEND_SAMPLES * sizeof(*pcm);
    char *body = encoded + ENCODED_CAPACITY;
    const char *setup = voice_live_setup;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        bool active = s_active && !s_failed; uint32_t epoch = s_epoch, generation = s_generation;
        portEXIT_CRITICAL(&s_mux);
        if (!active) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        char key[128], headers[160];
        if (hisu_settings_get_gemini_key(key) != ESP_OK) { fail(epoch, generation); continue; }
        snprintf(headers, sizeof(headers), "x-goog-api-key: %s\r\n", key); memset(key, 0, sizeof(key));
        connection_t connection = {.epoch = epoch};
        connection.message = heap_caps_malloc(MESSAGE_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        esp_websocket_client_config_t config = {
            .uri = "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent",
            .headers = headers, .crt_bundle_attach = esp_crt_bundle_attach,
            .disable_auto_reconnect = true, .buffer_size = 4096, .task_stack = 6144,
            .task_prio = 4, .network_timeout_ms = 10000,
        };
        esp_websocket_client_handle_t client = connection.message ? esp_websocket_client_init(&config) : NULL;
        memset(headers, 0, sizeof(headers));
        if (!client) { free(connection.message); fail(epoch, generation); continue; }
        if (hisu_network_set_performance(true) != ESP_OK) ESP_LOGW(TAG, "Cannot request Wi-Fi performance mode");
        esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event, &connection);
        if (esp_websocket_client_start(client) != ESP_OK) fail(epoch, generation);
        bool sent_setup = false;
        int64_t response_deadline = 0;
        int64_t started = esp_timer_get_time();
        while (true) {
            portENTER_CRITICAL(&s_mux);
            active = s_active && !s_failed && epoch == s_epoch;
            generation = s_generation;
            bool response_pending = s_turn.response_pending;
            int64_t interrupt_deadline = s_interrupt_deadline;
            portEXIT_CRITICAL(&s_mux);
            if (!active) break;
            if ((response_pending && response_deadline && esp_timer_get_time() >= response_deadline) ||
                (interrupt_deadline && esp_timer_get_time() >= interrupt_deadline)) {
                fail(epoch, generation); break;
            }
            if (!voice_live_ready() && esp_timer_get_time() - started > 15000000) { fail(epoch, generation); break; }
            portENTER_CRITICAL(&s_mux); bool connected = connection.connected; portEXIT_CRITICAL(&s_mux);
            if (connected && !sent_setup) {
                sent_setup = send_text(client, setup);
                if (!sent_setup) { fail(epoch, generation); break; }
            }
            if (xQueueReceive(s_input, &frame, pdMS_TO_TICKS(10)) != pdTRUE) continue;
            if (!current(frame.epoch, frame.generation)) continue;
            bool ok;
            if (frame.kind == 1 || frame.kind == 4) {
                response_deadline = 0;
                portENTER_CRITICAL(&s_mux);
                if (frame.epoch != s_epoch || frame.generation != s_generation) {
                    portEXIT_CRITICAL(&s_mux); continue;
                }
                unsigned action = live_turn_start(&s_turn);
                s_interrupt_deadline = s_turn.obsolete_turns ? esp_timer_get_time() + 10000000 : 0;
                portEXIT_CRITICAL(&s_mux);
                ok = !(action & LIVE_TURN_END_OLD) || send_text(client, "{\"realtimeInput\":{\"activityEnd\":{}}}");
                if (ok && (action & LIVE_TURN_START)) ok = send_text(client, "{\"realtimeInput\":{\"activityStart\":{}}}");
            }
            else if (frame.kind == 2) {
                portENTER_CRITICAL(&s_mux);
                if (frame.epoch != s_epoch || frame.generation != s_generation) {
                    portEXIT_CRITICAL(&s_mux); continue;
                }
                s_end_us = esp_timer_get_time(); live_turn_end(&s_turn);
                portEXIT_CRITICAL(&s_mux);
                response_deadline = esp_timer_get_time() + 60000000;
                ok = send_text(client, "{\"realtimeInput\":{\"activityEnd\":{}}}");
            } else if (frame.kind == 5) {
                ok = run_tool(client,&frame);
            } else if (frame.kind == 3) {
                ok = send_text(client, (char *)frame.pcm);
            } else {
                size_t samples = collect_audio(&frame, pcm);
                size_t length;
                int err = mbedtls_base64_encode((unsigned char *)encoded, ENCODED_CAPACITY, &length,
                                               (unsigned char *)pcm, samples * sizeof(*pcm));
                if (err || length >= ENCODED_CAPACITY) { fail(epoch, generation); break; }
                format_audio(body, encoded, length);
                ok = send_text(client, body);
            }
            if (!ok) { fail(epoch, generation); break; }
        }
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
        free(connection.message);
        if (hisu_network_set_performance(false) != ESP_OK) ESP_LOGW(TAG, "Cannot restore Wi-Fi power saving");
    }
}
static esp_err_t speaker_open(void)
{
    if (s_speaker) return ESP_OK;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    /* Do not replay old DMA samples when streamed audio arrives late. */
    channel.auto_clear_after_cb = true;
    esp_err_t err = i2s_new_channel(&channel, &s_speaker, NULL);
    if (err != ESP_OK) return err;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(24000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = SPK_BCLK, .ws = SPK_LRCK, .dout = SPK_DOUT, .din = I2S_GPIO_UNUSED},
    };
    err = i2s_channel_init_std_mode(s_speaker, &config);
    if (err == ESP_OK) err = i2s_channel_enable(s_speaker);
    if (err != ESP_OK) { i2s_del_channel(s_speaker); s_speaker = NULL; }
    return err;
}
static void speaker_close(void)
{
    if (s_speaker) { i2s_channel_disable(s_speaker); i2s_del_channel(s_speaker); s_speaker = NULL; }
}
static bool speaker_write(const int16_t *pcm, size_t bytes)
{
    size_t written = 0;
    /* I2S takes milliseconds, unlike FreeRTOS queue and WebSocket APIs. */
    esp_err_t err = s_speaker ? i2s_channel_write(s_speaker, pcm, bytes, &written, 100) : ESP_ERR_INVALID_STATE;
    if (err != ESP_OK || written != bytes) {
        ESP_LOGW(TAG, "Speaker write failed: %s, bytes=%u/%u", esp_err_to_name(err),
                 (unsigned)written, (unsigned)bytes);
        return false;
    }
    return true;
}
static void playback_task(void *arg)
{
    (void)arg;
    output_t audio;
    bool first = true;
    uint32_t playback_epoch = 0, playback_generation = 0;
    int16_t stereo[512];
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        uint32_t epoch = s_epoch, generation = s_generation;
        bool active = s_active && !s_failed;
        portEXIT_CRITICAL(&s_mux);
        if (!active || epoch != playback_epoch || generation != playback_generation) {
            speaker_close(); first = true;
            playback_epoch = epoch; playback_generation = generation;
        }
        if (xQueueReceive(s_output, &audio, pdMS_TO_TICKS(20)) != pdTRUE) {
            portENTER_CRITICAL(&s_mux); bool active = s_active && !s_failed; portEXIT_CRITICAL(&s_mux);
            if (!active) { speaker_close(); first = true; }
            continue;
        }
        if (audio.bytes) { portENTER_CRITICAL(&s_mux); s_queued_audio_bytes -= audio.bytes; portEXIT_CRITICAL(&s_mux); }
        if (!current(audio.epoch, audio.generation)) { free(audio.pcm); speaker_close(); first = true; continue; }
        if (audio.done) {
            if (s_speaker) vTaskDelay(pdMS_TO_TICKS(100));
            speaker_close(); first = true;
            if (current(audio.epoch, audio.generation)) s_callback(LIVE_DONE, audio.generation, "");
            continue;
        }
        if (first) {
            portENTER_CRITICAL(&s_mux); int64_t ended_us = s_end_us; portEXIT_CRITICAL(&s_mux);
            ESP_LOGI(TAG, "First audio: %lld ms after activityEnd", (long long)((esp_timer_get_time() - ended_us) / 1000));
            s_callback(LIVE_AUDIO, audio.generation, ""); first = false;
        }
        if (hisu_settings_get(HISU_SETTING_SPEAKER) == 1) {
            esp_err_t speaker_err = speaker_open();
            if (speaker_err != ESP_OK) {
                ESP_LOGW(TAG, "Speaker open failed: %s", esp_err_to_name(speaker_err));
                fail(audio.epoch, audio.generation);
            }
            size_t offset = 0;
            while (offset < audio.bytes / 2 && current(audio.epoch, audio.generation)) {
                size_t count = audio.bytes / 2 - offset; if (count > 256) count = 256;
                int volume = hisu_settings_get(HISU_SETTING_VOLUME);
                for (size_t i = 0; i < count; ++i) stereo[2*i] = stereo[2*i+1] = (int32_t)audio.pcm[offset+i] * volume / 100;
                if (!speaker_write(stereo, count * 4)) {
                    fail(audio.epoch, audio.generation); break;
                }
                offset += count;
            }
        }
        free(audio.pcm);
    }
}
esp_err_t voice_live_init(live_event_fn callback)
{
    s_callback = callback;
    s_send_storage = heap_caps_malloc(SEND_SAMPLES * sizeof(int16_t) + ENCODED_CAPACITY + SEND_BODY_CAPACITY,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_input_storage = heap_caps_malloc(INPUT_QUEUE_FRAMES * sizeof(input_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_input_storage || !s_send_storage) { voice_live_deinit(); return ESP_ERR_NO_MEM; }
    s_input = xQueueCreateStatic(INPUT_QUEUE_FRAMES, sizeof(input_t), s_input_storage, &s_input_control);
    s_output = xQueueCreate(48, sizeof(output_t));
    if (!s_input || !s_output) { voice_live_deinit(); return ESP_ERR_NO_MEM; }
    if (xTaskCreate(transport_task, "live_send", 6144, NULL, 4, &s_transport_task) != pdPASS) {
        voice_live_deinit(); return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(playback_task, "live_play", 4096, NULL, 4, &s_playback_task) != pdPASS) {
        voice_live_deinit(); return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
/* Startup rollback only, before microphone/detection tasks can open a session. */
void voice_live_deinit(void)
{
    if (s_transport_task) { vTaskDelete(s_transport_task); s_transport_task = NULL; }
    if (s_playback_task) { vTaskDelete(s_playback_task); s_playback_task = NULL; }
    if (s_input) { vQueueDelete(s_input); s_input = NULL; }
    if (s_output) { vQueueDelete(s_output); s_output = NULL; }
    free(s_input_storage); s_input_storage = NULL;
    free(s_send_storage); s_send_storage = NULL;
}
