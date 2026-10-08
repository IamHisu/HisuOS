#include "hisu_voice.h"
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
static volatile bool s_asr_busy;
#define ASR_SAMPLE_RATE 16000
#define ASR_MAX_SAMPLES (ASR_SAMPLE_RATE * 5)
#define ASR_MIN_SAMPLES (ASR_SAMPLE_RATE * 2)
#define ASR_SILENCE_SAMPLES (ASR_SAMPLE_RATE * 11 / 10)
#define ASR_WAKE_TAIL_SAMPLES (ASR_SAMPLE_RATE * 4 / 10)

typedef struct { int16_t *samples; size_t count; } asr_request_t;

static esp_err_t write_all(esp_http_client_handle_t client, const void *data, size_t length)
{
    const char *bytes = data;
    while (length) {
        int sent = esp_http_client_write(client, bytes, length);
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
        "--%s\r\nContent-Disposition: form-data; name=\"prompt\"\r\n\r\nBật đèn pin. Tắt đèn pin.\r\n"
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
            int got = esp_http_client_read(client, response + used, sizeof(response) - used - 1);
            if (got < 0) { err = ESP_FAIL; break; }
            if (!got) break;
            used += got;
        }
        response[used] = 0;
        if (err == ESP_OK && *http_status == 200) {
            cJSON *json = cJSON_Parse(response);
            cJSON *item = cJSON_GetObjectItemCaseSensitive(json, "text");
            if (cJSON_IsString(item) && strlen(item->valuestring) < text_size)
                strcpy(text, item->valuestring);
            else err = ESP_ERR_INVALID_RESPONSE;
            cJSON_Delete(json);
        }
    }
    esp_http_client_cleanup(client);
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

static esp_err_t gemini_post_once(const char *model, const char *body, size_t response_capacity,
                             cJSON **output, int *http_status)
{
    char key[128];
    esp_err_t err = hisu_settings_get_gemini_key(key);
    if (err != ESP_OK) return err;
    char url[160];
    snprintf(url, sizeof(url), "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent", model);
    esp_http_client_config_t config = {
        .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 45000,
        .crt_bundle_attach = esp_crt_bundle_attach, .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { memset(key, 0, sizeof(key)); return ESP_ERR_NO_MEM; }
    esp_http_client_set_header(client, "x-goog-api-key", key);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    memset(key, 0, sizeof(key));
    err = esp_http_client_open(client, strlen(body));
    if (err == ESP_OK) err = write_all(client, body, strlen(body));
    char *response = NULL;
    if (err == ESP_OK) {
        int64_t headers = esp_http_client_fetch_headers(client);
        *http_status = esp_http_client_get_status_code(client);
        if (headers < 0 || *http_status <= 0) err = ESP_ERR_TIMEOUT;
        else {
            response = heap_caps_malloc(response_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!response) err = ESP_ERR_NO_MEM;
        }
    }
    size_t used = 0;
    while (err == ESP_OK && used < response_capacity - 1) {
        int got = esp_http_client_read(client, response + used, response_capacity - used - 1);
        if (got < 0) { err = ESP_FAIL; break; }
        if (!got) break;
        used += got;
    }
    if (err == ESP_OK && used == response_capacity - 1) err = ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK && *http_status == 200) {
        response[used] = 0;
        *output = cJSON_ParseWithLength(response, used);
        if (!*output) err = ESP_ERR_INVALID_RESPONSE;
    }
    free(response);
    ESP_LOGI(TAG, "Gemini %s: HTTP %d, response %u bytes, %s", model,
             *http_status, (unsigned)used, esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t gemini_post(const char *model, const char *body, size_t capacity,
                             cJSON **output, int *status)
{
    esp_err_t err = gemini_post_once(model, body, capacity, output, status);
    if (err == ESP_OK && (*status == 502 || *status == 503 || *status == 504)) {
        ESP_LOGW(TAG, "Gemini temporarily unavailable (HTTP %d); retrying once", *status);
        vTaskDelay(pdMS_TO_TICKS(700));
        *status = 0;
        err = gemini_post_once(model, body, capacity, output, status);
    }
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

static const char *gemini_first_part(cJSON *response, const char *field)
{
    cJSON *candidates = cJSON_GetObjectItemCaseSensitive(response, "candidates");
    cJSON *candidate = cJSON_GetArrayItem(candidates, 0);
    cJSON *content = cJSON_GetObjectItemCaseSensitive(candidate, "content");
    cJSON *parts = cJSON_GetObjectItemCaseSensitive(content, "parts");
    cJSON *part;
    cJSON_ArrayForEach(part, parts) {
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(part, "thought"))) continue;
        cJSON *container = !strcmp(field, "data") ? cJSON_GetObjectItemCaseSensitive(part, "inlineData") : part;
        cJSON *item = cJSON_GetObjectItemCaseSensitive(container, field);
        if (cJSON_IsString(item)) return item->valuestring;
    }
    return NULL;
}

static esp_err_t gemini_reply(const char *utterance, char reply[256], int *http_status)
{
    cJSON *request = cJSON_CreateObject();
    cJSON *contents = cJSON_AddArrayToObject(request, "contents");
    cJSON *turn = cJSON_CreateObject();
    cJSON *parts = cJSON_AddArrayToObject(turn, "parts");
    cJSON *part = cJSON_CreateObject();
    bool ok = request && contents && turn && parts && part &&
              cJSON_AddStringToObject(turn, "role", "user") &&
              cJSON_AddStringToObject(part, "text", utterance) &&
              cJSON_AddItemToArray(parts, part) && cJSON_AddItemToArray(contents, turn);
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
    cJSON *response = NULL;
    esp_err_t err = gemini_post("gemini-3.8-flash", body, 8192, &response, http_status);
    cJSON_free(body);
    if (err == ESP_OK && *http_status == 200) {
        const char *text = gemini_first_part(response, "text");
        if (text && text[0] && strlen(text) < 256) strcpy(reply, text);
        else err = ESP_ERR_INVALID_RESPONSE;
    }
    cJSON_Delete(response);
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
        size_t count = samples < 512 ? samples : 512;
        for (size_t i = 0; i < count; ++i) {
            int16_t value = (int32_t)pcm[i] * volume / 100;
            stereo[i * 2] = stereo[i * 2 + 1] = value;
        }
        size_t written = 0;
        err = i2s_channel_write(s_tx, stereo, count * 4, &written, pdMS_TO_TICKS(1500));
        if (err != ESP_OK || written != count * 4) return ESP_FAIL;
        pcm += count; samples -= count;
    }
    return ESP_OK;
}

