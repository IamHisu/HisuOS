#include "friend_ai.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

#define AI_RESPONSE_MAX 16384
#define AI_TEXT_MAX 2048
#define AI_TASK_STACK 10240

enum
{
    AI_FAILURE_NONE,
    AI_FAILURE_UNAVAILABLE,
    AI_FAILURE_KEY,
    AI_FAILURE_RATE,
    AI_FAILURE_RESPONSE_SIZE,
    AI_FAILURE_REJECTED,
    AI_FAILURE_NETWORK,
    AI_FAILURE_EMPTY,
    AI_FAILURE_CLOCK,
    AI_FAILURE_CREDITS,
    AI_FAILURE_ORG_USAGE,
    AI_FAILURE_ORG_SPEND,
    AI_FAILURE_PROJECT_SPEND,
    AI_FAILURE_LIMIT_UNKNOWN,
    AI_FAILURE_GEMINI_QUOTA,
    AI_FAILURE_GEMINI_DENIED,
    AI_FAILURE_GEMINI_MODEL,
    AI_FAILURE_GEMINI_REQUEST,
    AI_FAILURE_GEMINI_BLOCKED,
    AI_FAILURE_DISPLAY,
};

typedef struct
{
    char question[FRIEND_AI_QUESTION_MAX + 1];
    uint32_t generation;
    friend_ai_provider_t provider;
} ai_request_t;

typedef struct
{
    char *text;
    bool error;
    uint32_t generation;
} ai_result_t;

typedef struct
{
    char *body;
    size_t used;
    size_t capacity;
    bool overflow;
} response_buffer_t;

static const char *TAG = "AI";
static QueueHandle_t request_queue;
static QueueHandle_t result_queue;
static atomic_int state = FRIEND_AI_IDLE;
static atomic_uint generation;
static atomic_int error_code;
static atomic_bool clear_subtitle_pending;
static atomic_int active_provider = FRIEND_AI_GEMINI;
static friend_ai_output_t s_output;

esp_err_t friend_ai_set_output(const friend_ai_output_t *output)
{
    if (output == NULL || output->available == NULL || output->show == NULL ||
        output->active == NULL || output->clear == NULL) return ESP_ERR_INVALID_ARG;
    if (request_queue != NULL) return ESP_ERR_INVALID_STATE;
    s_output = *output;
    return ESP_OK;
}

static bool output_available(void)
{
    return s_output.available != NULL && s_output.available();
}

static const char *default_name = "Hisu";
static const char *default_personality =
    "Tinh nghịch, trẻ con, thân thiện, chủ yếu nói trống không, không dùng đại từ nhân xưng "
    "khi cần xưng hô thì dùng mình–bạn, đa phần không dùng đại từ nhân xưng";

static const char *key_name(friend_ai_provider_t provider)
{
    return provider == FRIEND_AI_OPENAI ? "api_key" : "gemini_key";
}

static esp_err_t on_http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    response_buffer_t *response = event->user_data;
    if (event->data_len > response->capacity - 1 - response->used)
    {
        response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->body + response->used, event->data, event->data_len);
    response->used += event->data_len;
    response->body[response->used] = '\0';
    return ESP_OK;
}

static char *extract_text(const char *body, friend_ai_provider_t provider)
{
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) return NULL;
    char *text = heap_caps_calloc(AI_TEXT_MAX + 1, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t used = 0;
    if (text != NULL && provider == FRIEND_AI_OPENAI)
    {
        cJSON *output = cJSON_GetObjectItemCaseSensitive(root, "output");
        cJSON *item;
        cJSON_ArrayForEach(item, output)
        {
            cJSON *content = cJSON_GetObjectItemCaseSensitive(item, "content");
            if (!cJSON_IsArray(content)) continue;
            cJSON *part;
            cJSON_ArrayForEach(part, content)
            {
                cJSON *type = cJSON_GetObjectItemCaseSensitive(part, "type");
                cJSON *value = cJSON_GetObjectItemCaseSensitive(part, "text");
                if (!cJSON_IsString(type) || strcmp(type->valuestring, "output_text") != 0 ||
                    !cJSON_IsString(value)) continue;
                size_t length = strlen(value->valuestring);
                if (length > AI_TEXT_MAX - used) length = AI_TEXT_MAX - used;
                memcpy(text + used, value->valuestring, length);
                used += length;
            }
        }
    }
    else if (text != NULL)
    {
        cJSON *candidates = cJSON_GetObjectItemCaseSensitive(root, "candidates");
        cJSON *candidate = cJSON_GetArrayItem(candidates, 0);
        cJSON *content = cJSON_GetObjectItemCaseSensitive(candidate, "content");
        cJSON *parts = cJSON_GetObjectItemCaseSensitive(content, "parts");
        cJSON *part;
        cJSON_ArrayForEach(part, parts)
        {
            cJSON *thought = cJSON_GetObjectItemCaseSensitive(part, "thought");
            cJSON *value = cJSON_GetObjectItemCaseSensitive(part, "text");
            if (cJSON_IsTrue(thought) || !cJSON_IsString(value)) continue;
            size_t length = strlen(value->valuestring);
            if (length > AI_TEXT_MAX - used) length = AI_TEXT_MAX - used;
            memcpy(text + used, value->valuestring, length);
            used += length;
        }
    }
    cJSON_Delete(root);
    if (text == NULL || used == 0)
    {
        free(text);
        return NULL;
    }
    if (used == AI_TEXT_MAX)
    {
        size_t start = used - 1;
        while (start > 0 && ((unsigned char)text[start] & 0xc0) == 0x80) start--;
        unsigned char lead = (unsigned char)text[start];
        size_t width = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        if (used - start < width) used = start;
    }
    text[used] = '\0';
    return text;
}

