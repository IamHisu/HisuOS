#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include "alert_mock.h"
static bool summary_online=true,summary_setup,summary_key=true,summary_requested=true,summary_token_valid=true;
static int summary_percent=80;
static unsigned summary_applied,summary_done,summary_started;
static unsigned uxTaskGetStackHighWaterMark(void *task) { return 4096; }
static esp_http_client_handle_t summary_http_init(const esp_http_client_config_t *config)
{
    assert(strstr(config->url,"gemini-3.5-flash-lite:generateContent"));
    assert(config->is_async && config->disable_auto_redirect && config->timeout_ms==1000 && config->crt_bundle_attach);
    esp_http_client_handle_t http=calloc(1,sizeof(*http));
    if (http) { http->config=*config; ++mock_http_live; ++summary_started; }
    return http;
}
static esp_err_t summary_http_body(esp_http_client_handle_t http,const char *body,int size)
{
    cJSON *root=cJSON_Parse(body); assert(root && size==(int)strlen(body));
    cJSON *config=cJSON_GetObjectItem(root,"generationConfig");
    assert(!strcmp(cJSON_GetObjectItem(config,"responseMimeType")->valuestring,"application/json"));
    assert(cJSON_GetObjectItem(config,"responseJsonSchema") && cJSON_GetObjectItem(config,"maxOutputTokens")->valueint==1024);
    assert(!cJSON_GetObjectItem(root,"tools"));
    cJSON_Delete(root); return ESP_OK;
}
static const char *summary_wire="{\"candidates\":[{\"finishReason\":\"STOP\",\"content\":{\"parts\":[{\"text\":\"{\\\"facts\\\":[]}\"}]}}]}";
static esp_err_t summary_http_perform(esp_http_client_handle_t http)
{
    ++mock_http_calls;
    if (!http->calls++) return ESP_ERR_HTTP_EAGAIN;
    if (mock_mode==7) return ESP_ERR_HTTP_EAGAIN;
    if (mock_mode==8) mock_idle=false;
    if (mock_mode==9) summary_percent=19;
    if (mock_mode==10) summary_token_valid=false;
    esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.user_data=http->config.user_data,
        .data=(void *)summary_wire,.data_len=mock_mode==5 ? 9000 : (int)strlen(summary_wire)};
    return http->config.event_handler(&event);
}
#define esp_http_client_init summary_http_init
#define esp_http_client_set_post_field summary_http_body
#define esp_http_client_perform summary_http_perform
#include "../components/hisu_voice/src/hisu_voice_memory_summary.c"