static esp_err_t gemini_speak(const char *reply, int *http_status)
{
    cJSON *request = cJSON_CreateObject();
    cJSON *contents = cJSON_AddArrayToObject(request, "contents");
    cJSON *turn = cJSON_CreateObject();
    cJSON *parts = cJSON_AddArrayToObject(turn, "parts");
    cJSON *part = cJSON_CreateObject();
    char speech[320];
    snprintf(speech, sizeof(speech), "Đọc bằng tiếng Việt: %s", reply);
    bool ok = request && contents && turn && parts && part &&
              cJSON_AddStringToObject(part, "text", speech) &&
              cJSON_AddItemToArray(parts, part) && cJSON_AddItemToArray(contents, turn);
    if (!ok) { cJSON_Delete(request); return ESP_ERR_NO_MEM; }
    cJSON *generation = cJSON_AddObjectToObject(request, "generationConfig");
    cJSON *modalities = cJSON_AddArrayToObject(generation, "responseModalities");
    cJSON *format = cJSON_AddObjectToObject(generation, "responseFormat");
    cJSON *audio = cJSON_AddObjectToObject(format, "audio");
    cJSON *speech_config = cJSON_AddObjectToObject(generation, "speechConfig");
    cJSON *voice_config = cJSON_AddObjectToObject(speech_config, "voiceConfig");
    ok = generation && modalities && format && audio && speech_config && voice_config &&
         cJSON_AddItemToArray(modalities, cJSON_CreateString("AUDIO")) &&
         cJSON_AddStringToObject(audio, "mimeType", "AUDIO_L16") &&
         cJSON_AddNumberToObject(audio, "sampleRate", 24000) &&
         cJSON_AddStringToObject(voice_config, "voice", "Kore");
    if (!ok) { cJSON_Delete(request); return ESP_ERR_NO_MEM; }
    char *body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (!body) return ESP_ERR_NO_MEM;
    cJSON *response = NULL;
    esp_err_t err = gemini_post("gemini-3.8-flash-tts", body, 512 * 1024, &response, http_status);
    cJSON_free(body);
    if (err == ESP_OK && *http_status == 200) {
        const char *encoded = gemini_first_part(response, "data");
        if (!encoded) err = ESP_ERR_INVALID_RESPONSE;
        else {
            size_t encoded_size = strlen(encoded), decoded_size = 0;
            int16_t *pcm = heap_caps_malloc(encoded_size * 3 / 4 + 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!pcm) err = ESP_ERR_NO_MEM;
            else {
                int decode = mbedtls_base64_decode((unsigned char *)pcm, encoded_size * 3 / 4 + 4,
                                                   &decoded_size, (const unsigned char *)encoded, encoded_size);
                err = decode || decoded_size % 2 ? ESP_ERR_INVALID_RESPONSE :
                      speaker_play(pcm, decoded_size / 2);
                free(pcm);
            }
        }
    }
    cJSON_Delete(response);
    return err;
}