static int classify_http_error(int status, const char *body, friend_ai_provider_t provider)
{
    if (provider == FRIEND_AI_GEMINI)
    {
        if (status == 429) return AI_FAILURE_GEMINI_QUOTA;
        if (status == 401 || status == 403) return AI_FAILURE_GEMINI_DENIED;
        if (status == 404) return AI_FAILURE_GEMINI_MODEL;
        return AI_FAILURE_GEMINI_REQUEST;
    }
    if (status == 401) return AI_FAILURE_KEY;
    if (status != 429) return AI_FAILURE_REJECTED;

    cJSON *root = cJSON_Parse(body);
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    cJSON *code_item = cJSON_GetObjectItemCaseSensitive(error, "code");
    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(error, "type");
    const char *code = cJSON_IsString(code_item) ? code_item->valuestring : "";
    const char *type = cJSON_IsString(type_item) ? type_item->valuestring : "";
    int failure = AI_FAILURE_LIMIT_UNKNOWN;
    if (strcmp(code, "organization_usage_limit_exceeded") == 0)
        failure = AI_FAILURE_ORG_USAGE;
    else if (strcmp(code, "organization_spend_limit_exceeded") == 0)
        failure = AI_FAILURE_ORG_SPEND;
    else if (strcmp(code, "project_spend_limit_exceeded") == 0)
        failure = AI_FAILURE_PROJECT_SPEND;
    else if (strcmp(code, "credit_balance_exhausted") == 0 ||
        strcmp(code, "insufficient_quota") == 0 ||
        strcmp(type, "insufficient_quota") == 0)
        failure = AI_FAILURE_CREDITS;
    else if (strcmp(code, "rate_limit_exceeded") == 0 || strcmp(code, "slow_down") == 0 ||
        strcmp(type, "rate_limit_error") == 0)
        failure = AI_FAILURE_RATE;
    cJSON_Delete(root);
    return failure;
}

static const char *failure_message(int failure)
{
    switch (failure)
    {
        case AI_FAILURE_KEY: return "API key khong hop le";
        case AI_FAILURE_RATE: return "Hoi qua nhanh, thu lai sau";
        case AI_FAILURE_RESPONSE_SIZE: return "Phan hoi AI qua dai";
        case AI_FAILURE_REJECTED: return "OpenAI tu choi yeu cau";
        case AI_FAILURE_NETWORK: return "Loi ket noi OpenAI";
        case AI_FAILURE_EMPTY: return "AI khong tra loi duoc";
        case AI_FAILURE_CLOCK: return "Chua dong bo duoc gio";
        case AI_FAILURE_CREDITS: return "API het credit/han muc";
        case AI_FAILURE_ORG_USAGE: return "API vuot han muc to chuc";
        case AI_FAILURE_ORG_SPEND: return "API cham gioi han chi tieu";
        case AI_FAILURE_PROJECT_SPEND: return "Project vuot gioi han chi tieu";
        case AI_FAILURE_LIMIT_UNKNOWN: return "OpenAI bao 429; xem billing/limits";
        case AI_FAILURE_GEMINI_QUOTA: return "Gemini het luot; thu lai sau";
        case AI_FAILURE_GEMINI_DENIED: return "Gemini khong co quyen truy cap";
        case AI_FAILURE_GEMINI_MODEL: return "Gemini model khong kha dung";
        case AI_FAILURE_GEMINI_REQUEST: return "Gemini tu choi yeu cau";
        case AI_FAILURE_GEMINI_BLOCKED: return "Gemini khong the tra loi";
        case AI_FAILURE_DISPLAY: return "Khong hien duoc phu de";
        default: return "Chua ket noi duoc AI";
    }
}

