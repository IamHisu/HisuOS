#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <time.h>
#include "alert_mock.h"
#define MALLOC_CAP_INTERNAL 4
#define pdPASS 1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static size_t lookup_free_ram=32768,lookup_largest_ram=16384;
static size_t heap_caps_get_free_size(int caps) { return lookup_free_ram; }
static size_t heap_caps_get_largest_free_block(int caps) { return lookup_largest_ram; }
static void *heap_caps_calloc(size_t n,size_t bytes,int caps) { return mock_alloc_budget==0 ? NULL : calloc(n,bytes); }
static unsigned uxTaskGetStackHighWaterMark(void *task) { return 3072; }
static void (*lookup_task)(void *);
static void *lookup_task_arg;
static bool task_fail,lookup_valid=true,lookup_online=true;
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    assert(stack==6144 && priority==3); if (task_fail) return 0; lookup_task=fn; lookup_task_arg=arg; return pdPASS;
}
static void vTaskDelete(void *task) { assert(!task); }
static struct tm *lookup_gmtime(const time_t *t,struct tm *out) { return gmtime_s(out,t)==0 ? out : NULL; }
#define gmtime_r lookup_gmtime
static time_t lookup_now=1792483200;
static time_t lookup_time(time_t *out) { if (out) *out=lookup_now; return lookup_now; }
#define time lookup_time
static unsigned lookup_http_started;
static char *forecast_wire;
static esp_http_client_handle_t lookup_http_init(const esp_http_client_config_t *config) {
    assert(config->crt_bundle_attach && config->is_async && config->timeout_ms==1000 && config->disable_auto_redirect);
    assert(!strncmp(config->url,"https://www.vang.today/api/prices",31) ||
        !strncmp(config->url,"https://geocoding-api.open-meteo.com/v1/search?",44) ||
        !strncmp(config->url,"https://api.open-meteo.com/v1/forecast?",37));
    ++lookup_http_started; esp_http_client_handle_t http=calloc(1,sizeof(*http));
    if (http) { http->config=*config; ++mock_http_live; } return http;
}
static const char *gold_wire="{\"success\":true,\"timestamp\":1792483200,\"prices\":{\"SJL1L10\":{\"buy\":139600000,\"sell\":142600000,\"currency\":\"VND\"},\"XAUUSD\":{\"buy\":4123,\"sell\":0,\"currency\":\"USD\"}}}";
static const char *geo_wire="{\"results\":[{\"id\":1586203,\"name\":\"Cần Thơ\",\"country_code\":\"VN\",\"latitude\":10.03,\"longitude\":105.78}]}";
static esp_err_t lookup_http_perform(esp_http_client_handle_t http) {
    ++mock_http_calls;
    if (!http->calls++ || mock_mode==7) return ESP_ERR_HTTP_EAGAIN;
    if (mock_mode==8) lookup_valid=false;
    const char *wire=strstr(http->config.url,"/prices") ? gold_wire : strstr(http->config.url,"/search?") ? geo_wire : forecast_wire;
    if (mock_mode==3) wire="<html>not JSON</html>";
    esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.user_data=http->config.user_data,
        .data=(void *)wire,.data_len=mock_mode==5 ? 20000 : (int)strlen(wire)};
    return http->config.event_handler(&event);
}
#define esp_http_client_init lookup_http_init
#define esp_http_client_perform lookup_http_perform
#include "../components/hisu_voice/src/hisu_voice_lookup_data.c"
#include "../components/hisu_voice/src/hisu_voice_lookup.c"
void hisu_network_get_link(hisu_network_link_t *out) {
    *out=(hisu_network_link_t){.connected=lookup_online,.internet=lookup_online ? HISU_INTERNET_ONLINE : HISU_INTERNET_OFFLINE};
}
static bool valid_turn(uint32_t epoch,uint32_t generation) { assert(epoch==7 && generation==9); return lookup_valid; }
static cJSON *decode_result(voice_lookup_result_t *r) {
    assert(r && r->epoch==7 && r->generation==9 && !strcmp(r->id,"test"));
    cJSON *o=cJSON_Parse(r->response); assert(o); voice_lookup_free(r); return o;
}
static cJSON *run_lookup(const char *args) {
    cJSON *a=cJSON_Parse(args); assert(a);
    assert(voice_lookup_submit(7,9,"test",a,valid_turn)); cJSON_Delete(a);
    assert(voice_lookup_busy() && !voice_lookup_take());
    lookup_task(lookup_task_arg);
    cJSON *o=decode_result(voice_lookup_take()); assert(!voice_lookup_busy() && !mock_http_live); return o;
}
static void assert_error(cJSON *o,const char *error) {
    assert(cJSON_IsFalse(field(o,"ok")) && !strcmp(field(o,"error")->valuestring,error)); cJSON_Delete(o);
}
static void reset_lookup(void) {
    assert(!voice_lookup_busy() && !mock_http_live);
    mock_now+=20000000; mock_mode=0; mock_http_status=200; lookup_valid=lookup_online=true;
    lookup_free_ram=32768; lookup_largest_ram=16384; mock_alloc_budget=-1; task_fail=false;
    for (unsigned i=0;i<2;++i) { free(lookup_cache[i].json); lookup_cache[i].json=NULL; }
}
static void make_forecast(void) {
    cJSON *o=cJSON_Parse("{\"utc_offset_seconds\":25200,\"current\":{\"time\":\"2026-10-20T15:00\",\"weather_code\":95,\"temperature_2m\":26.5},\"current_units\":{\"temperature_2m\":\"°C\"},\"hourly_units\":{\"temperature_2m\":\"°C\",\"precipitation\":\"mm\",\"precipitation_probability\":\"%\"}}");
    cJSON *h=cJSON_AddObjectToObject(o,"hourly"),*t=cJSON_AddArrayToObject(h,"time"),*v=cJSON_AddArrayToObject(h,"temperature_2m"),
        *r=cJSON_AddArrayToObject(h,"precipitation"),*p=cJSON_AddArrayToObject(h,"precipitation_probability");
    for (int i=0;i<72;++i) {
        char stamp[32]; snprintf(stamp,sizeof(stamp),"2026-10-%02dT%02d:00",20+i/24,i%24);
        cJSON_AddItemToArray(t,cJSON_CreateString(stamp)); cJSON_AddItemToArray(v,cJSON_CreateNumber(28));
        cJSON_AddItemToArray(r,cJSON_CreateNumber(1)); cJSON_AddItemToArray(p,cJSON_CreateNumber(70));
    }
    forecast_wire=cJSON_PrintUnformatted(o); cJSON_Delete(o);
}
int main(void) {
    make_forecast(); reset_lookup();
    cJSON *args=cJSON_Parse("{\"topic\":\"gold\",\"gold_type\":\"SJL1L10\"}");
    lookup_query_t q; assert(lookup_query_parse(args,&q));
    const char *bad[]={"{}","{\"topic\":\"gold\",\"url\":\"https://evil\"}","{\"topic\":\"weather\",\"day_offset\":0.5}",
        "{\"topic\":\"weather\",\"day_offset\":3}","{\"topic\":\"gold\",\"gold_type\":\"unknown\"}","{\"topic\":\"weather\",\"place\":\"abc\\nxyz\"}"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) { cJSON *a=cJSON_Parse(bad[i]); assert(!lookup_query_parse(a,&q)); cJSON_Delete(a); }
    assert(voice_lookup_submit(7,9,"test",args,valid_turn)); assert(!voice_lookup_submit(7,9,"test",args,valid_turn));
    lookup_task(lookup_task_arg); cJSON *o=decode_result(voice_lookup_take()); assert(cJSON_IsTrue(field(o,"ok")));
    const cJSON *price=cJSON_GetArrayItem(field(o,"prices"),0); assert(field(price,"buy")->valuedouble==139600000);
    assert(!strcmp(field(price,"unit")->valuestring,"VND/lượng")); cJSON_Delete(o);
    unsigned calls=lookup_http_started; o=run_lookup("{\"topic\":\"gold\",\"gold_type\":\"SJL1L10\"}");
    assert(lookup_http_started==calls && cJSON_IsTrue(field(o,"cached"))); cJSON_Delete(o);
    reset_lookup(); o=run_lookup("{\"topic\":\"gold\",\"gold_type\":\"XAUUSD\"}");
    price=cJSON_GetArrayItem(field(o,"prices"),0); assert(!field(price,"sell") && field(price,"spot_reference")); cJSON_Delete(o);
    reset_lookup(); assert_error(run_lookup("{\"topic\":\"weather\"}"),"place_required");
    reset_lookup(); o=run_lookup("{\"topic\":\"weather\",\"place\":\"Cần Thơ\",\"day_offset\":1}");
    assert(cJSON_IsTrue(field(o,"ok")) && cJSON_GetArraySize(field(o,"hourly"))==24 && !strcmp(field(o,"forecast_date")->valuestring,"2026-10-21")); cJSON_Delete(o);
    cJSON *geo=cJSON_Parse("{\"results\":[{\"id\":1,\"name\":\"ABC\",\"country_code\":\"VN\",\"latitude\":10,\"longitude\":105},{\"id\":2,\"name\":\"ABC\",\"country_code\":\"VN\",\"latitude\":11,\"longitude\":106}]}");
    q=(lookup_query_t){.weather=true}; cJSON *error=NULL;
    assert(!lookup_location(geo,&q,&error)); assert_error(error,"place_ambiguous");
    q.location_id=2; assert(lookup_location(geo,&q,&error)); q.location_id=999; assert(!lookup_location(geo,&q,&error)); cJSON_Delete(error); cJSON_Delete(geo);
    for (unsigned i=0;i<4;++i) {
        reset_lookup(); mock_mode=(int[]){3,5,6,7}[i];
        assert_error(run_lookup("{\"topic\":\"gold\",\"gold_type\":\"SJL1L10\"}"),"source_unavailable");
    }
    reset_lookup(); mock_http_status=302; assert_error(run_lookup("{\"topic\":\"gold\"}"),"source_unavailable");
    reset_lookup(); mock_mode=8; o=run_lookup("{\"topic\":\"gold\"}"); cJSON_Delete(o); assert(!lookup_cache[0].json);
    reset_lookup(); task_fail=true; assert(!voice_lookup_submit(7,9,"test",args,valid_turn) && !voice_lookup_busy());
    task_fail=false; lookup_free_ram=14000; assert(!voice_lookup_submit(7,9,"test",args,valid_turn));
    lookup_free_ram=32768; mock_alloc_budget=0; assert(!voice_lookup_submit(7,9,"test",args,valid_turn));
    reset_lookup(); lookup_now+=200000;
    assert_error(run_lookup("{\"topic\":\"gold\",\"gold_type\":\"SJL1L10\"}"),"stale_data"); lookup_now-=200000;
    cJSON_Delete(args); reset_lookup(); free(forecast_wire);
    puts("Lookup tests passed: bounded HTTP, TLS host allowlist, cache, gold units, hourly weather, clarification, cancellation, timeout, memory guard.");
    return 0;
}