static void asr_task(void *arg)
{
    (void)arg;
    asr_request_t request;
    for (;;) {
        if (xQueueReceive(s_asr_queue, &request, portMAX_DELAY) != pdTRUE) continue;
        (void)hisu_ui_post_status(HISU_UI_THINKING, "Đang nhận lệnh tiếng Việt...");
        char text[256] = "";
        int status = 0;
        esp_err_t err = transcribe(&request, text, sizeof(text), &status);
        ESP_LOGI(TAG, "ASR captured %u samples, HTTP %d", (unsigned)request.count, status);
        free(request.samples);
        unsigned display_ms = 4000;
        if (err == ESP_OK && status == 200) {
            int intent = flashlight_intent(text);
            if (intent) {
                (void)hisu_ui_post_flashlight(intent > 0);
                (void)hisu_ui_post_status(HISU_UI_THINKING,
                    intent > 0 ? "Đã bật đèn pin" : "Đã tắt đèn pin");
                ESP_LOGI(TAG, "Groq flashlight intent: %s", intent > 0 ? "on" : "off");
                if (hisu_settings_has_gemini_key() && hisu_settings_get(HISU_SETTING_SPEAKER) == 1) {
                    int tts_status = 0;
                    esp_err_t tts_err = gemini_speak(intent > 0 ? "Đã bật đèn pin." : "Đã tắt đèn pin.", &tts_status);
                    if (tts_err != ESP_OK || tts_status != 200)
                        ESP_LOGW(TAG, "Voice confirmation failed: %s, HTTP %d", esp_err_to_name(tts_err), tts_status);
                }
            } else if (hisu_settings_has_gemini_key()) {
                char reply[256] = "";
                int chat_status = 0;
                (void)hisu_ui_post_status(HISU_UI_THINKING, "Đang trả lời...");
                esp_err_t chat_err = gemini_reply(text, reply, &chat_status);
                if (chat_err == ESP_OK && chat_status == 200) {
                    ESP_LOGI(TAG, "Gemini reply received: %u bytes; speaker=%d volume=%d",
                             (unsigned)strlen(reply), hisu_settings_get(HISU_SETTING_SPEAKER),
                             hisu_settings_get(HISU_SETTING_VOLUME));
                    (void)hisu_ui_post_status(HISU_UI_THINKING, reply);
                    if (hisu_settings_get(HISU_SETTING_SPEAKER) == 1) {
                        int tts_status = 0;
                        esp_err_t tts_err = gemini_speak(reply, &tts_status);
                        if (tts_err != ESP_OK || tts_status != 200) {
                            ESP_LOGW(TAG, "Gemini speech failed: %s, HTTP %d", esp_err_to_name(tts_err), tts_status);
                            (void)hisu_ui_post_status(HISU_UI_ERROR, "Chưa tạo được giọng nói Gemini");
                        }
                    }
                } else {
                    (void)hisu_ui_post_status(HISU_UI_ERROR, chat_err == ESP_ERR_TIMEOUT ?
                                              "Gemini phản hồi quá lâu" : gemini_error_message(chat_status));
                    ESP_LOGW(TAG, "Gemini reply failed: %s, HTTP %d", esp_err_to_name(chat_err), chat_status);
                }
            } else {
                (void)hisu_ui_post_status(HISU_UI_THINKING, "Chưa hiểu lệnh đèn pin");
                ESP_LOGI(TAG, "Groq returned a phrase without a flashlight command");
            }
        } else {
            ESP_LOGW(TAG, "Groq transcription failed: %s, HTTP %d", esp_err_to_name(err), status);
            const char *message = status == 401 ? "Groq API key không hợp lệ" :
                                  status == 429 ? "Groq tạm hết lượt thử" :
                                  "Không nhận được lệnh qua Wi-Fi";
            (void)hisu_ui_post_status(HISU_UI_ERROR, message);
        }
        vTaskDelay(pdMS_TO_TICKS(display_ms));
        (void)hisu_ui_post_status(HISU_UI_IDLE, "");
        s_asr_busy = false;
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
    bool heard_command = false;
    int64_t feedback_until = 0;
    int previous_vad = -1;
    for (;;) {
        afe_fetch_result_t *result = s_afe->fetch(s_afe_data);
        if (!result || result->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "Audio front-end fetch failed");
            (void)hisu_ui_post_status(HISU_UI_ERROR, "Bộ nhận giọng nói lỗi");
            vTaskDelete(NULL);
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
        if (feedback_until && esp_timer_get_time() >= feedback_until) {
            (void)hisu_ui_post_status(HISU_UI_IDLE, "");
            feedback_until = 0;
        }
        if (capture) {
            size_t frame = s_multinet->get_samp_chunksize(s_multinet_data);
            if (frame > ASR_MAX_SAMPLES - captured) frame = ASR_MAX_SAMPLES - captured;
            memcpy(capture + captured, result->data, frame * sizeof(int16_t));
            captured += frame;
            /* Ignore the tail of the wake phrase, then require several VAD
             * speech frames. A full second of silence after the command lets
             * people pause between Vietnamese syllables without truncation. */
            if (captured >= ASR_WAKE_TAIL_SAMPLES) {
                if (result->vad_state == VAD_SPEECH) {
                    if (speech_frames < 3) ++speech_frames;
                    if (speech_frames == 3) heard_command = true;
                    silence_samples = 0;
                } else if (heard_command) {
                    silence_samples += frame;
                } else {
                    speech_frames = 0;
                }
            }
            if (captured >= ASR_MAX_SAMPLES ||
                (heard_command && captured >= ASR_MIN_SAMPLES &&
                 silence_samples >= ASR_SILENCE_SAMPLES)) {
                ESP_LOGI(TAG, "Command capture finished after %u ms (%s)",
                         (unsigned)(captured * 1000 / ASR_SAMPLE_RATE),
                         captured >= ASR_MAX_SAMPLES ? "timeout" : "VAD silence");
                asr_request_t request = {.samples = capture, .count = captured};
                capture = NULL;
                if (xQueueSend(s_asr_queue, &request, 0) != pdTRUE) {
                    free(request.samples);
                    s_asr_busy = false;
                    (void)hisu_ui_post_status(HISU_UI_ERROR, "Bộ nhận lệnh đang bận");
                }
            }
            continue;
        }
        /* Keep draining AFE while HTTPS transcription runs, but do not spend
         * CPU on another wake-word search until the command is complete. */
        if (s_asr_busy) continue;
        esp_mn_state_t state = s_multinet->detect(s_multinet_data, result->data);
        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *recognized = s_multinet->get_results(s_multinet_data);
            if (recognized && recognized->num > 0)
                ESP_LOGI(TAG, "MultiNet command=%d text=%s confidence=%.2f",
                         recognized->command_id[0], recognized->string, recognized->prob[0]);
            if (recognized && recognized->num > 0 && recognized->command_id[0] == 1) {
                if (!s_asr_busy) {
                    ESP_LOGI(TAG, "Offline Hey Hisu detected (confidence %.2f)", recognized->prob[0]);
                    hisu_network_status_t network;
                    hisu_network_get(&network);
                    if (!network.connected) {
                        (void)hisu_ui_post_status(HISU_UI_ERROR, "Cần Wi-Fi để nghe lệnh");
                        feedback_until = esp_timer_get_time() + 2500000;
                    } else if (!hisu_settings_has_asr_key()) {
                        (void)hisu_ui_post_status(HISU_UI_ERROR, "Cần Groq API key trong Setting");
                        feedback_until = esp_timer_get_time() + 2500000;
                    } else {
                        capture = heap_caps_malloc(ASR_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                        if (capture) {
                            s_asr_busy = true;
                            captured = 0;
                            silence_samples = 0;
                            speech_frames = 0;
                            heard_command = false;
                            (void)hisu_ui_post_status(HISU_UI_LISTENING, "Đang nghe lệnh...");
                        } else {
                            (void)hisu_ui_post_status(HISU_UI_ERROR, "Không đủ RAM ghi âm");
                            feedback_until = esp_timer_get_time() + 2500000;
                        }
                    }
                }
            }
            s_multinet->clean(s_multinet_data);
        } else if (state == ESP_MN_STATE_TIMEOUT) {
            esp_mn_results_t *candidate = s_multinet->get_results(s_multinet_data);
            if (candidate && candidate->num > 0)
                ESP_LOGI(TAG, "MultiNet timeout candidate=%s confidence=%.2f",
                         candidate->raw_string, candidate->prob[0]);
            s_multinet->clean(s_multinet_data);
        }
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
        s_asr_queue = xQueueCreate(1, sizeof(asr_request_t));
        if (!s_asr_queue) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK && xTaskCreate(asr_task, "hisu_asr", 12288, NULL, 4, NULL) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err == ESP_OK && xTaskCreate(microphone_task, "hisu_mic", 4096, NULL, 5, NULL) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err == ESP_OK && xTaskCreate(detection_task, "hisu_wake", 4096, NULL, 5, NULL) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
    return err;
}