static char *build_payload(const char *question, friend_ai_provider_t provider)
{
    friend_ai_profile_t profile;
    friend_ai_get_profile(&profile);
    char instructions[1536];
    int instruction_len = snprintf(instructions, sizeof(instructions),
        "You are %s, a small desktop companion. Personality and speaking style: %s "
        "Reply in Vietnamese and plain text. For ordinary questions, use at most three "
        "short sentences. When asked for a story, use up to eight short sentences. "
        "When asked for a poem, use up to ten short lines and preserve line breaks. "
        "No Markdown. Do not claim to speak or hear audio. You have no live web access. "
        "For current weather, prices, stocks, fuel prices, lottery results, or other "
        "time-sensitive facts, say you cannot verify live values and do not invent numbers.",
        profile.name, profile.personality);
    if (instruction_len < 0 || instruction_len >= sizeof(instructions)) return NULL;
    cJSON *body = cJSON_CreateObject();
    if (body == NULL) return NULL;
    if (provider == FRIEND_AI_OPENAI)
    {
        cJSON_AddStringToObject(body, "model", "gpt-4.1-mini");
        cJSON_AddStringToObject(body, "instructions", instructions);
        cJSON_AddStringToObject(body, "input", question);
        cJSON_AddBoolToObject(body, "store", false);
        cJSON_AddNumberToObject(body, "max_output_tokens", 512);
    }
    else
    {
        cJSON *system = cJSON_AddObjectToObject(body, "systemInstruction");
        if (system == NULL) { cJSON_Delete(body); return NULL; }
        cJSON *system_parts = cJSON_AddArrayToObject(system, "parts");
        if (system_parts == NULL) { cJSON_Delete(body); return NULL; }
        cJSON *system_text = cJSON_CreateObject();
        if (system_text == NULL) { cJSON_Delete(body); return NULL; }
        cJSON_AddStringToObject(system_text, "text", instructions);
        cJSON_AddItemToArray(system_parts, system_text);
        cJSON *contents = cJSON_AddArrayToObject(body, "contents");
        if (contents == NULL) { cJSON_Delete(body); return NULL; }
        cJSON *user = cJSON_CreateObject();
        if (user == NULL) { cJSON_Delete(body); return NULL; }
        cJSON_AddStringToObject(user, "role", "user");
        cJSON *parts = cJSON_AddArrayToObject(user, "parts");
        if (parts == NULL) { cJSON_Delete(user); cJSON_Delete(body); return NULL; }
        cJSON *part = cJSON_CreateObject();
        if (part == NULL) { cJSON_Delete(user); cJSON_Delete(body); return NULL; }
        cJSON_AddStringToObject(part, "text", question);
        cJSON_AddItemToArray(parts, part);
        cJSON_AddItemToArray(contents, user);
        cJSON *config = cJSON_AddObjectToObject(body, "generationConfig");
        if (config == NULL) { cJSON_Delete(body); return NULL; }
        cJSON_AddNumberToObject(config, "maxOutputTokens", 640);
        cJSON *thinking = cJSON_AddObjectToObject(config, "thinkingConfig");
        if (thinking == NULL) { cJSON_Delete(body); return NULL; }
        cJSON_AddStringToObject(thinking, "thinkingLevel", "MINIMAL");
    }
    char *payload = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    return payload;
}

