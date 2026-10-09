#include "hisu_voice_live.h"
#include "hisu_voice_live_message.h"
#include "hisu_settings.h"
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
#define ENCODED_CAPACITY 1720
#define SEND_BODY_CAPACITY 1900
typedef struct { uint32_t epoch, generation; unsigned kind; size_t count; int16_t pcm[INPUT_SAMPLES]; } input_t;
typedef struct { uint32_t epoch, generation; int16_t *pcm; size_t bytes; bool done; } output_t;
static const char *TAG = "LIVE";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_epoch, s_generation;
static bool s_active, s_ready, s_failed;
static QueueHandle_t s_input, s_output;
static StaticQueue_t s_input_control;
static uint8_t *s_input_storage;
static char *s_send_storage;
static TaskHandle_t s_transport_task, s_playback_task;
static live_event_fn s_callback;
static i2s_chan_handle_t s_speaker;
static int64_t s_end_us;
static size_t s_queued_audio_bytes;

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
    if (notify) { s_failed = true; s_ready = false; }
    portEXIT_CRITICAL(&s_mux);
    if (notify) s_callback(LIVE_ERROR, generation, "Gemini Live bị ngắt, gọi Hi Su để thử lại");
}
void voice_live_open(uint32_t generation)
{
    portENTER_CRITICAL(&s_mux);
    ++s_epoch; s_generation = generation; s_active = true; s_ready = s_failed = false;
    portEXIT_CRITICAL(&s_mux);
}
void voice_live_close(void)
{
    portENTER_CRITICAL(&s_mux);
    ++s_epoch; s_active = s_ready = false;
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
bool voice_live_begin(uint32_t generation)
{
    portENTER_CRITICAL(&s_mux);
    s_generation = generation;
    portEXIT_CRITICAL(&s_mux);
    return enqueue(generation, 1, NULL, 0);
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
        s_callback(LIVE_READY, generation, "Mình đang nghe...");
    } else if (cJSON_GetObjectItem(json, "toolCall")) {
        cJSON *calls = cJSON_GetObjectItem(cJSON_GetObjectItem(json, "toolCall"), "functionCalls");
        cJSON *call;
        cJSON_ArrayForEach(call, calls) {
            cJSON *name = cJSON_GetObjectItem(call, "name"), *id = cJSON_GetObjectItem(call, "id");
            cJSON *enabled = cJSON_GetObjectItem(cJSON_GetObjectItem(call, "args"), "enabled");
            if (!cJSON_IsString(name) || !cJSON_IsString(id) || strcmp(name->valuestring, "set_flashlight") || !cJSON_IsBool(enabled)) {
                fail(connection->epoch, generation); break;
            }
            s_callback(LIVE_FLASHLIGHT, generation, cJSON_IsTrue(enabled) ? "on" : "off");
            cJSON *response = cJSON_CreateObject();
            cJSON *tool = cJSON_AddObjectToObject(response, "toolResponse");
            cJSON *results = cJSON_AddArrayToObject(tool, "functionResponses");
            cJSON *result = cJSON_CreateObject();
            if (!response || !tool || !results || !result || !cJSON_AddItemToArray(results, result)) {
                cJSON_Delete(result); cJSON_Delete(response); fail(connection->epoch, generation); break;
            }
            cJSON_AddStringToObject(result, "id", id->valuestring);
            cJSON_AddStringToObject(result, "name", "set_flashlight");
            cJSON *value = cJSON_AddObjectToObject(result, "response");
            cJSON_AddBoolToObject(value, "enabled", cJSON_IsTrue(enabled));
            char *body = cJSON_PrintUnformatted(response);
            cJSON_Delete(response);
            input_t frame = {.epoch = connection->epoch, .generation = generation, .kind = 3};
            if (!body || strlen(body) >= sizeof(frame.pcm)) {
                cJSON_free(body); fail(connection->epoch, generation); break;
            }
            strcpy((char *)frame.pcm, body); cJSON_free(body);
            if (xQueueSend(s_input, &frame, 0) != pdTRUE) { fail(connection->epoch, generation); break; }
        }
    } else {
        cJSON *content = cJSON_GetObjectItem(json, "serverContent");
        cJSON *input = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "inputTranscription"), "text");
        if (cJSON_IsString(input)) s_callback(LIVE_INPUT, generation, input->valuestring);
        cJSON *text = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "outputTranscription"), "text");
        if (cJSON_IsString(text)) s_callback(LIVE_TEXT, generation, text->valuestring);
        cJSON *parts = cJSON_GetObjectItem(cJSON_GetObjectItem(content, "modelTurn"), "parts");
        cJSON *part;
        cJSON_ArrayForEach(part, parts) {
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
static void transport_task(void *arg)
{
    (void)arg;
    input_t frame;
    char *encoded = s_send_storage, *body = s_send_storage + ENCODED_CAPACITY;
    const char *setup = "{\"setup\":{\"model\":\"models/gemini-3.8-live\",\"generationConfig\":{\"responseModalities\":[\"AUDIO\"]},\"systemInstruction\":{\"parts\":[{\"text\":\"Bạn là Hisu. Trả lời bằng tiếng Việt, ngắn gọn và tự nhiên. Không tự nói khi người dùng chưa hỏi.\"}]},\"realtimeInputConfig\":{\"automaticActivityDetection\":{\"disabled\":true}},\"inputAudioTranscription\":{},\"outputAudioTranscription\":{},\"tools\":[{\"functionDeclarations\":[{\"name\":\"set_flashlight\",\"description\":\"Bật hoặc tắt đèn pin của Hisu theo yêu cầu người dùng\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"enabled\":{\"type\":\"BOOLEAN\"}},\"required\":[\"enabled\"]}}]}]}}";
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
        esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event, &connection);
        if (esp_websocket_client_start(client) != ESP_OK) fail(epoch, generation);
        bool sent_setup = false;
        int64_t response_deadline = 0;
        int64_t started = esp_timer_get_time();
        while (true) {
            portENTER_CRITICAL(&s_mux);
            active = s_active && !s_failed && epoch == s_epoch;
            generation = s_generation;
            portEXIT_CRITICAL(&s_mux);
            if (!active) break;
            if (response_deadline && esp_timer_get_time() >= response_deadline) {
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
            if (frame.kind == 1) {
                response_deadline = 0;
                ok = send_text(client, "{\"realtimeInput\":{\"activityStart\":{}}}");
            }
            else if (frame.kind == 2) {
                portENTER_CRITICAL(&s_mux); s_end_us = esp_timer_get_time(); portEXIT_CRITICAL(&s_mux);
                response_deadline = esp_timer_get_time() + 60000000;
                ok = send_text(client, "{\"realtimeInput\":{\"activityEnd\":{}}}");
            } else if (frame.kind == 3) {
                ok = send_text(client, (char *)frame.pcm);
            } else {
                size_t length;
                int err = mbedtls_base64_encode((unsigned char *)encoded, ENCODED_CAPACITY, &length,
                                               (unsigned char *)frame.pcm, frame.count * 2);
                if (err) { fail(epoch, generation); break; }
                encoded[length] = 0;
                snprintf(body, SEND_BODY_CAPACITY, "{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"%s\"}}}", encoded);
                ok = send_text(client, body);
            }
            if (!ok) { fail(epoch, generation); break; }
        }
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
        free(connection.message);
    }
}
static esp_err_t speaker_open(void)
{
    if (s_speaker) return ESP_OK;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
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
static void playback_task(void *arg)
{
    (void)arg;
    output_t audio;
    bool first = true;
    int16_t stereo[512];
    for (;;) {
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
            if (speaker_open() != ESP_OK) fail(audio.epoch, audio.generation);
            size_t offset = 0;
            while (offset < audio.bytes / 2 && current(audio.epoch, audio.generation)) {
                size_t count = audio.bytes / 2 - offset; if (count > 256) count = 256;
                int volume = hisu_settings_get(HISU_SETTING_VOLUME);
                for (size_t i = 0; i < count; ++i) stereo[2*i] = stereo[2*i+1] = (int32_t)audio.pcm[offset+i] * volume / 100;
                size_t written = 0;
                if (!s_speaker || i2s_channel_write(s_speaker, stereo, count * 4, &written, pdMS_TO_TICKS(100)) != ESP_OK || written != count * 4) {
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
    s_send_storage = heap_caps_malloc(ENCODED_CAPACITY + SEND_BODY_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_input_storage = heap_caps_malloc(24 * sizeof(input_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_input_storage || !s_send_storage) { voice_live_deinit(); return ESP_ERR_NO_MEM; }
    s_input = xQueueCreateStatic(24, sizeof(input_t), s_input_storage, &s_input_control);
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
