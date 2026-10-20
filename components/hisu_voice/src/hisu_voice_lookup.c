#include "hisu_voice_lookup.h"
#include "hisu_voice_lookup_data.h"
#include "hisu_memory_json_guard.h"
#include "hisu_network.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define LOOKUP_BODY_LIMIT 16384U
typedef struct {
    voice_lookup_result_t result;
    lookup_query_t query;
    bool (*current)(uint32_t,uint32_t);
    int64_t deadline;
    char *body;
    size_t used;
    bool overflow;
    char url[640];
} lookup_job_t;
static portMUX_TYPE lookup_mux=portMUX_INITIALIZER_UNLOCKED;
static lookup_job_t *lookup_job;
static bool lookup_done;
/* Worker-only RAM cache. Never persist locations or provider payloads. */
static struct { lookup_query_t query; char *json; int64_t expires,local_day; } lookup_cache[2];
static int64_t lookup_next_request;

bool voice_lookup_busy(void) {
    portENTER_CRITICAL(&lookup_mux); bool busy=lookup_job!=NULL; portEXIT_CRITICAL(&lookup_mux); return busy;
}
static bool lookup_current(lookup_job_t *j) {
    hisu_network_link_t link; hisu_network_get_link(&link);
    return j->current(j->result.epoch,j->result.generation) && link.connected && !link.setup_enabled &&
        link.internet==HISU_INTERNET_ONLINE && esp_timer_get_time()<j->deadline;
}
static esp_err_t lookup_receive(esp_http_client_event_t *event) {
    lookup_job_t *j=event->user_data;
    if (!lookup_current(j)) return ESP_FAIL;
    if (event->event_id==HTTP_EVENT_ON_DATA && event->data_len>0) {
        if ((size_t)event->data_len>LOOKUP_BODY_LIMIT-j->used || memchr(event->data,0,(size_t)event->data_len)) {
            j->overflow=true; return ESP_FAIL;
        }
        memcpy(j->body+j->used,event->data,(size_t)event->data_len);
        j->used+=(size_t)event->data_len; j->body[j->used]=0;
    }
    return ESP_OK;
}
static cJSON *lookup_fetch(lookup_job_t *j) {
    if (!lookup_current(j) || heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<6144 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<2048) return NULL;
    j->used=0; j->overflow=false; j->body[0]=0;
    esp_http_client_config_t config={.url=j->url,.timeout_ms=1000,.is_async=true,.disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach,.event_handler=lookup_receive,.user_data=j,.buffer_size=1024,.buffer_size_tx=1024};
    esp_http_client_handle_t http=esp_http_client_init(&config);
    if (!http) return NULL;
    esp_err_t err=esp_http_client_set_header(http,"Accept-Encoding","identity");
    if (err==ESP_OK) err=esp_http_client_set_header(http,"Accept","application/json");
    if (err==ESP_OK) do {
        if (!lookup_current(j)) { err=ESP_FAIL; break; }
        err=esp_http_client_perform(http);
        if (err==ESP_ERR_HTTP_EAGAIN) vTaskDelay(pdMS_TO_TICKS(20));
    } while (err==ESP_ERR_HTTP_EAGAIN);
    int status=esp_http_client_get_status_code(http);
    bool ok=err==ESP_OK && status==200 && !j->overflow && esp_http_client_is_complete_data_received(http);
    esp_http_client_cleanup(http);
    ESP_LOGI("LOOKUP","HTTP=%d err=%d bytes=%u stack=%u",status,(int)err,(unsigned)j->used,(unsigned)uxTaskGetStackHighWaterMark(NULL));
    if (!ok || !lookup_current(j) || strstr(j->body,"\\u0000") || !hisu_memory_json_shallow(j->body,10)) return NULL;
    return cJSON_ParseWithOpts(j->body,NULL,true);
}
static void lookup_encode(char *out,const char *in) {
    static const char hex[]="0123456789ABCDEF";
    for (const unsigned char *p=(const unsigned char *)in;*p;++p) {
        if ((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='-' || *p=='_') *out++=(char)*p;
        else { *out++='%'; *out++=hex[*p>>4]; *out++=hex[*p&15]; }
    }
    *out=0;
}
static cJSON *lookup_run(lookup_job_t *j) {
    if (time(NULL)<1700000000) return lookup_error("clock_unsynced");
    if (j->query.weather && !j->query.place[0]) return lookup_error("place_required");
    unsigned slot=j->query.weather ? 1 : 0;
    if (lookup_cache[slot].json && esp_timer_get_time()<lookup_cache[slot].expires &&
        (!slot || lookup_cache[slot].local_day==((int64_t)time(NULL)+25200)/86400) &&
        !memcmp(&lookup_cache[slot].query,&j->query,sizeof(j->query))) {
        cJSON *cached=cJSON_Parse(lookup_cache[slot].json);
        if (cached) { cJSON_AddBoolToObject(cached,"cached",true); return cached; }
    }
    if (esp_timer_get_time()<lookup_next_request) return lookup_error("rate_limited");
    lookup_next_request=esp_timer_get_time()+2000000;
    j->body=heap_caps_malloc(LOOKUP_BODY_LIMIT+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!j->body) return lookup_error("insufficient_memory");
    cJSON *result=NULL;
    if (!j->query.weather) {
        strcpy(j->url,"https://www.vang.today/api/prices");
        cJSON *root=lookup_fetch(j);
        result=root ? lookup_gold(root,&j->query,time(NULL)) : lookup_error("source_unavailable"); cJSON_Delete(root);
    } else {
        char encoded[289]; lookup_encode(encoded,j->query.place);
        snprintf(j->url,sizeof(j->url),"https://geocoding-api.open-meteo.com/v1/search?name=%s&count=3&language=vi&format=json&countryCode=VN",encoded);
        cJSON *geo=lookup_fetch(j);
        const cJSON *location=geo ? lookup_location(geo,&j->query,&result) : NULL;
        if (location) {
            snprintf(j->url,sizeof(j->url),"https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f&timezone=Asia%%2FBangkok&forecast_days=3&current=temperature_2m,weather_code&hourly=temperature_2m,precipitation_probability,precipitation",
                cJSON_GetObjectItemCaseSensitive(location,"latitude")->valuedouble,cJSON_GetObjectItemCaseSensitive(location,"longitude")->valuedouble);
            cJSON *forecast=lookup_fetch(j);
            result=forecast ? lookup_weather(forecast,location,&j->query,time(NULL)) : lookup_error("source_unavailable");
            cJSON_Delete(forecast);
        }
        cJSON_Delete(geo);
        if (!result) result=lookup_error("source_unavailable");
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok")) && lookup_current(j)) {
        char *json=cJSON_PrintUnformatted(result);
        free(lookup_cache[slot].json); lookup_cache[slot].json=json;
        lookup_cache[slot].query=j->query; lookup_cache[slot].expires=esp_timer_get_time()+(slot ? 600000000 : 300000000);
        lookup_cache[slot].local_day=((int64_t)time(NULL)+25200)/86400;
    }
    return result;
}
static void lookup_worker(void *arg) {
    lookup_job_t *j=arg;
    cJSON *data=lookup_run(j);
    j->result.response=cJSON_PrintUnformatted(data); cJSON_Delete(data);
    free(j->body); j->body=NULL;
    portENTER_CRITICAL(&lookup_mux); lookup_done=true; portEXIT_CRITICAL(&lookup_mux);
    /* No further access to j: live_send owns the completed job from here. */
    vTaskDelete(NULL);
}
bool voice_lookup_submit(uint32_t epoch,uint32_t generation,const char *id,const cJSON *args,
                         bool (*current)(uint32_t,uint32_t)) {
    if (!id || !id[0] || strlen(id)>=128 || !current || voice_lookup_busy()) return false;
    lookup_job_t *j=heap_caps_calloc(1,sizeof(*j),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!j) return false;
    if (!lookup_query_parse(args,&j->query) ||
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<14336 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<6144) { free(j); return false; }
    j->result.epoch=epoch; j->result.generation=generation; strcpy(j->result.id,id);
    j->current=current; j->deadline=esp_timer_get_time()+12000000;
    portENTER_CRITICAL(&lookup_mux); lookup_job=j; lookup_done=false; portEXIT_CRITICAL(&lookup_mux);
    if (xTaskCreate(lookup_worker,"info_lookup",6144,j,3,NULL)!=pdPASS) {
        portENTER_CRITICAL(&lookup_mux); lookup_job=NULL; portEXIT_CRITICAL(&lookup_mux); free(j); return false;
    }
    return true;
}
voice_lookup_result_t *voice_lookup_take(void) {
    portENTER_CRITICAL(&lookup_mux);
    lookup_job_t *j=lookup_done ? lookup_job : NULL;
    if (j) { lookup_job=NULL; lookup_done=false; }
    portEXIT_CRITICAL(&lookup_mux); return j ? &j->result : NULL;
}
void voice_lookup_free(voice_lookup_result_t *result) {
    if (result) { cJSON_free(result->response); free(result); }
}