static char *request_ai(const char *question, friend_ai_provider_t provider, int *failure)
{
    *failure = AI_FAILURE_UNAVAILABLE;
    if (time(NULL) < 1704067200)
    {
        esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
        if (time(NULL) < 1704067200)
        {
            *failure = AI_FAILURE_CLOCK;
            return NULL;
        }
    }
    nvs_handle_t handle;
    if (nvs_open("friend_ai", NVS_READONLY, &handle) != ESP_OK) return NULL;
    char key[FRIEND_AI_KEY_MAX + 1] = {0};
    size_t key_size = sizeof(key);
    esp_err_t err = nvs_get_str(handle, key_name(provider), key, &key_size);
    nvs_close(handle);
    if (err != ESP_OK || key[0] == '\0')
    {
        memset(key, 0, sizeof(key));
        *failure = AI_FAILURE_KEY;
        return NULL;
    }

    char *payload = build_payload(question, provider);
    if (payload == NULL) { memset(key, 0, sizeof(key)); return NULL; }

    response_buffer_t response = {
        .capacity = AI_RESPONSE_MAX,
    };
    response.body = heap_caps_calloc(response.capacity, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (response.body == NULL)
    {
        cJSON_free(payload);
        memset(key, 0, sizeof(key));
        return NULL;
    }
    esp_http_client_config_t config = {
        .url = provider == FRIEND_AI_OPENAI ? "https://api.openai.com/v1/responses" :
            "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent",
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_http_event,
        .user_data = &response,
        .timeout_ms = 30000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char *answer = NULL;
    if (client != NULL)
    {
        char authorization[FRIEND_AI_KEY_MAX + 8];
        if (provider == FRIEND_AI_OPENAI)
        {
            snprintf(authorization, sizeof(authorization), "Bearer %s", key);
            esp_http_client_set_header(client, "Authorization", authorization);
        }
        else esp_http_client_set_header(client, "x-goog-api-key", key);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, payload, strlen(payload));
        err = esp_http_client_perform(client);
        int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
        if (err == ESP_OK && status == 200 && !response.overflow)
            answer = extract_text(response.body, provider);
        *failure = response.overflow ? AI_FAILURE_RESPONSE_SIZE :
                   err != ESP_OK ? AI_FAILURE_NETWORK :
                   status != 200 ? classify_http_error(status, response.body, provider) :
                   answer == NULL ? provider == FRIEND_AI_GEMINI ? AI_FAILURE_GEMINI_BLOCKED : AI_FAILURE_EMPTY :
                   AI_FAILURE_NONE;
        if (err != ESP_OK || status != 200)
            ESP_LOGW(TAG, "%s request failed: %s, HTTP %d",
                provider == FRIEND_AI_GEMINI ? "Gemini" : "OpenAI", esp_err_to_name(err), status);
        memset(authorization, 0, sizeof(authorization));
        esp_http_client_cleanup(client);
    }
    memset(key, 0, sizeof(key));
    cJSON_free(payload);
    free(response.body);
    return answer;
}

static void ai_task(void *arg)
{
    (void)arg;
    ai_request_t request;
    while (xQueueReceive(request_queue, &request, portMAX_DELAY) == pdTRUE)
    {
        int failure = 0;
        char *answer = request_ai(request.question, request.provider, &failure);
        memset(&request.question, 0, sizeof(request.question));
        if (request.generation != atomic_load(&generation) ||
            !output_available())
        {
            ESP_LOGI(TAG, "Reply discarded after display mode changed");
            free(answer);
            atomic_store(&state, FRIEND_AI_IDLE);
            continue;
        }
        ai_result_t result = { .text = answer, .error = answer == NULL,
            .generation = request.generation };
        if (answer == NULL) result.text = strdup(failure_message(failure));
        if (result.text == NULL)
        {
            ESP_LOGE(TAG, "Could not allocate reply for UI");
            atomic_store(&error_code, AI_FAILURE_DISPLAY);
            atomic_store(&state, FRIEND_AI_ERROR);
            continue;
        }
        atomic_store(&error_code, failure);
        atomic_store(&state, FRIEND_AI_READY);
        if (xQueueSend(result_queue, &result, 0) != pdTRUE)
        {
            ESP_LOGE(TAG, "UI reply queue full; dropped %u bytes",
                (unsigned int)strlen(result.text));
            free(result.text);
            atomic_store(&error_code, AI_FAILURE_DISPLAY);
            atomic_store(&state, FRIEND_AI_ERROR);
        }
        else ESP_LOGI(TAG, "Reply queued for TFT: %u bytes, error=%d",
            (unsigned int)strlen(result.text), result.error);
    }
    vTaskDelete(NULL);
}

esp_err_t friend_ai_init(void)
{
    nvs_handle_t handle;
    if (nvs_open("friend_ai", NVS_READWRITE, &handle) == ESP_OK)
    {
        uint8_t saved = FRIEND_AI_GEMINI;
        if (nvs_get_u8(handle, "provider", &saved) == ESP_OK && saved <= FRIEND_AI_OPENAI)
            atomic_store(&active_provider, saved);
        char saved_name[FRIEND_AI_NAME_MAX + 1];
        size_t name_length = sizeof(saved_name);
        if (nvs_get_str(handle, "name", saved_name, &name_length) == ESP_OK &&
            (strcmp(saved_name, "Mộc") == 0 || strcmp(saved_name, "Moc") == 0))
        {
            esp_err_t err = nvs_set_str(handle, "name", default_name);
            if (err == ESP_OK) err = nvs_commit(handle);
            if (err != ESP_OK)
                ESP_LOGW(TAG, "Could not migrate saved AI name: %s", esp_err_to_name(err));
        }
        nvs_close(handle);
    }
    request_queue = xQueueCreate(1, sizeof(ai_request_t));
    result_queue = xQueueCreate(1, sizeof(ai_result_t));
    if (request_queue == NULL || result_queue == NULL) return ESP_ERR_NO_MEM;
    return xTaskCreate(ai_task, "friend_ai", AI_TASK_STACK, NULL, 3, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

friend_ai_provider_t friend_ai_get_provider(void)
{
    return atomic_load(&active_provider);
}

const char *friend_ai_provider_name(void)
{
    return friend_ai_get_provider() == FRIEND_AI_OPENAI ? "openai" : "gemini";
}

esp_err_t friend_ai_set_provider(friend_ai_provider_t provider)
{
    if (provider != FRIEND_AI_GEMINI && provider != FRIEND_AI_OPENAI) return ESP_ERR_INVALID_ARG;
    if (atomic_load(&state) == FRIEND_AI_WORKING ||
        atomic_load(&state) == FRIEND_AI_READY) return ESP_ERR_INVALID_STATE;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("friend_ai", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "provider", provider);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    atomic_store(&active_provider, provider);
    atomic_fetch_add(&generation, 1);
    atomic_store(&clear_subtitle_pending, true);
    atomic_store(&error_code, AI_FAILURE_NONE);
    atomic_store(&state, FRIEND_AI_IDLE);
    return ESP_OK;
}

esp_err_t friend_ai_save_key(const char *key)
{
    if (atomic_load(&state) == FRIEND_AI_WORKING ||
        atomic_load(&state) == FRIEND_AI_READY) return ESP_ERR_INVALID_STATE;
    if (key == NULL || strlen(key) > FRIEND_AI_KEY_MAX) return ESP_ERR_INVALID_ARG;
    for (const unsigned char *p = (const unsigned char *)key; *p != '\0'; p++)
        if (*p < 0x21 || *p > 0x7e) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("friend_ai", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    const char *name = key_name(friend_ai_get_provider());
    err = key[0] == '\0' ? nvs_erase_key(handle, name) : nvs_set_str(handle, name, key);
    if (err == ESP_ERR_NVS_NOT_FOUND && key[0] == '\0') err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

bool friend_ai_has_key(void)
{
    nvs_handle_t handle;
    if (nvs_open("friend_ai", NVS_READONLY, &handle) != ESP_OK) return false;
    size_t length = 0;
    esp_err_t err = nvs_get_str(handle, key_name(friend_ai_get_provider()), NULL, &length);
    nvs_close(handle);
    return err == ESP_OK && length > 1;
}

void friend_ai_get_profile(friend_ai_profile_t *profile)
{
    if (profile == NULL) return;
    snprintf(profile->name, sizeof(profile->name), "%s", default_name);
    snprintf(profile->personality, sizeof(profile->personality), "%s", default_personality);
    nvs_handle_t handle;
    if (nvs_open("friend_ai", NVS_READONLY, &handle) != ESP_OK) return;
    size_t length = sizeof(profile->name);
    if (nvs_get_str(handle, "name", profile->name, &length) != ESP_OK || profile->name[0] == '\0')
        snprintf(profile->name, sizeof(profile->name), "%s", default_name);
    length = sizeof(profile->personality);
    if (nvs_get_str(handle, "personality", profile->personality, &length) != ESP_OK ||
        profile->personality[0] == '\0')
        snprintf(profile->personality, sizeof(profile->personality), "%s", default_personality);
    nvs_close(handle);
}

static bool profile_text_valid(const char *text, size_t max_length)
{
    size_t length = strnlen(text, max_length + 1);
    if (length == 0 || length > max_length) return false;
    bool visible = false;
    for (size_t i = 0; i < length; i++)
    {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 || c == 0x7f) return false;
        if (c != ' ') visible = true;
    }
    return visible;
}

esp_err_t friend_ai_save_profile(const friend_ai_profile_t *profile)
{
    if (profile == NULL || !profile_text_valid(profile->name, FRIEND_AI_NAME_MAX) ||
        !profile_text_valid(profile->personality, FRIEND_AI_PERSONALITY_MAX))
        return ESP_ERR_INVALID_ARG;
    if (atomic_load(&state) == FRIEND_AI_WORKING || atomic_load(&state) == FRIEND_AI_READY)
        return ESP_ERR_INVALID_STATE;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("friend_ai", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "name", profile->name);
    if (err == ESP_OK) err = nvs_set_str(handle, "personality", profile->personality);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t friend_ai_submit(const char *question)
{
    if (question == NULL || question[0] == '\0' || strlen(question) > FRIEND_AI_QUESTION_MAX)
        return ESP_ERR_INVALID_ARG;
    if (!output_available()) return ESP_ERR_INVALID_STATE;
    if (!friend_ai_has_key()) return ESP_ERR_NOT_FOUND;
    int expected = atomic_load(&state);
    if (expected == FRIEND_AI_WORKING || expected == FRIEND_AI_READY ||
        !atomic_compare_exchange_strong(&state, &expected, FRIEND_AI_WORKING))
        return ESP_ERR_INVALID_STATE;
    ai_request_t request = { .generation = atomic_fetch_add(&generation, 1) + 1,
        .provider = friend_ai_get_provider() };
    memcpy(request.question, question, strlen(question) + 1);
    if (xQueueSend(request_queue, &request, 0) != pdTRUE)
    {
        atomic_store(&state, FRIEND_AI_IDLE);
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&error_code, 0);
    atomic_store(&clear_subtitle_pending, true);
    return ESP_OK;
}

esp_err_t friend_ai_test_subtitle(void)
{
    if (!output_available() || result_queue == NULL)
        return ESP_ERR_INVALID_STATE;
    int expected = atomic_load(&state);
    if (expected == FRIEND_AI_WORKING || expected == FRIEND_AI_READY ||
        !atomic_compare_exchange_strong(&state, &expected, FRIEND_AI_READY))
        return ESP_ERR_INVALID_STATE;
    ai_result_t result = { .text = strdup("A lô, A lô 1 2 3 4! A lô!"),
        .generation = atomic_fetch_add(&generation, 1) + 1 };
    if (result.text == NULL)
    {
        atomic_store(&state, FRIEND_AI_ERROR);
        atomic_store(&error_code, AI_FAILURE_DISPLAY);
        return ESP_ERR_NO_MEM;
    }
    atomic_store(&error_code, AI_FAILURE_NONE);
    atomic_store(&clear_subtitle_pending, true);
    if (xQueueSend(result_queue, &result, 0) != pdTRUE)
    {
        free(result.text);
        atomic_store(&state, FRIEND_AI_ERROR);
        atomic_store(&error_code, AI_FAILURE_DISPLAY);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Subtitle self-test queued");
    return ESP_OK;
}

friend_ai_state_t friend_ai_state(void)
{
    return atomic_load(&state);
}

const char *friend_ai_error(void)
{
    return failure_message(atomic_load(&error_code));
}

void friend_ai_process_ui(void)
{
    if (atomic_load(&state) == FRIEND_AI_SHOWING && s_output.active != NULL && !s_output.active())
        atomic_store(&state, FRIEND_AI_DONE);
    if (atomic_exchange(&clear_subtitle_pending, false) && s_output.clear != NULL) s_output.clear();
    ai_result_t result;
    if (result_queue == NULL || xQueueReceive(result_queue, &result, 0) != pdTRUE) return;
    if (result.generation == atomic_load(&generation) &&
        output_available())
    {
        if (s_output.show != NULL && s_output.show(result.text))
        {
            ESP_LOGI(TAG, "Reply accepted by TFT UI");
            atomic_store(&state, result.error ? FRIEND_AI_ERROR : FRIEND_AI_SHOWING);
        }
        else
        {
            ESP_LOGE(TAG, "TFT subtitle creation failed");
            atomic_store(&error_code, AI_FAILURE_DISPLAY);
            atomic_store(&state, FRIEND_AI_ERROR);
        }
    }
    else ESP_LOGI(TAG, "Queued reply ignored: stale generation or not face mode");
    free(result.text);
}

void friend_ai_cancel(void)
{
    atomic_fetch_add(&generation, 1);
    if (atomic_load(&state) != FRIEND_AI_WORKING)
        atomic_store(&state, FRIEND_AI_IDLE);
    atomic_store(&clear_subtitle_pending, true);
}
