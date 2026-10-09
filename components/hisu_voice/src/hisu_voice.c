#include "hisu_voice.h"
#include "hisu_voice_session.h"
#include "hisu_voice_sse.h"
#include "hisu_voice_protocol.h"
#include "hisu_ui.h"
#include "hisu_network.h"
#include "hisu_settings.h"
#include "board_config.h"
#include "driver/i2s_std.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "mbedtls/base64.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "model_path.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *TAG = "VOICE";
static i2s_chan_handle_t s_rx;
static i2s_chan_handle_t s_tx;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t *s_afe_data;
static esp_mn_iface_t *s_multinet;
static model_iface_data_t *s_multinet_data;
static QueueHandle_t s_asr_queue;
static SemaphoreHandle_t s_lock;
static voice_session_t s_session;
static int s_http_socket = -1;
/* Only the ASR worker owns request generation and conversation history. */
static uint32_t s_worker_generation;
static int64_t s_turn_started_ms;
#define HISTORY_PAIRS 6
typedef struct { char user[256]; char reply[256]; } history_pair_t;
static history_pair_t s_history[HISTORY_PAIRS];
static unsigned s_history_count;
static uint32_t s_history_session;
#define ASR_SAMPLE_RATE 16000
#define ASR_MAX_SAMPLES (ASR_SAMPLE_RATE * 12)
#define ASR_MIN_SAMPLES (ASR_SAMPLE_RATE / 2)
#define ASR_SILENCE_SAMPLES (ASR_SAMPLE_RATE * 11 / 10)

typedef struct { int16_t *samples; size_t count; uint32_t generation; uint32_t session_id; } asr_request_t;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static bool worker_current(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool current = voice_session_current(&s_session, s_worker_generation);
    xSemaphoreGive(s_lock);
    return current;
}

static void worker_status(hisu_ui_status_t status, const char *text)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (voice_session_current(&s_session, s_worker_generation))
        (void)hisu_ui_post_status(status, text);
    xSemaphoreGive(s_lock);
}

static void cancel_socket_locked(void)
{
    /* Shutdown wakes the HTTP owner without freeing or mutating its client. */
    if (s_http_socket >= 0) (void)shutdown(s_http_socket, SHUT_RDWR);
}

bool hisu_voice_control(bool end_session)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool active = s_session.state != VOICE_IDLE;
    if (active) {
        if (end_session) voice_session_close(&s_session);
        else voice_session_open(&s_session, now_ms());
        cancel_socket_locked();
        (void)hisu_ui_post_status(end_session ? HISU_UI_IDLE : HISU_UI_LISTENING,
                                  end_session ? "" : "Mình đang nghe...");
    }
    xSemaphoreGive(s_lock);
    return active;
}

static bool register_socket(esp_http_client_handle_t client)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool current = voice_session_current(&s_session, s_worker_generation);
    if (current) s_http_socket = esp_http_client_get_socket(client);
    xSemaphoreGive(s_lock);
    return current;
}