void hisu_power_get(hisu_power_status_t *out) { *out=(hisu_power_status_t){.valid=true,.external_power=mock_charging,.percent=summary_percent}; }
void hisu_network_get_link(hisu_network_link_t *out) {
    *out=(hisu_network_link_t){.connected=summary_online,.setup_enabled=summary_setup,.internet=summary_online ? HISU_INTERNET_ONLINE : HISU_INTERNET_OFFLINE};
}
esp_err_t hisu_settings_get_gemini_profile(hisu_gemini_key_t profile,char out[128]) {
    assert(profile==HISU_GEMINI_CHAT); strcpy(out,"fake-key"); return summary_key ? ESP_OK : ESP_ERR_NOT_FOUND;
}
char *hisu_memory_summary_snapshot(uint32_t *ticket) {
    if (!summary_requested) return NULL;
    *ticket=1; const char *text="{\"known_facts\":[],\"user_statements\":\"Short answers please\"}";
    char *copy=malloc(strlen(text)+1); strcpy(copy,text); return copy;
}
bool hisu_memory_summary_current(uint32_t ticket) { return ticket==1 && summary_requested && summary_token_valid; }
void hisu_memory_summary_done(uint32_t ticket) { if (hisu_memory_summary_current(ticket)) { ++summary_done; summary_requested=false; } }
esp_err_t hisu_memory_summary_apply(uint32_t ticket,const char *text,unsigned *stored) {
    assert(!mock_http_live && hisu_memory_summary_current(ticket) && !strcmp(text,"{\"facts\":[]}"));
    ++summary_applied; *stored=0; return ESP_OK;
}
static bool summary_idle(void) { return mock_idle; }
static void summary_reset(void)
{
    assert(!mock_http_live); mock_now+=4000000000LL;
    mock_idle=summary_online=summary_key=summary_requested=summary_token_valid=true;
    mock_charging=summary_setup=false; summary_percent=80; mock_mode=0; mock_http_status=200;
}
int main(void)
{
    summary_reset(); summary_percent=19;
    assert(!voice_memory_summary_poll(summary_idle) && !summary_started);
    summary_percent=20; assert(summary_allowed(summary_idle));
    summary_setup=true; assert(!voice_memory_summary_poll(summary_idle)); summary_setup=false;
    summary_online=false; assert(!voice_memory_summary_poll(summary_idle)); summary_online=true;
    mock_idle=false; assert(!voice_memory_summary_poll(summary_idle)); mock_idle=true;
    summary_key=false; assert(!voice_memory_summary_poll(summary_idle) && !summary_started); summary_key=true;
    summary_reset(); assert(voice_memory_summary_poll(summary_idle) && summary_applied==1 && summary_done==1);
    assert(!summary_requested && !mock_http_live);
    summary_requested=true; unsigned starts=summary_started;
    assert(!voice_memory_summary_poll(summary_idle) && summary_started==starts);
    unsigned stored=0;
    assert(summary_decode(1,"{\"candidates\":[]}",&stored)!=ESP_OK);
    assert(summary_decode(1,"{\"candidates\":[{\"finishReason\":\"MAX_TOKENS\",\"content\":{\"parts\":[{\"text\":\"{}\"}]}}]}",&stored)!=ESP_OK);
    assert(!hisu_memory_json_shallow("[[[[[[[[[[[[[0]]]]]]]]]]]]]",12));
    assert(hisu_memory_json_shallow("{\"quoted\":\"[[[[[[[[[[[[[[[\"}",12));
    summary_reset(); mock_http_status=429; unsigned applied=summary_applied,done=summary_done;
    assert(voice_memory_summary_poll(summary_idle) && summary_applied==applied && summary_done==done+1 && !mock_http_live);
    summary_requested=true; starts=summary_started;
    mock_now+=600000000; assert(!voice_memory_summary_poll(summary_idle) && summary_started==starts);
    for (unsigned mode=8;mode<=10;++mode) {
        summary_reset(); mock_mode=(int)mode; applied=summary_applied; done=summary_done;
        assert(voice_memory_summary_poll(summary_idle) && summary_applied==applied && summary_done==done && summary_requested);
        assert(!mock_http_live);
    }
    summary_reset(); mock_mode=5; applied=summary_applied;
    assert(voice_memory_summary_poll(summary_idle) && summary_applied==applied && !mock_http_live && !summary_requested);
    summary_reset(); mock_mode=6;
    assert(voice_memory_summary_poll(summary_idle) && summary_applied==applied && !mock_http_live && !summary_requested);
    summary_reset(); mock_mode=7; int64_t started=mock_now;
    assert(voice_memory_summary_poll(summary_idle) && summary_applied==applied && mock_now-started<=10020000 && !mock_http_live);
    summary_reset(); mock_charging=true; summary_percent=1; assert(summary_allowed(summary_idle));
    while (summary_started<12) { summary_reset(); assert(voice_memory_summary_poll(summary_idle)); }
    summary_reset(); starts=summary_started;
    assert(!voice_memory_summary_poll(summary_idle) && starts==12 && !mock_http_live);
    puts("Memory summary HTTP tests passed: battery20/charging/idle/online gates, primary key only, structured bounded text, cancellation, timeout, quota cooldown, cleanup and 12-request cap. API mocked.");
    return 0;
}
