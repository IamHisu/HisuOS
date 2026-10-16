#include "hisu_voice_alert_tts.h"
#include "hisu_alert.h"
#include "hisu_power.h"
#include "hisu_network.h"
#include "hisu_settings.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "cJSON.h"
#include "hisu_voice_tts_config.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

#define RESPONSE_LIMIT (HISU_ALERT_PCM_MAX / 3 * 4 + 4096)
typedef struct { char *body; size_t used; bool overflow, reminder; bool (*idle)(void); } tts_request_t;
static bool reminder_allowed(bool (*idle)(void))
{
    hisu_power_status_t power; hisu_power_get(&power);
    hisu_network_link_t network; hisu_network_get_link(&network);
    return idle() && power.valid && (power.external_power || power.percent > 2) && network.connected && !network.setup_enabled;
}
static bool allowed(bool (*idle)(void))
{
    hisu_power_status_t power; hisu_power_get(&power);
    hisu_network_link_t network; hisu_network_get_link(&network);
    return idle() && power.valid && power.external_power && network.connected;
}
static esp_err_t receive_data(esp_http_client_event_t *event)
{
    tts_request_t *r = event->user_data;
    if (!(r->reminder ? reminder_allowed(r->idle) : allowed(r->idle))) return ESP_FAIL;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if ((size_t)event->data_len > RESPONSE_LIMIT - r->used) { r->overflow = true; return ESP_FAIL; }
        memcpy(r->body + r->used,event->data,event->data_len);
        r->used += event->data_len; r->body[r->used] = 0;
    }
    return ESP_OK;
}
static char *make_text_body(const char *phrase)
{
    char *prompt = heap_caps_malloc(1400,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!prompt) return NULL;
    snprintf(prompt,1400,"Đọc nguyên văn câu tiếng Việt sau bằng giọng gốc tự nhiên, "
        "không giả giọng robot hay thêm hiệu ứng. Nói rõ, tốc độ tự nhiên, không kéo dài quá 8 giây. "
        "Không đọc chỉ dẫn, không thêm lời, không hát, không thêm tiếng bíp: %s",phrase);
    cJSON *text = cJSON_CreateString(prompt); free(prompt);
    char *encoded = text ? cJSON_PrintUnformatted(text) : NULL; cJSON_Delete(text);
    if (!encoded) return NULL;
    static const char format[] = "{\"contents\":[{\"parts\":[{\"text\":%s}]}],\"generationConfig\":{"
        "\"responseModalities\":[\"AUDIO\"],\"speechConfig\":{\"voiceConfig\":{\"prebuiltVoiceConfig\":{\"voiceName\":\"Kore\"}}}}}";
    size_t size = strlen(encoded) + sizeof(format);
    char *body = heap_caps_malloc(size,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body) snprintf(body,size,format,encoded);
    cJSON_free(encoded); return body;
}
static char *make_body(unsigned index) { return make_text_body(hisu_alert_phrase(index)); }
static esp_err_t decode_response(const char *body, int16_t **pcm, size_t *bytes)
{
    cJSON *root = cJSON_Parse(body);
    const cJSON *candidate = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root,"candidates"),0);
    const cJSON *finish = cJSON_GetObjectItemCaseSensitive(candidate,"finishReason");
    const cJSON *parts = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(candidate,"content"),"parts");
    bool valid = cJSON_IsString(finish) && !strcmp(finish->valuestring,"STOP") && cJSON_IsArray(parts);
    int16_t *out = valid ? heap_caps_malloc(HISU_ALERT_PCM_MAX,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    size_t used = 0;
    const cJSON *part;
    cJSON_ArrayForEach(part,parts) {
        const cJSON *blob = cJSON_GetObjectItemCaseSensitive(part,"inlineData");
        if (!blob) continue;
        const cJSON *mime = cJSON_GetObjectItemCaseSensitive(blob,"mimeType");
        const cJSON *data = cJSON_GetObjectItemCaseSensitive(blob,"data");
        if (!out || !cJSON_IsString(mime) || !cJSON_IsString(data) ||
            (strcasecmp(mime->valuestring,"audio/L16;codec=pcm;rate=24000") &&
             strcasecmp(mime->valuestring,"audio/L16;rate=24000") &&
             strcasecmp(mime->valuestring,"audio/pcm;rate=24000"))) { valid = false; break; }
        size_t decoded = 0;
        int err = mbedtls_base64_decode((unsigned char *)out + used,HISU_ALERT_PCM_MAX - used,&decoded,
            (const unsigned char *)data->valuestring,strlen(data->valuestring));
        if (err || decoded > HISU_ALERT_PCM_MAX - used) { valid = false; break; }
        used += decoded;
    }
    cJSON_Delete(root);
    if (!valid || !out || used < 24000 || (used & 1U)) { free(out); return ESP_FAIL; }
    int peak = 0;
    for (size_t i = 0; i < used / 2; ++i) {
        int sample = out[i]; if (sample < 0) sample = -sample;
        if (sample > peak) peak = sample;
    }
    if (peak < 32) { free(out); return ESP_FAIL; }
    *pcm = out; *bytes = used; return ESP_OK;
}
bool hisu_voice_alert_prepare(bool (*idle)(void))
{
    unsigned index;
    if (!hisu_alert_prepare_take(&index)) return false;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    int status = 0;
    char key[128] = {0};
    char *body = NULL;
    tts_request_t request = {.idle=idle};
    esp_http_client_handle_t http = NULL;
    int16_t *pcm = NULL; size_t bytes = 0;
    if (!allowed(idle)) goto done;
    err = hisu_settings_get_gemini_profile(HISU_GEMINI_READER,key);
    if (err != ESP_OK) goto done;
    body = make_body(index);
    request.body = heap_caps_malloc(RESPONSE_LIMIT + 1,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body || !request.body) { err = ESP_ERR_NO_MEM; goto done; }
    request.body[0] = 0;
    esp_http_client_config_t config = {
        .url=HISU_VOICE_TTS_URL,
        .method=HTTP_METHOD_POST,.timeout_ms=5000,.is_async=true,.disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach,.event_handler=receive_data,.user_data=&request,
        .buffer_size=2048,.buffer_size_tx=2048
    };
    http = esp_http_client_init(&config);
    if (!http) { err = ESP_ERR_NO_MEM; goto done; }
    err = esp_http_client_set_header(http,"x-goog-api-key",key);
    memset(key,0,sizeof(key));
    if (err == ESP_OK) err = esp_http_client_set_header(http,"Content-Type","application/json");
    if (err == ESP_OK) err = esp_http_client_set_header(http,"Accept-Encoding","identity");
    if (err == ESP_OK) err = esp_http_client_set_post_field(http,body,(int)strlen(body));
    if (err != ESP_OK) goto done;
    int64_t deadline = esp_timer_get_time() + 60000000;
    do {
        if (!allowed(idle) || esp_timer_get_time() >= deadline) { err = ESP_ERR_TIMEOUT; break; }
        err = esp_http_client_perform(http);
        if (err == ESP_ERR_HTTP_EAGAIN) vTaskDelay(pdMS_TO_TICKS(20));
    } while (err == ESP_ERR_HTTP_EAGAIN);
    status = esp_http_client_get_status_code(http);
    if (err == ESP_OK && (status != 200 || request.overflow || !esp_http_client_is_complete_data_received(http))) err = ESP_FAIL;
    /* Release TLS before parsing audio or erasing flash. Never log response/key. */
    esp_http_client_cleanup(http); http = NULL;
    if (err == ESP_OK) err = decode_response(request.body,&pcm,&bytes);
    free(request.body); request.body = NULL;
    if (err == ESP_OK) err = allowed(idle) ? hisu_alert_store(index,pcm,bytes) : ESP_ERR_INVALID_STATE;
done:
    if (http) esp_http_client_cleanup(http);
    memset(key,0,sizeof(key)); free(body); free(request.body); free(pcm);
    hisu_alert_prepare_finish(err,status);
    ESP_LOGI("BATTERY_AUDIO","Prepare clip %u: err=%d HTTP=%d",index,(int)err,status);
    return true;
}