static void cleanup_http(esp_http_client_handle_t client)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_http_socket = -1;
    xSemaphoreGive(s_lock);
    esp_http_client_cleanup(client);
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_CONNECTED && !register_socket(event->client)) {
        int socket = esp_http_client_get_socket(event->client);
        if (socket >= 0) (void)shutdown(socket, SHUT_RDWR);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t write_all(esp_http_client_handle_t client, const void *data, size_t length)
{
    const char *bytes = data;
    while (length) {
        if (!worker_current()) return ESP_ERR_INVALID_STATE;
        int sent = esp_http_client_write(client, bytes, length > 4096 ? 4096 : length);
        if (sent <= 0) return ESP_FAIL;
        bytes += sent;
        length -= sent;
    }
    return ESP_OK;
}

static void wav_header(uint8_t out[44], size_t samples)
{
    uint32_t bytes = samples * 2;
    memcpy(out, "RIFF", 4);
    out[4] = (bytes + 36) & 255; out[5] = (bytes + 36) >> 8;
    out[6] = (bytes + 36) >> 16; out[7] = (bytes + 36) >> 24;
    memcpy(out + 8, "WAVEfmt ", 8);
    out[16] = 16; out[17] = out[18] = out[19] = 0;
    out[20] = 1; out[21] = 0; out[22] = 1; out[23] = 0;
    out[24] = 0x80; out[25] = 0x3e; out[26] = out[27] = 0;
    out[28] = 0; out[29] = 0x7d; out[30] = out[31] = 0;
    out[32] = 2; out[33] = 0; out[34] = 16; out[35] = 0;
    memcpy(out + 36, "data", 4);
    out[40] = bytes & 255; out[41] = bytes >> 8;
    out[42] = bytes >> 16; out[43] = bytes >> 24;
}

static esp_err_t transcribe(const asr_request_t *request, char *text, size_t text_size, int *http_status)
{
    char key[128];
    esp_err_t err = hisu_settings_get_asr_key(key);
    if (err != ESP_OK) return err;
    char authorization[136];
    snprintf(authorization, sizeof(authorization), "Bearer %s", key);
    memset(key, 0, sizeof(key));
    static const char boundary[] = "HisuVoiceBoundary9d8c";
    char prefix[512];
    int prefix_size = snprintf(prefix, sizeof(prefix),
        "--%s\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-large-v3\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nvi\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"prompt\"\r\n\r\nHội thoại tiếng Việt với trợ lý Hisu.\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"command.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n", boundary, boundary, boundary, boundary);
    char suffix[48];
    int suffix_size = snprintf(suffix, sizeof(suffix), "\r\n--%s--\r\n", boundary);
    if (prefix_size < 0 || prefix_size >= sizeof(prefix) || suffix_size < 0 || suffix_size >= sizeof(suffix)) {
        memset(authorization, 0, sizeof(authorization));
        return ESP_ERR_INVALID_SIZE;
    }
    esp_http_client_config_t config = {
        .url = "https://api.groq.com/openai/v1/audio/transcriptions",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { memset(authorization, 0, sizeof(authorization)); return ESP_ERR_NO_MEM; }
    char type[80];
    snprintf(type, sizeof(type), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_header(client, "Content-Type", type);
    memset(authorization, 0, sizeof(authorization));
    size_t audio_bytes = request->count * sizeof(int16_t);
    size_t total = prefix_size + 44 + audio_bytes + suffix_size;
    err = esp_http_client_open(client, total);
    if (err == ESP_OK && !register_socket(client)) err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) err = write_all(client, prefix, prefix_size);
    uint8_t header[44]; wav_header(header, request->count);
    if (err == ESP_OK) err = write_all(client, header, sizeof(header));
    if (err == ESP_OK) err = write_all(client, request->samples, audio_bytes);
    if (err == ESP_OK) err = write_all(client, suffix, suffix_size);
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        *http_status = esp_http_client_get_status_code(client);
        char response[2048]; size_t used = 0;
        while (used < sizeof(response) - 1) {
            if (!worker_current()) { err = ESP_ERR_INVALID_STATE; break; }
            int got = esp_http_client_read(client, response + used, sizeof(response) - used - 1);
            if (got < 0) { err = ESP_FAIL; break; }
            if (!got) break;
            used += got;
        }
        response[used] = 0;
        if (err == ESP_OK && *http_status == 200) {
            cJSON *json = cJSON_Parse(response);
            cJSON *item = cJSON_GetObjectItemCaseSensitive(json, "text");
            if (cJSON_IsString(item) && item->valuestring[0] && strlen(item->valuestring) < text_size)
                strcpy(text, item->valuestring);
            else err = ESP_ERR_INVALID_RESPONSE;
            cJSON_Delete(json);
        }
    }
    cleanup_http(client);
    return err;
}

static int flashlight_intent(const char *utterance)
{
    char lower[256];
    size_t length = strlen(utterance);
    if (length >= sizeof(lower)) return 0;
    for (size_t i = 0; i <= length; ++i) lower[i] = tolower((unsigned char)utterance[i]);
    bool light = strstr(lower, "đèn") || strstr(lower, "den") || strstr(lower, "flashlight");
    bool on = strstr(lower, "bật") || strstr(lower, "bat") ||
              (strstr(lower, "bắt đèn") != NULL) ||
              strstr(lower, "mở") || strstr(lower, "mo ");
    bool off = strstr(lower, "tắt") || strstr(lower, "tat") || strstr(lower, "đóng") || strstr(lower, "dong");
    if (!light || on == off) return 0;
    return on ? 1 : -1;
}

typedef esp_err_t (*gemini_chunk_fn)(cJSON *event, void *context);
typedef struct { gemini_chunk_fn chunk; void *context; esp_err_t error; } gemini_stream_t;

static bool gemini_event(const char *data, size_t length, void *context)
{
    gemini_stream_t *stream = context;
    if (!worker_current()) { stream->error = ESP_ERR_INVALID_STATE; return false; }
    if (length == 6 && memcmp(data, "[DONE]", 6) == 0) return true;
    cJSON *json = cJSON_ParseWithLength(data, length);
    if (!json) stream->error = ESP_ERR_INVALID_RESPONSE;
    else if (cJSON_GetObjectItemCaseSensitive(json, "error")) stream->error = ESP_FAIL;
    else stream->error = stream->chunk(json, stream->context);
    cJSON_Delete(json);
    return stream->error == ESP_OK;
}

static esp_err_t gemini_stream(const char *url, const char *body, size_t event_capacity,
                               gemini_chunk_fn chunk, void *context, int *http_status)
{
    char key[128];
    esp_err_t err = hisu_settings_get_gemini_key(key);
    if (err != ESP_OK) return err;
    esp_http_client_config_t config = {
        .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 20000,
        .crt_bundle_attach = esp_crt_bundle_attach, .buffer_size = 1024,
        .event_handler = http_event,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { memset(key, 0, sizeof(key)); return ESP_ERR_NO_MEM; }
    esp_http_client_set_header(client, "x-goog-api-key", key);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "text/event-stream");
    memset(key, 0, sizeof(key));
    err = esp_http_client_open(client, strlen(body));
    if (err == ESP_OK && !register_socket(client)) err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) err = write_all(client, body, strlen(body));
    char *buffers = NULL;
    if (err == ESP_OK) {
        int64_t headers = esp_http_client_fetch_headers(client);
        *http_status = esp_http_client_get_status_code(client);
        if (headers < 0 || *http_status <= 0) err = ESP_ERR_TIMEOUT;
        else if (*http_status == 200) {
            buffers = heap_caps_malloc(event_capacity * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buffers) err = ESP_ERR_NO_MEM;
        }
    }
    if (err == ESP_OK && *http_status == 200) {
        gemini_stream_t stream = { .chunk = chunk, .context = context, .error = ESP_OK };
        voice_sse_t parser;
        voice_sse_init(&parser, buffers, buffers + event_capacity, event_capacity, gemini_event, &stream);
        char bytes[1024];
        int64_t started = now_ms();
        while (err == ESP_OK) {
            if (!worker_current()) { err = ESP_ERR_INVALID_STATE; break; }
            if (now_ms() - started > 60000) { err = ESP_ERR_TIMEOUT; break; }
            int got = esp_http_client_read(client, bytes, sizeof(bytes));
            if (got < 0) { err = ESP_FAIL; break; }
            if (!got) {
                if (!esp_http_client_is_complete_data_received(client) || !voice_sse_complete(&parser))
                    err = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            if (!voice_sse_feed(&parser, bytes, got))
                err = stream.error != ESP_OK ? stream.error : ESP_ERR_INVALID_SIZE;
        }
    }
    free(buffers);
    cleanup_http(client);
    ESP_LOGI(TAG, "Gemini stream: HTTP %d, %s", *http_status, esp_err_to_name(err));
    return err;
}

static const char *gemini_error_message(int status)
{
    if (status == 503 || status == 502 || status == 504) return "Gemini đang bận, thử lại sau";
    if (status == 429) return "Gemini hết hạn mức API";
    if (status == 401 || status == 403) return "Kiểm tra Gemini API key";
    if (status == 404) return "Gemini chưa hỗ trợ model này";
    return "Không nhận được trả lời Gemini";
}

static bool add_turn(cJSON *contents, const char *role, const char *text)
{
    cJSON *turn = cJSON_CreateObject();
    if (!turn) return false;
    if (!cJSON_AddItemToArray(contents, turn)) { cJSON_Delete(turn); return false; }
    cJSON *parts = cJSON_AddArrayToObject(turn, "parts");
    cJSON *part = parts ? cJSON_CreateObject() : NULL;
    if (!part) return false;
    if (!cJSON_AddItemToArray(parts, part)) { cJSON_Delete(part); return false; }
    return cJSON_AddStringToObject(turn, "role", role) && cJSON_AddStringToObject(part, "text", text);
}

static esp_err_t reply_chunk(cJSON *response, void *context)
{
    voice_reply_t *reply = context;
    bool first = !reply->text[0];
    if (!voice_reply_append(reply, response)) return ESP_ERR_INVALID_RESPONSE;
    if (first && reply->text[0]) ESP_LOGI(TAG, "First reply text: %lld ms after capture", (long long)(now_ms() - s_turn_started_ms));
    if (reply->text[0]) worker_status(HISU_UI_THINKING, reply->text);
    return ESP_OK;
}

static esp_err_t gemini_reply(const char *utterance, char reply[256], int *http_status)
{
    cJSON *request = cJSON_CreateObject();
    cJSON *contents = cJSON_AddArrayToObject(request, "contents");
    bool ok = request && contents;
    for (unsigned i = 0; ok && i < s_history_count; ++i)
        ok = add_turn(contents, "user", s_history[i].user) && add_turn(contents, "model", s_history[i].reply);
    if (ok) ok = add_turn(contents, "user", utterance);
    if (!ok) { cJSON_Delete(request); return ESP_ERR_NO_MEM; }
    cJSON *system = cJSON_AddObjectToObject(request, "systemInstruction");
    cJSON *system_parts = cJSON_AddArrayToObject(system, "parts");
    cJSON *instruction = cJSON_CreateObject();
    ok = system && system_parts && instruction &&
         cJSON_AddStringToObject(instruction, "text",
             "Bạn là Hisu, trợ lý trên thiết bị nhỏ. Trả lời bằng tiếng Việt tự nhiên, tối đa 25 từ. Không tự nhận đã thực hiện hành động trên thiết bị.") &&
         cJSON_AddItemToArray(system_parts, instruction);
    if (!ok) { cJSON_Delete(request); return ESP_ERR_NO_MEM; }
    cJSON *generation = cJSON_AddObjectToObject(request, "generationConfig");
    cJSON *thinking = cJSON_AddObjectToObject(generation, "thinkingConfig");
    if (!generation || !thinking || !cJSON_AddStringToObject(thinking, "thinkingLevel", "low") ||
        !cJSON_AddNumberToObject(generation, "maxOutputTokens", 1024)) {
        cJSON_Delete(request); return ESP_ERR_NO_MEM;
    }
    char *body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (!body) return ESP_ERR_NO_MEM;
    voice_reply_t stream_reply = { .text = reply };
    esp_err_t err = gemini_stream(
        "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.8-flash:streamGenerateContent?alt=sse",
        body, 16384, reply_chunk, &stream_reply, http_status);
    cJSON_free(body);
    if (err == ESP_OK && *http_status == 200) {
        if (!reply[0] || !stream_reply.complete) err = ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

static esp_err_t speaker_init(void)
{
    if (s_tx) return ESP_OK;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&channel, &s_tx, NULL);
    if (err != ESP_OK) return err;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(24000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = SPK_BCLK, .ws = SPK_LRCK,
            .dout = SPK_DOUT, .din = I2S_GPIO_UNUSED,
        },
    };
    err = i2s_channel_init_std_mode(s_tx, &config);
    if (err == ESP_OK) err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) { i2s_del_channel(s_tx); s_tx = NULL; }
    return err;
}

static esp_err_t speaker_play(const int16_t *pcm, size_t samples)
{
    if (hisu_settings_get(HISU_SETTING_SPEAKER) != 1) return ESP_OK;
    esp_err_t err = speaker_init();
    if (err != ESP_OK) return err;
    int volume = hisu_settings_get(HISU_SETTING_VOLUME);
    ESP_LOGI(TAG, "Speaker playback: %u samples at 24000 Hz, volume %d", (unsigned)samples, volume);
    int16_t stereo[512 * 2];
    while (samples) {
        if (!worker_current()) return ESP_ERR_INVALID_STATE;
        size_t count = samples < 512 ? samples : 512;
        for (size_t i = 0; i < count; ++i) {
            int16_t value = (int32_t)pcm[i] * volume / 100;
            stereo[i * 2] = stereo[i * 2 + 1] = value;
        }
        size_t written = 0;
        err = i2s_channel_write(s_tx, stereo, count * 4, &written, pdMS_TO_TICKS(100));
        if (err != ESP_OK || written != count * 4) return ESP_FAIL;
        pcm += count; samples -= count;
    }
    return ESP_OK;
}

typedef struct { const char *reply; bool received; bool complete; } speech_stream_t;

static esp_err_t speech_chunk(cJSON *event, void *context)
{
    speech_stream_t *speech = context;
    const char *encoded = NULL;
    voice_audio_event_t kind = voice_audio_event(event, &encoded);
    if (kind == VOICE_AUDIO_ERROR) return ESP_ERR_INVALID_RESPONSE;
    if (kind == VOICE_AUDIO_DONE) { speech->complete = true; return ESP_OK; }
    if (kind == VOICE_AUDIO_IGNORE) return ESP_OK;
    size_t encoded_size = strlen(encoded), decoded_size = 0;
    if (!encoded_size) return ESP_OK;
    int16_t *pcm = heap_caps_malloc(encoded_size * 3 / 4 + 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm) return ESP_ERR_NO_MEM;
    int decode = mbedtls_base64_decode((unsigned char *)pcm, encoded_size * 3 / 4 + 4,
                                       &decoded_size, (const unsigned char *)encoded, encoded_size);
    esp_err_t err = decode || decoded_size % 2 ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
    if (err == ESP_OK && worker_current()) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (voice_session_current(&s_session, s_worker_generation)) s_session.state = VOICE_SPEAKING;
        xSemaphoreGive(s_lock);
        if (!speech->received) {
            ESP_LOGI(TAG, "First speech audio: %lld ms after capture", (long long)(now_ms() - s_turn_started_ms));
            worker_status(HISU_UI_SPEAKING, speech->reply);
        }
        speech->received = true;
        err = speaker_play(pcm, decoded_size / 2);
    }
    free(pcm);
    return err;
}

