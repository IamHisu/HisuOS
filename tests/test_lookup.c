#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include "alert_mock.h"
static cJSON *(*normal_json_parse)(const char *)=cJSON_Parse;
static void (*normal_json_delete)(cJSON *)=cJSON_Delete;
#define MALLOC_CAP_INTERNAL 4
#define pdPASS 1
static char lookup_reason[48];
#define ESP_LOGW(tag,format,reason,...) ((void)snprintf(lookup_reason,sizeof(lookup_reason),"%s",reason))
static unsigned lookup_completions,lookup_parse_samples;
static char lookup_complete_log[256];
static void lookup_info_log(const char *tag,const char *format,...) {
    char message[512]; va_list args; va_start(args,format);
    vsnprintf(message,sizeof(message),format,args); va_end(args);
    if (!strncmp(message,"Complete:",9)) {
        ++lookup_completions; strcpy(lookup_complete_log,message);
    }
    if (strstr(message,"phase=after_json_parse")) ++lookup_parse_samples;
}
#undef ESP_LOGI
#define ESP_LOGI lookup_info_log
static void *lookup_last_psram;
static size_t lookup_last_psram_bytes;
static bool lookup_fail_print;
static struct { void *pointer; size_t bytes; } lookup_allocations[4096];
static size_t lookup_allocation_count,lookup_live_bytes;
static void lookup_record_allocation(void *pointer,size_t bytes) {
    if (!pointer) return;
    assert(lookup_allocation_count<4096);
    lookup_allocations[lookup_allocation_count].pointer=pointer;
    lookup_allocations[lookup_allocation_count++].bytes=bytes;
    lookup_live_bytes+=bytes;
    lookup_last_psram=pointer; lookup_last_psram_bytes=bytes;
}
static void lookup_forget_allocation(void *pointer) {
    for (size_t i=0;i<lookup_allocation_count;++i) if (lookup_allocations[i].pointer==pointer) {
        lookup_live_bytes-=lookup_allocations[i].bytes;
        lookup_allocations[i]=lookup_allocations[--lookup_allocation_count]; return;
    }
}
static void lookup_tracked_free(void *pointer) {
    lookup_forget_allocation(pointer); free(pointer);
}
static void *lookup_psram_malloc(size_t bytes,int caps) {
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (lookup_fail_print && bytes==1024) return NULL;
    void *out=heap_caps_malloc(bytes,caps);
    lookup_record_allocation(out,bytes);
    return out;
}
static void *heap_caps_realloc(void *pointer,size_t bytes,int caps) {
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) && bytes);
    if (!mock_alloc_budget) return NULL;
    if (mock_alloc_budget>0) --mock_alloc_budget;
    void *out=realloc(pointer,bytes);
    if (out) { lookup_forget_allocation(pointer); lookup_record_allocation(out,bytes); }
    return out;
}
#define heap_caps_malloc lookup_psram_malloc
#define free lookup_tracked_free
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static size_t lookup_free_ram=32768,lookup_largest_ram=16384;
static size_t heap_caps_get_free_size(int caps) { return lookup_free_ram; }
static size_t heap_caps_get_largest_free_block(int caps) { return lookup_largest_ram; }
static void *heap_caps_calloc(size_t n,size_t bytes,int caps) { return mock_alloc_budget==0 ? NULL : calloc(n,bytes); }
static unsigned lookup_stack_free=3072;
static unsigned uxTaskGetStackHighWaterMark(void *task) { return lookup_stack_free; }
static void (*lookup_task)(void *);
static void *lookup_task_arg;
static bool task_fail,lookup_valid=true,lookup_online=true;
typedef void *TaskHandle_t;
enum { eRunning, eSuspended };
static bool lookup_suspended;
static unsigned lookup_deleted;
static void (*lookup_state_hook)(void);
static int xTaskCreateWithCaps(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle,int caps) {
    assert(stack==8192 && priority==3 && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (task_fail) return 0;
    lookup_task=fn; lookup_task_arg=arg; lookup_suspended=false; *handle=(void *)1; return pdPASS;
}
static void vTaskSuspend(void *task) { assert(!task); lookup_suspended=true; }
static int eTaskGetState(TaskHandle_t task) {
    assert(task==(void *)1);
    if (lookup_state_hook) { void (*hook)(void)=lookup_state_hook; lookup_state_hook=NULL; hook(); }
    return lookup_suspended ? eSuspended : eRunning;
}
static void vTaskDeleteWithCaps(TaskHandle_t task) { assert(task==(void *)1 && lookup_suspended); ++lookup_deleted; }
static struct tm *lookup_gmtime(const time_t *t,struct tm *out) { return gmtime_s(out,t)==0 ? out : NULL; }
#define gmtime_r lookup_gmtime
static time_t lookup_now=1792483200;
static time_t lookup_time(time_t *out) { if (out) *out=lookup_now; return lookup_now; }
#define time lookup_time
static unsigned lookup_http_started;
static char *forecast_wire;
static const char *news_wire;
static bool news_missing;
static esp_http_client_handle_t lookup_http_init(const esp_http_client_config_t *config) {
    assert(config->crt_bundle_attach && config->is_async && config->timeout_ms==1000 && config->disable_auto_redirect);
    assert(!strncmp(config->url,"https://www.vang.today/api/prices",31) ||
        !strncmp(config->url,"https://news.google.com/rss/search?",sizeof("https://news.google.com/rss/search?")-1) ||
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
    if (strstr(http->config.url,"news.google.com")) {
        wire=news_wire;
        if (news_missing && strstr(http->config.url,"AI")) wire="<rss><channel/></rss>";
    }
    if (mock_mode==3) wire="<html>not JSON</html>";
    esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.user_data=http->config.user_data,
        .data=(void *)wire,.data_len=mock_mode==5 ? (strstr(http->config.url,"news.google.com") ? 300000 : 20000) : (int)strlen(wire)};
    return http->config.event_handler(&event);
}
#define esp_http_client_init lookup_http_init
#define esp_http_client_perform lookup_http_perform
#include "../components/hisu_voice/src/hisu_voice_lookup_json.c"
#include "../components/hisu_voice/src/hisu_voice_lookup_data.c"
#include "../components/hisu_voice/src/hisu_voice_news.c"
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
    unsigned before=lookup_completions;
    lookup_task(lookup_task_arg);
    assert(lookup_completions==before+1 && strstr(lookup_complete_log,"stack_bytes=8192"));
    assert(strstr(lookup_complete_log,lookup_stack_free<1536 ? "stack_low=1" : "stack_low=0"));
    voice_lookup_result_t *result=voice_lookup_take();
    assert(result && result->response==lookup_last_psram && lookup_last_psram_bytes<=32768);
    cJSON *o=decode_result(result); assert(!voice_lookup_busy() && !mock_http_live); return o;
}
static void assert_error(cJSON *o,const char *error) {
    assert(cJSON_IsFalse(field(o,"ok")) && !strcmp(field(o,"error")->valuestring,error)); cJSON_Delete(o);
}
static void concurrent_take(void) { assert(!voice_lookup_take() && voice_lookup_busy()); }
static void reset_lookup(void) {
    assert(!voice_lookup_busy() && !mock_http_live);
    mock_now+=20000000; mock_mode=0; mock_http_status=200; lookup_valid=lookup_online=true;
    lookup_free_ram=32768; lookup_largest_ram=16384; mock_alloc_budget=-1; task_fail=false;
    lookup_stack_free=3072;
    lookup_fail_print=false;
    for (unsigned i=0;i<3;++i) { free(lookup_cache[i].json); lookup_cache[i].json=NULL; }
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
static void lookup_psram_tests(void) {
    cJSON *value=cJSON_CreateObject(); assert(value);
    assert(cJSON_AddStringToObject(value,"value","small"));
    char *json=lookup_print(value);
    assert(json && json==lookup_last_psram && lookup_last_psram_bytes==1024);
    assert(!strcmp(json,"{\"value\":\"small\"}")); free(json);
    cJSON_Delete(value);
    char *large=malloc(LOOKUP_JSON_LIMIT+1); assert(large);
    memset(large,'x',LOOKUP_JSON_LIMIT); large[LOOKUP_JSON_LIMIT]=0;
    large[4096]=0; value=cJSON_CreateString(large); assert(value);
    json=lookup_print(value); assert(json && json==lookup_last_psram && lookup_last_psram_bytes==8192);
    assert(strlen(json)==4098); free(json);
    mock_alloc_budget=0; assert(!lookup_print(value));
    mock_alloc_budget=1; assert(!lookup_print(value)); /* Growth allocation fails cleanly. */
    mock_alloc_budget=-1; cJSON_Delete(value);
    large[4096]='x'; value=cJSON_CreateString(large); assert(value);
    assert(!lookup_print(value)); cJSON_Delete(value); free(large);
    assert(!lookup_print(NULL));
}
static void lookup_cache_failure_test(void) {
    reset_lookup();
    cJSON *result=run_lookup("{\"topic\":\"gold\",\"gold_type\":\"SJL1L10\"}"); cJSON_Delete(result);
    char *saved=lookup_cache[0].json; assert(saved);
    lookup_query_t query=lookup_cache[0].query;
    int64_t expires=lookup_cache[0].expires;
    mock_now+=3000000;
    lookup_job_t job={.result={.epoch=7,.generation=9},.current=valid_turn,.deadline=mock_now+12000000};
    cJSON *args=cJSON_Parse("{\"topic\":\"gold\",\"gold_type\":\"XAUUSD\"}"); assert(args);
    assert(lookup_query_parse(args,&job.query)); cJSON_Delete(args);
    lookup_fail_print=true; /* Parsing succeeds, replacement cache print fails. */
    result=lookup_run(&job);
    assert(cJSON_IsTrue(field(result,"ok")));
    assert(lookup_cache[0].json==saved && lookup_cache[0].expires==expires &&
        !memcmp(&query,&lookup_cache[0].query,sizeof(query)));
    cJSON_Delete(result); free(job.body); lookup_fail_print=false;
    unsigned before=lookup_http_started;
    lookup_stack_free=1000;
    result=run_lookup("{\"topic\":\"gold\",\"gold_type\":\"SJL1L10\"}");
    assert(cJSON_IsTrue(field(result,"cached")) && lookup_http_started==before);
    assert(strstr(lookup_complete_log,"stack_free=1000") && strstr(lookup_complete_log,"stack_low=1"));
    cJSON_Delete(result);
    args=cJSON_Parse("{\"topic\":\"gold\",\"gold_type\":\"SJL1L10\"}"); assert(args);
    assert(voice_lookup_submit(7,9,"test",args,valid_turn)); cJSON_Delete(args);
    mock_alloc_budget=0; lookup_task(lookup_task_arg);
    voice_lookup_result_t *failed=voice_lookup_take();
    assert(failed && !failed->response && !voice_lookup_busy() && !mock_http_live);
    assert(lookup_cache[0].json==saved && strstr(lookup_complete_log,"response_bytes=0"));
    voice_lookup_free(failed); reset_lookup();
}
static void private_json_tests(void) {
    size_t baseline=lookup_live_bytes;
    mock_alloc_budget=0;
    cJSON *normal=normal_json_parse("{\"topic\":\"weather\",\"place\":\"Test city\"}");
    assert(normal && !cJSON_Parse("{\"a\":1}"));
    lookup_query_t query; assert(lookup_query_parse(normal,&query));
    assert(query.weather && !strcmp(query.place,"Test city") && lookup_live_bytes==baseline);
    normal_json_delete(normal); mock_alloc_budget=-1;
    unsigned successes=0;
    for (int budget=0;budget<640;++budget) {
        mock_alloc_budget=budget;
        cJSON *tree=cJSON_ParseWithOpts(forecast_wire,NULL,true);
        if (tree) ++successes;
        cJSON_Delete(tree);
        assert(lookup_live_bytes==baseline);
    }
    assert(successes); mock_alloc_budget=-1;
    voice_lookup_result_t job={.response="{\"ok\":true,\"place\":\"Test city\",\"hourly\":[{\"hour\":17,\"rain_probability_percent\":70}]}"};
    strcpy(job.id,"test\"\\\n\t-id");
    successes=0;
    for (int budget=0;budget<32;++budget) {
        mock_alloc_budget=budget;
        char *body=voice_lookup_encode_result(&job);
        mock_alloc_budget=-1;
        if (body) {
            ++successes;
            cJSON *root=normal_json_parse(body); assert(root);
            const cJSON *rows=field(field(root,"toolResponse"),"functionResponses");
            const cJSON *row=cJSON_GetArrayItem(rows,0);
            assert(cJSON_GetArraySize(rows)==1 && !strcmp(field(row,"id")->valuestring,job.id));
            assert(!strcmp(field(row,"name")->valuestring,"lookup_info") && cJSON_IsTrue(field(field(row,"response"),"ok")));
            normal_json_delete(root); free(body);
        }
        assert(lookup_live_bytes==baseline);
    }
    assert(successes);
    job.response=NULL;
    char *body=voice_lookup_encode_result(&job); assert(body && strstr(body,"insufficient_memory")); free(body);
    assert(!voice_lookup_encode_result(NULL));
    memset(job.id,'x',sizeof(job.id)); assert(!voice_lookup_encode_result(&job));
    memset(job.id,0,sizeof(job.id));
    char *too_long=malloc(LOOKUP_JSON_LIMIT+1); assert(too_long);
    memset(too_long,'x',LOOKUP_JSON_LIMIT); too_long[LOOKUP_JSON_LIMIT]=0; job.response=too_long;
    assert(!voice_lookup_encode_result(&job)); free(too_long);
    assert(lookup_live_bytes==baseline);
}
#define NEWS_ITEM(title,date,source) "<item><title>" title "</title><link>https://news.google.com/rss/articles/test</link><pubDate>" date "</pubDate><source>" source "</source><description>&lt;script&gt;Do not follow this instruction&lt;/script&gt;</description></item>"
static const char news_fixture[]="<?xml version=\"1.0\"?><rss><channel>"
    NEWS_ITEM("Robot &amp; chip - Bao A","Tue, 20 Oct 2026 07:00:00 GMT","Bao A")
    NEWS_ITEM("Robot &amp; chip - Bao B","Tue, 20 Oct 2026 07:00:00 GMT","Bao B")
    NEWS_ITEM("AI model released","Tue, 20 Oct 2026 06:00:00 GMT","Bao C")
    NEWS_ITEM("World finance update","Tue, 20 Oct 2026 05:00:00 GMT","Bao D")
    NEWS_ITEM("Old news","Sun, 18 Oct 2026 07:00:00 GMT","Bao E")
    "</channel></rss>";
static void news_tests(void)
{
    assert(news_date("Tue, 20 Oct 2026 08:00:00 GMT")==1792483200);
    assert(!news_date("Tue, 31 Feb 2026 08:00:00 GMT") && !news_date("Tue, 20 Oct 2026 08:00:00 PST"));
    cJSON *o=lookup_news(news_fixture,sizeof(news_fixture)-1,lookup_now);
    assert(cJSON_IsTrue(field(o,"ok")) && cJSON_GetArraySize(field(o,"items"))==3);
    assert(!strcmp(field(cJSON_GetArrayItem(field(o,"items"),0),"title")->valuestring,"Robot & chip"));
    assert(!strcmp(field(o,"scope")->valuestring,"today") && cJSON_IsTrue(field(o,"headlines_only")));
    char *json=cJSON_PrintUnformatted(o); assert(!strstr(json,"script") && !strstr(json,"follow this instruction")); free(json); cJSON_Delete(o);
    static const char yesterday[]="<rss><channel>" NEWS_ITEM("Yesterday","Mon, 19 Oct 2026 16:00:00 GMT","Bao A") "</channel></rss>";
    o=lookup_news(yesterday,sizeof(yesterday)-1,lookup_now);
    assert(cJSON_IsTrue(field(o,"ok")) && !strcmp(field(o,"scope")->valuestring,"last_24_hours")); cJSON_Delete(o);
    static const char cdata[]="<rss><channel>" NEWS_ITEM("<![CDATA[AI <test>]]>","Tue, 20 Oct 2026 07:00:00 GMT","Bao A") "</channel></rss>";
    o=lookup_news(cdata,sizeof(cdata)-1,lookup_now); assert(cJSON_IsTrue(field(o,"ok"))); cJSON_Delete(o);
    const char *bad[]={"<html/>","<rss><channel><item>","<!DOCTYPE rss><rss><channel/></rss>",
        "<rss><channel><item><title>&unknown;</title></item></channel></rss>","<rss><channel></rss>"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) assert_error(lookup_news(bad[i],strlen(bad[i]),lookup_now),"invalid_source_data");
    assert_error(lookup_news("<rss><channel/></rss>",21,lookup_now),"no_recent_news");
    assert_error(lookup_news(news_fixture,sizeof(news_fixture)-1,0),"clock_unsynced");
    char nul[]="<rss>\0</rss>"; assert_error(lookup_news(nul,sizeof(nul)-1,lookup_now),"invalid_source_data");
    mock_alloc_budget=0; assert(!lookup_news(news_fixture,sizeof(news_fixture)-1,lookup_now)); mock_alloc_budget=-1;
    reset_lookup(); news_wire=news_fixture; news_missing=false;
    unsigned before=lookup_http_started;
    o=run_lookup("{\"topic\":\"news\"}");
    assert(cJSON_IsTrue(field(o,"ok")) && cJSON_GetArraySize(field(o,"items"))==3 && lookup_http_started==before+3);
    assert(!strcmp(field(cJSON_GetArrayItem(field(o,"items"),0),"topic")->valuestring,"technology_robotics"));
    assert(!strcmp(field(cJSON_GetArrayItem(field(o,"items"),1),"topic")->valuestring,"ai_models"));
    assert(!strcmp(field(cJSON_GetArrayItem(field(o,"items"),2),"topic")->valuestring,"economy_finance"));
    cJSON_Delete(o); before=lookup_http_started;
    o=run_lookup("{\"topic\":\"news\"}"); assert(cJSON_IsTrue(field(o,"cached")) && lookup_http_started==before); cJSON_Delete(o);
    mock_now+=901000000; o=run_lookup("{\"topic\":\"news\"}"); assert(lookup_http_started==before+3); cJSON_Delete(o);
    before=lookup_http_started; mock_now+=3000000; lookup_now+=86400;
    assert_error(run_lookup("{\"topic\":\"news\"}"),"news_unavailable");
    assert(lookup_http_started==before+3); lookup_now-=86400;
    reset_lookup(); news_missing=true;
    o=run_lookup("{\"topic\":\"news\"}"); assert(cJSON_IsTrue(field(o,"partial")) && cJSON_GetArraySize(field(o,"items"))==2); cJSON_Delete(o);
    reset_lookup(); news_missing=false; mock_mode=3;
    assert_error(run_lookup("{\"topic\":\"news\"}"),"news_unavailable");
    reset_lookup(); mock_mode=5; assert_error(run_lookup("{\"topic\":\"news\"}"),"news_unavailable");
    reset_lookup(); mock_mode=7; assert_error(run_lookup("{\"topic\":\"news\"}"),"news_unavailable");
    reset_lookup(); mock_mode=8; assert_error(run_lookup("{\"topic\":\"news\"}"),"news_unavailable");
    reset_lookup();
}
int main(int argc,char **argv) {
    if (argc==4 && !strcmp(argv[1],"--news")) {
        FILE *f=fopen(argv[2],"rb"); assert(f);
        char *body=malloc(HISU_NEWS_BODY_LIMIT+1); assert(body);
        size_t bytes=fread(body,1,HISU_NEWS_BODY_LIMIT+1,f); fclose(f); assert(bytes<=HISU_NEWS_BODY_LIMIT);
        cJSON *result=lookup_news(body,bytes,_strtoi64(argv[3],NULL,10));
        assert(cJSON_IsTrue(field(result,"ok")));
        printf("Live RSS parsed: %d recent headlines, scope=%s\n",cJSON_GetArraySize(field(result,"items")),field(result,"scope")->valuestring);
        cJSON_Delete(result); free(body); return 0;
    }
    lookup_psram_tests();
    lookup_cache_failure_test();
    news_tests();
    make_forecast(); reset_lookup();
    private_json_tests();
    cJSON *args=cJSON_Parse("{\"topic\":\"gold\",\"gold_type\":\"SJL1L10\"}");
    lookup_query_t q; assert(lookup_query_parse(args,&q));
    const char *bad[]={"{}","{\"topic\":\"gold\",\"url\":\"https://evil\"}","{\"topic\":\"weather\",\"day_offset\":0.5}",
        "{\"topic\":\"weather\",\"day_offset\":3}","{\"topic\":\"gold\",\"gold_type\":\"unknown\"}","{\"topic\":\"weather\",\"place\":\"abc\\nxyz\"}",
        "{\"topic\":\"news\",\"place\":\"abc\"}","{\"topic\":\"news\",\"url\":\"https://evil\"}"};
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
    assert(lookup_parse_samples>0);
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
    assert(!strcmp(lookup_reason,"worker_allocation"));
    task_fail=false; lookup_free_ram=10239; assert(!voice_lookup_submit(7,9,"test",args,valid_turn));
    assert(!strcmp(lookup_reason,"internal_memory"));
    lookup_free_ram=12623; lookup_largest_ram=7680;
    unsigned deleted=lookup_deleted;
    assert(voice_lookup_submit(7,9,"test",args,valid_turn));
    lookup_task(lookup_task_arg);
    lookup_suspended=false; assert(!voice_lookup_take() && voice_lookup_busy() && lookup_deleted==deleted);
    lookup_suspended=true; lookup_state_hook=concurrent_take;
    o=decode_result(voice_lookup_take()); assert(cJSON_IsTrue(field(o,"ok"))); cJSON_Delete(o);
    assert(!voice_lookup_busy() && lookup_deleted==deleted+1);
    lookup_free_ram=32768; lookup_largest_ram=2047;
    assert(!voice_lookup_submit(7,9,"test",args,valid_turn) && !voice_lookup_busy());
    lookup_largest_ram=16384;
    reset_lookup(); lookup_free_ram=12623; lookup_largest_ram=7680;
    assert(voice_lookup_submit(7,9,"test",args,valid_turn));
    calls=lookup_http_started; lookup_free_ram=6143;
    lookup_task(lookup_task_arg);
    assert_error(decode_result(voice_lookup_take()),"source_unavailable");
    assert(lookup_http_started==calls && !voice_lookup_busy());
    lookup_free_ram=32768; mock_alloc_budget=0; assert(!voice_lookup_submit(7,9,"test",args,valid_turn));
    reset_lookup(); lookup_now+=200000;
    assert_error(run_lookup("{\"topic\":\"gold\",\"gold_type\":\"SJL1L10\"}"),"stale_data"); lookup_now-=200000;
    cJSON_Delete(args); reset_lookup(); free(forecast_wire);
    assert(!lookup_live_bytes && !lookup_allocation_count);
    puts("Lookup tests passed: bounded HTTP, TLS host allowlist, cache, gold units, hourly weather, clarification, cancellation, timeout, memory guard.");
    return 0;
}