esp_err_t hisu_voice_reminder_tts(const char *text, bool (*idle)(void), int16_t **pcm, size_t *bytes)
{
    *pcm=NULL; *bytes=0;
    if (!text || strlen(text)>240 || !reminder_allowed(idle)) return ESP_ERR_INVALID_STATE;
    char key[128]={0};
    esp_err_t err=hisu_settings_get_gemini_profile(HISU_GEMINI_READER,key);
    if (err!=ESP_OK) return err;
    char *body=make_text_body(text);
    tts_request_t request={.idle=idle,.reminder=true};
    request.body=heap_caps_malloc(RESPONSE_LIMIT+1,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body || !request.body) { memset(key,0,sizeof(key)); free(body); free(request.body); return ESP_ERR_NO_MEM; }
    request.body[0]=0;
    esp_http_client_config_t config={
        .url=HISU_VOICE_TTS_URL,
        .method=HTTP_METHOD_POST,.timeout_ms=2000,.is_async=true,.disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach,.event_handler=receive_data,.user_data=&request,
        .buffer_size=2048,.buffer_size_tx=2048
    };
    esp_http_client_handle_t http=esp_http_client_init(&config);
    if (!http) { memset(key,0,sizeof(key)); free(body); free(request.body); return ESP_ERR_NO_MEM; }
    err=esp_http_client_set_header(http,"x-goog-api-key",key); memset(key,0,sizeof(key));
    if (err==ESP_OK) err=esp_http_client_set_header(http,"Content-Type","application/json");
    if (err==ESP_OK) err=esp_http_client_set_header(http,"Accept-Encoding","identity");
    if (err==ESP_OK) err=esp_http_client_set_post_field(http,body,(int)strlen(body));
    int64_t deadline=esp_timer_get_time()+8000000;
    if (err==ESP_OK) do {
        if (!reminder_allowed(idle) || esp_timer_get_time()>=deadline) { err=ESP_ERR_TIMEOUT; break; }
        err=esp_http_client_perform(http);
        if (err==ESP_ERR_HTTP_EAGAIN) vTaskDelay(pdMS_TO_TICKS(20));
    } while (err==ESP_ERR_HTTP_EAGAIN);
    int status=esp_http_client_get_status_code(http);
    if (err==ESP_OK && (status!=200 || request.overflow || !esp_http_client_is_complete_data_received(http))) err=ESP_FAIL;
    esp_http_client_cleanup(http);
    if (err==ESP_OK) err=decode_response(request.body,pcm,bytes);
    free(body); free(request.body);
    ESP_LOGI("REMINDER","TTS: err=%d HTTP=%d",(int)err,status);
    return err;
}