static void speaker_stop(void)
{
    if (!s_tx) return;
    /* Normal completion drains DMA; cancellation discards queued samples. */
    for (int i = 0; i < 10 && worker_current(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    i2s_channel_disable(s_tx);
    i2s_del_channel(s_tx);
    s_tx = NULL;
}

static esp_err_t gemini_speak(const char *reply, int *http_status)
{
    cJSON *request = cJSON_CreateObject();
    cJSON *format = cJSON_AddObjectToObject(request, "response_format");
    cJSON *generation = cJSON_AddObjectToObject(request, "generation_config");
    cJSON *voices = cJSON_AddArrayToObject(generation, "speech_config");
    cJSON *voice = voices ? cJSON_CreateObject() : NULL;
    if (voice && !cJSON_AddItemToArray(voices, voice)) { cJSON_Delete(voice); voice = NULL; }
    bool ok = request && format && generation && voices && voice &&
         cJSON_AddStringToObject(request, "model", "gemini-3.8-flash-tts") &&
         cJSON_AddStringToObject(request, "input", reply) &&
         cJSON_AddBoolToObject(request, "stream", true) &&
         cJSON_AddBoolToObject(request, "store", false) &&
         cJSON_AddStringToObject(format, "type", "audio") &&
         cJSON_AddStringToObject(format, "mime_type", "audio/l16") &&
         cJSON_AddNumberToObject(format, "sample_rate", 24000) &&
         cJSON_AddStringToObject(voice, "voice", "Kore");
    if (!ok) { cJSON_Delete(request); return ESP_ERR_NO_MEM; }
    char *body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (!body) return ESP_ERR_NO_MEM;
    speech_stream_t speech = { .reply = reply };
    esp_err_t err = gemini_stream("https://generativelanguage.googleapis.com/v1beta/interactions",
                                  body, 256 * 1024, speech_chunk, &speech, http_status);
    cJSON_free(body);
    speaker_stop();
    if (err == ESP_OK && *http_status == 200 && (!speech.received || !speech.complete)) err = ESP_ERR_INVALID_RESPONSE;
    return err;
}

static void asr_task(void *arg)
{
    (void)arg;
    asr_request_t request;
    for (;;) {
        if (xQueueReceive(s_asr_queue, &request, portMAX_DELAY) != pdTRUE) continue;
        s_worker_generation = request.generation;
        if (!worker_current()) { free(request.samples); continue; }
        s_turn_started_ms = now_ms();
        if (request.session_id != s_history_session) {
            memset(s_history, 0, sizeof(s_history));
            s_history_count = 0;
            s_history_session = request.session_id;
        }
        worker_status(HISU_UI_THINKING, "Đang nhận giọng nói...");
        char text[256] = "";
        int status = 0;
        esp_err_t err = transcribe(&request, text, sizeof(text), &status);
        ESP_LOGI(TAG, "ASR captured %u samples, HTTP %d", (unsigned)request.count, status);
        free(request.samples);
        ESP_LOGI(TAG, "Transcription: %lld ms", (long long)(now_ms() - s_turn_started_ms));
        if (!worker_current()) continue;
        char reply[256] = "";
        bool speech_failed = false;
        if (err == ESP_OK && status == 200) {
            if (voice_session_end_phrase(text)) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                if (voice_session_current(&s_session, s_worker_generation)) {
                    voice_session_close(&s_session);
                    (void)hisu_ui_post_status(HISU_UI_IDLE, "");
                }
                xSemaphoreGive(s_lock);
                continue;
            }
            int intent = flashlight_intent(text);
            if (intent) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                if (voice_session_current(&s_session, s_worker_generation))
                    (void)hisu_ui_post_flashlight(intent > 0);
                xSemaphoreGive(s_lock);
                strcpy(reply, intent > 0 ? "Đã bật đèn pin." : "Đã tắt đèn pin.");
                worker_status(HISU_UI_THINKING, reply);
                ESP_LOGI(TAG, "Groq flashlight intent: %s", intent > 0 ? "on" : "off");
                if (hisu_settings_has_gemini_key() && hisu_settings_get(HISU_SETTING_SPEAKER) == 1) {
                    int tts_status = 0;
                    esp_err_t tts_err = gemini_speak(reply, &tts_status);
                    if (tts_err != ESP_OK || tts_status != 200) {
                        ESP_LOGW(TAG, "Voice confirmation failed: %s, HTTP %d", esp_err_to_name(tts_err), tts_status);
                        speech_failed = true;
                        worker_status(HISU_UI_ERROR, "Chưa tạo được giọng nói Gemini");
                    }
                }
            } else if (hisu_settings_has_gemini_key()) {
                int chat_status = 0;
                worker_status(HISU_UI_THINKING, "Đang trả lời...");
                esp_err_t chat_err = gemini_reply(text, reply, &chat_status);
                if (!worker_current()) continue;
                if (chat_err == ESP_OK && chat_status == 200) {
                    ESP_LOGI(TAG, "Gemini reply received: %u bytes; speaker=%d volume=%d",
                             (unsigned)strlen(reply), hisu_settings_get(HISU_SETTING_SPEAKER),
                             hisu_settings_get(HISU_SETTING_VOLUME));
                    worker_status(HISU_UI_THINKING, reply);
                    if (hisu_settings_get(HISU_SETTING_SPEAKER) == 1) {
                        int tts_status = 0;
                        esp_err_t tts_err = gemini_speak(reply, &tts_status);
                        if (tts_err != ESP_OK || tts_status != 200) {
                            speech_failed = true;
                            ESP_LOGW(TAG, "Gemini speech failed: %s, HTTP %d", esp_err_to_name(tts_err), tts_status);
                            worker_status(HISU_UI_ERROR, "Chưa tạo được giọng nói Gemini");
                        }
                    }
                } else {
                    reply[0] = 0;
                    worker_status(HISU_UI_ERROR, chat_err == ESP_ERR_TIMEOUT ?
                                              "Gemini phản hồi quá lâu" : gemini_error_message(chat_status));
                    ESP_LOGW(TAG, "Gemini reply failed: %s, HTTP %d", esp_err_to_name(chat_err), chat_status);
                }
            } else {
                worker_status(HISU_UI_ERROR, "Cần Gemini API key để trò chuyện");
                ESP_LOGI(TAG, "Groq returned a phrase without a flashlight command");
            }
        } else {
            ESP_LOGW(TAG, "Groq transcription failed: %s, HTTP %d", esp_err_to_name(err), status);
            const char *message = status == 401 ? "Groq API key không hợp lệ" :
                                  status == 429 ? "Groq tạm hết lượt thử" :
                                  "Không nhận được lệnh qua Wi-Fi";
            worker_status(HISU_UI_ERROR, message);
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (voice_session_current(&s_session, s_worker_generation)) {
            if (reply[0]) {
                if (s_history_count == HISTORY_PAIRS) {
                    memmove(s_history, s_history + 1, sizeof(s_history[0]) * (HISTORY_PAIRS - 1));
                    --s_history_count;
                }
                strcpy(s_history[s_history_count].user, text);
                strcpy(s_history[s_history_count++].reply, reply);
            }
            voice_session_finish(&s_session, s_worker_generation, now_ms());
            if (reply[0] && !speech_failed) (void)hisu_ui_post_status(HISU_UI_LISTENING, reply);
        }
        xSemaphoreGive(s_lock);
    }
}

static void microphone_task(void *arg)
{
    (void)arg;
    const int frames = s_afe->get_feed_chunksize(s_afe_data);
    int16_t *pcm = malloc(frames * sizeof(*pcm));
    int32_t *raw = malloc(frames * 2 * sizeof(*raw));
    if (!pcm || !raw) {
        ESP_LOGE(TAG, "No memory for wake-word audio buffers");
        (void)hisu_ui_post_status(HISU_UI_ERROR, "Không đủ RAM cho mic");
        free(pcm); free(raw);
        vTaskDelete(NULL);
    }
    uint64_t recent_energy[2] = {0};
    unsigned recent_blocks = 0;
    for (;;) {
        size_t filled = 0;
        const size_t needed = frames * 2 * sizeof(*raw);
        while (filled < needed) {
            size_t bytes = 0;
            esp_err_t err = i2s_channel_read(s_rx, (uint8_t *)raw + filled,
                                             needed - filled, &bytes, pdMS_TO_TICKS(1000));
            if (err != ESP_OK || !bytes) {
                ESP_LOGW(TAG, "Microphone read: %s", esp_err_to_name(err));
                filled = 0;
                break;
            }
            filled += bytes;
        }
        if (filled != needed) continue;
        /* I2S MEMS mic occupies one of the two 32-bit slots. Select the
         * active slot by signal energy; preserve 16 kHz mono for the AFE. */
        uint64_t energy[2] = {0};
        for (int i = 0; i < frames; ++i) {
            for (int ch = 0; ch < 2; ++ch) {
                int64_t sample = (int64_t)raw[i * 2 + ch] >> 16;
                energy[ch] += sample < 0 ? -sample : sample;
            }
        }
        int active = energy[1] > energy[0] ? 1 : 0;
        recent_energy[0] += energy[0];
        recent_energy[1] += energy[1];
        if (++recent_blocks >= 160) {
            ESP_LOGI(TAG, "Mic L=%lu R=%lu active=%c (mean abs PCM)",
                     (unsigned long)(recent_energy[0] / ((uint64_t)frames * recent_blocks)),
                     (unsigned long)(recent_energy[1] / ((uint64_t)frames * recent_blocks)),
                     active ? 'R' : 'L');
            recent_energy[0] = recent_energy[1] = 0;
            recent_blocks = 0;
        }
        /* Preserve headroom for close speech; AFE AGC raises quiet speech
         * while compressing louder input. */
        for (int i = 0; i < frames; ++i) {
            int64_t amplified = ((int64_t)raw[i * 2 + active] >> 16) * 12;
            if (amplified > INT16_MAX) amplified = INT16_MAX;
            if (amplified < INT16_MIN) amplified = INT16_MIN;
            pcm[i] = (int16_t)amplified;
        }
        s_afe->feed(s_afe_data, pcm);
    }
}

static void detection_task(void *arg)
{
    (void)arg;
    unsigned fetch_count = 0;
    int16_t *capture = NULL;
    size_t captured = 0;
    size_t silence_samples = 0;
    unsigned speech_frames = 0;
    uint32_t capture_generation = 0;
    uint32_t capture_session = 0;
    uint32_t seen_generation = 0;
    const size_t preroll_capacity = ASR_SAMPLE_RATE / 5;
    int16_t *preroll = heap_caps_malloc(preroll_capacity * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t preroll_count = 0;
    if (!preroll) {
        (void)hisu_ui_post_status(HISU_UI_ERROR, "Không đủ RAM nhận giọng nói");
        vTaskDelete(NULL); return;
    }
    int previous_vad = -1;
    for (;;) {
        afe_fetch_result_t *result = s_afe->fetch(s_afe_data);
        if (!result || result->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "Audio front-end fetch failed");
            (void)hisu_voice_control(true);
            (void)hisu_ui_post_status(HISU_UI_ERROR, "Bộ nhận giọng nói lỗi");
            free(capture); free(preroll);
            vTaskDelete(NULL);
            return;
        }
        /* MultiNet may keep CPU1 busy while Wi-Fi is connecting. Give the
         * idle task a chance to run without delaying every audio frame. */
        if (++fetch_count == 8) {
            fetch_count = 0;
            vTaskDelay(1);
        }
        if ((int)result->vad_state != previous_vad) {
            previous_vad = result->vad_state;
            ESP_LOGI(TAG, "VAD %s, volume %.1f dBFS",
                     result->vad_state == VAD_SPEECH ? "speech" : "silence",
                     result->data_volume);
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (voice_session_expire(&s_session, now_ms())) {
            cancel_socket_locked();
            (void)hisu_ui_post_status(HISU_UI_IDLE, "");
        }
        voice_session_t snapshot = s_session;
        xSemaphoreGive(s_lock);
        if (seen_generation != snapshot.generation) {
            seen_generation = snapshot.generation;
            preroll_count = 0;
            speech_frames = 0;
            s_multinet->clean(s_multinet_data);
        }
        if (capture && !voice_session_current(&snapshot, capture_generation)) {
            free(capture); capture = NULL;
        }
        if (capture) {
            size_t frame = s_multinet->get_samp_chunksize(s_multinet_data);
            if (frame > ASR_MAX_SAMPLES - captured) frame = ASR_MAX_SAMPLES - captured;
            memcpy(capture + captured, result->data, frame * sizeof(int16_t));
            captured += frame;
            /* Keep a generous silence gap for pauses between Vietnamese syllables. */
            if (result->vad_state == VAD_SPEECH) silence_samples = 0;
            else silence_samples += frame;
            if (captured >= ASR_MAX_SAMPLES ||
                (captured >= ASR_MIN_SAMPLES &&
                 silence_samples >= ASR_SILENCE_SAMPLES)) {
                ESP_LOGI(TAG, "Command capture finished after %u ms (%s)",
                         (unsigned)(captured * 1000 / ASR_SAMPLE_RATE),
                         captured >= ASR_MAX_SAMPLES ? "timeout" : "VAD silence");
                asr_request_t request = {.samples = capture, .count = captured,
                    .generation = capture_generation, .session_id = capture_session};
                capture = NULL;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                if (voice_session_current(&s_session, request.generation)) {
                    s_session.state = VOICE_PROCESSING;
                    /* Replace a cancelled queued turn; its buffer still needs freeing. */
                    asr_request_t stale;
                    if (xQueueReceive(s_asr_queue, &stale, 0) == pdTRUE) free(stale.samples);
                    if (xQueueSend(s_asr_queue, &request, 0) != pdTRUE) {
                        free(request.samples);
                        voice_session_finish(&s_session, request.generation, now_ms());
                        (void)hisu_ui_post_status(HISU_UI_ERROR, "Bộ nhận lệnh đang bận");
                    }
                } else {
                    free(request.samples);
                }
                xSemaphoreGive(s_lock);
            }
            continue;
        }
        /* Keep wake detection active during processing and playback for interruption. */
        esp_mn_state_t state = s_multinet->detect(s_multinet_data, result->data);
        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *recognized = s_multinet->get_results(s_multinet_data);
            if (recognized && recognized->num > 0)
                ESP_LOGI(TAG, "MultiNet command=%d text=%s confidence=%.2f",
                         recognized->command_id[0], recognized->string, recognized->prob[0]);
            if (recognized && recognized->num > 0 && recognized->command_id[0] == 1) {
                ESP_LOGI(TAG, "Offline Hey Hisu detected (confidence %.2f)", recognized->prob[0]);
                hisu_network_status_t network;
                hisu_network_get(&network);
                if (hisu_voice_control(false)) {
                    /* Interruption works even if connectivity was lost during the turn. */
                } else if (!network.connected) {
                    (void)hisu_ui_post_status(HISU_UI_ERROR, "Cần Wi-Fi để nghe lệnh");
                } else if (!hisu_settings_has_asr_key()) {
                    (void)hisu_ui_post_status(HISU_UI_ERROR, "Cần Groq API key trong Setting");
                } else {
                    xSemaphoreTake(s_lock, portMAX_DELAY);
                    voice_session_open(&s_session, now_ms());
                    cancel_socket_locked();
                    (void)hisu_ui_post_status(HISU_UI_LISTENING, "Mình đang nghe...");
                    xSemaphoreGive(s_lock);
                }
                s_multinet->clean(s_multinet_data);
                continue;
            }
            s_multinet->clean(s_multinet_data);
        } else if (state == ESP_MN_STATE_TIMEOUT) {
            esp_mn_results_t *candidate = s_multinet->get_results(s_multinet_data);
            if (candidate && candidate->num > 0)
                ESP_LOGI(TAG, "MultiNet timeout candidate=%s confidence=%.2f",
                         candidate->raw_string, candidate->prob[0]);
            s_multinet->clean(s_multinet_data);
        }
        if (snapshot.state != VOICE_WAITING || now_ms() < snapshot.ready_ms) continue;
        size_t frame = s_afe->get_fetch_chunksize(s_afe_data);
        if (frame > preroll_capacity) frame = preroll_capacity;
        if (preroll_count + frame > preroll_capacity) {
            size_t discard = preroll_count + frame - preroll_capacity;
            memmove(preroll, preroll + discard, (preroll_count - discard) * sizeof(int16_t));
            preroll_count -= discard;
        }
        memcpy(preroll + preroll_count, result->data, frame * sizeof(int16_t));
        preroll_count += frame;
        if (result->vad_state == VAD_SPEECH) ++speech_frames;
        else speech_frames = 0;
        if (speech_frames < 3) continue;
        capture = heap_caps_malloc(ASR_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (capture && s_session.generation == snapshot.generation && voice_session_capture(&s_session, now_ms())) {
            capture_generation = s_session.generation;
            capture_session = s_session.session_id;
            seen_generation = capture_generation;
            s_multinet->clean(s_multinet_data);
            captured = preroll_count;
            memcpy(capture, preroll, captured * sizeof(int16_t));
            silence_samples = 0;
            (void)hisu_ui_post_status(HISU_UI_LISTENING, "Đang nghe...");
        } else {
            free(capture); capture = NULL;
            if (!capture && s_session.generation == snapshot.generation) {
                voice_session_close(&s_session);
                (void)hisu_ui_post_status(HISU_UI_ERROR, "Không bắt đầu được ghi âm");
            }
        }
        xSemaphoreGive(s_lock);
    }
}

esp_err_t hisu_voice_start(void)
{
    if (s_rx) return ESP_ERR_INVALID_STATE;
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) return ESP_ERR_NOT_FOUND;
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (!mn_name) {
        ESP_LOGE(TAG, "No English MultiNet model in model partition");
        esp_srmodel_deinit(models);
        return ESP_ERR_NOT_FOUND;
    }
    s_multinet = esp_mn_handle_from_name(mn_name);
    s_multinet_data = s_multinet ? s_multinet->create(mn_name, 6000) : NULL;
    if (!s_multinet_data) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_mn_commands_alloc(s_multinet, s_multinet_data);
    /* The owner says each syllable separately. English G2P merges Hisu and
     * voices its s as /z/, so provide /s/ and syllable boundaries directly. */
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "hey hi su", "hd hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "eh hi su", "f hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "ay hi su", "d hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "he hi su", "hf hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "hello hi su", "hf Lb hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "oi hi su", "u hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "uh ai hi su", "ci hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "hi su", "hm So");
    if (err == ESP_OK) err = esp_mn_commands_phoneme_add(1, "hi su hi su", "hm So hm So");
    if (err == ESP_OK) {
        esp_mn_error_t *command_error = esp_mn_commands_update();
        if (command_error && command_error->num) err = ESP_ERR_INVALID_ARG;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MultiNet cannot recognize the custom phrase 'hey hisu'");
        return err;
    }
    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!afe_config) return ESP_ERR_NO_MEM;
    afe_config->wakenet_init = false;
    afe_config->agc_init = true;
    afe_config->agc_mode = AFE_AGC_MODE_WEBRTC;
    afe_config->agc_compression_gain_db = 18;
    afe_config->agc_target_level_dbfs = 6;
    afe_config->ns_init = true;
    afe_config->afe_ns_mode = AFE_NS_MODE_WEBRTC;
    afe_config->afe_linear_gain = 5.0f;
    s_afe = esp_afe_handle_from_config(afe_config);
    s_afe_data = s_afe ? s_afe->create_from_config(afe_config) : NULL;
    afe_config_free(afe_config);
    if (!s_afe_data) return ESP_ERR_NO_MEM;
    if (s_afe->get_fetch_chunksize(s_afe_data) != s_multinet->get_samp_chunksize(s_multinet_data)) {
        ESP_LOGE(TAG, "AFE and MultiNet frame sizes differ");
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "Offline phrase: Hey Hisu (%s)", mn_name);

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    err = i2s_new_channel(&channel, NULL, &s_rx);
    if (err != ESP_OK) return err;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_SCK,
            .ws = MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_SD,
        },
    };
    err = i2s_channel_init_std_mode(s_rx, &config);
    if (err == ESP_OK) err = i2s_channel_enable(s_rx);
    if (err == ESP_OK) {
        if (!s_lock) s_lock = xSemaphoreCreateMutex();
        if (!s_lock) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        s_asr_queue = xQueueCreate(1, sizeof(asr_request_t));
        if (!s_asr_queue) err = ESP_ERR_NO_MEM;
    }
    TaskHandle_t asr = NULL, mic = NULL, wake = NULL;
    if (err == ESP_OK && xTaskCreate(asr_task, "hisu_asr", 12288, NULL, 4, &asr) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err == ESP_OK && xTaskCreate(microphone_task, "hisu_mic", 4096, NULL, 5, &mic) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err == ESP_OK && xTaskCreate(detection_task, "hisu_wake", 6144, NULL, 5, &wake) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        if (wake) vTaskDelete(wake);
        if (mic) vTaskDelete(mic);
        if (asr) vTaskDelete(asr);
        if (s_asr_queue) { vQueueDelete(s_asr_queue); s_asr_queue = NULL; }
        (void)i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
    return err;
}
