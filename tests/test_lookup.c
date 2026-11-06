#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include "alert_mock.h"
#include "hisu_settings.h"
#include "hisu_network.h"
#include "hisu_media.h"
#include "hisu_power.h"
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
static void (*lookup_worker_entry)(void *);
static bool lookup_worker_running;
static void run_lookup_worker(void *arg) {
    lookup_worker_running=true;
    lookup_worker_entry(arg);
    lookup_worker_running=false;
}
static void *lookup_task_arg;
static bool task_fail,lookup_valid=true,lookup_online=true;
static hisu_internet_state_t lookup_internet=HISU_INTERNET_ONLINE;
typedef void *TaskHandle_t;
enum { eRunning, eSuspended };
static bool lookup_suspended;
static unsigned lookup_deleted;
static void (*lookup_state_hook)(void);
static int xTaskCreateWithCaps(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle,int caps) {
    assert(stack==8192 && priority==3 && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (task_fail) return 0;
    lookup_worker_entry=fn; lookup_task=run_lookup_worker; lookup_task_arg=arg; lookup_suspended=false; *handle=(void *)1; return pdPASS;
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
static bool web_key=true;
static unsigned research_published;
static bool slang_enabled=true,slang_cancel;
static int slang_power=50;
static unsigned slang_writes;
static char *slang_stored;
int hisu_settings_get(hisu_setting_t key) { assert(key==HISU_SETTING_SLANG_AUTO); return slang_enabled; }
void hisu_power_get(hisu_power_status_t *out) { *out=(hisu_power_status_t){.valid=true,.percent=slang_power}; }
esp_err_t hisu_settings_load_slang(char *out,size_t capacity) {
    out[0]=0; if (!slang_stored) return ESP_ERR_NOT_FOUND;
    assert(strlen(slang_stored)<capacity); strcpy(out,slang_stored); return ESP_OK;
}
esp_err_t hisu_settings_save_slang(const char *json) {
    assert(mock_idle && lookup_valid && strlen(json)<=6144);
    char *copy=malloc(strlen(json)+1); assert(copy); strcpy(copy,json); free(slang_stored); slang_stored=copy; ++slang_writes; return ESP_OK;
}

static const char *web_wire="{\"status\":\"completed\",\"steps\":[{\"type\":\"google_search_call\"},{\"type\":\"google_search_result\",\"result\":[{\"search_suggestions\":\"<div>Search suggestions</div>\"}]},{\"type\":\"model_output\",\"content\":[{\"type\":\"text\",\"text\":\"Detailed sourced news, beyond the headline.\",\"annotations\":[{\"type\":\"url_citation\",\"title\":\"Source\",\"url\":\"https://example.com/news\",\"start_index\":0,\"end_index\":8}]}]}]}";
static const char *tech_qbit_wire="[{\"date_gmt\":\"2026-10-20T07:00:00\",\"link\":\"https://www.qbitai.com/2026/10/123.html\",\"title\":{\"rendered\":\"AI agent launch\"},\"excerpt\":{\"rendered\":\"<p>New robot chip research</p>\",\"protected\":false}}]";
static const char *tech_ithome_wire="{\"newslist\":[{\"postdate\":\"2026-10-20T15:30:00.123\",\"url\":\"/1/123/456.htm\",\"title\":\"AI chip launch\",\"description\":\"New open source processor\"}]}";
static const char *tech_good_wire="{\"candidates\":[{\"finishReason\":\"STOP\",\"content\":{\"parts\":[{\"text\":\"{\\\"items\\\":[{\\\"index\\\":0,\\\"summary\\\":\\\"Tin AI moi tu Trung Quoc.\\\"}]}\"}]}}]}";
static const char *tech_genk_wire="<li class=\"shownews\"><h4 class=\"knswli-title\"><a href=\"/ai-chip-moi.chn\" title=\"AI chip moi\">AI chip moi</a></h4><span class=\"knswli-sapo\">Robot moi tai Viet Nam</span><span class=\"knswli-time\" title=\"2026-10-20T15:00:00.000000\">hom nay</span></li>";
static bool tech_qbit_timeout;
static unsigned tech_qbit_attempts;
static unsigned tech_empty_summaries;
static unsigned article_gets;
static bool article_wrong_id;
static const char *article_ithome_wire="{\"success\":true,\"newsid\":1123456,\"detail\":\"<p>This processor uses a new open architecture, supports local AI inference and reduces memory transfers. These statements were reported by its developer.</p>\"}";
static const char *article_genk_wire="<html><div id=\"ContentDetail\"><p>Chip AI m&#7899;i gi&#250;p x&#7917; l&#253; ngay tr&#234;n thi&#7871;t b&#7883;. <b>Nh&#224; s&#7843;n xu&#7845;t</b> cho bi&#7871;t s&#7869; c&#244;ng b&#7889; th&#234;m th&#244;ng s&#7889; sau th&#7917; nghi&#7879;m.</p><script>steal_key()</script><div>Only the article body belongs here.</div></div><footer>UNRELATED FOOTER</footer></html>";
static const char *tech_tc_wire="[{\"id\":123,\"date_gmt\":\"2026-10-20T07:00:00\",\"link\":\"https://techcrunch.com/2026/10/ai-chip/\",\"title\":{\"rendered\":\"New AI processor\"},\"excerpt\":{\"rendered\":\"New robot hardware\",\"protected\":false}}]";
esp_err_t hisu_settings_get_gemini_profile(hisu_gemini_key_t profile,char out[128]) {
    assert(!lookup_worker_running);
    assert(profile==HISU_GEMINI_CHAT); strcpy(out,"fake-key"); return web_key ? ESP_OK : ESP_ERR_NOT_FOUND;
}
bool hisu_settings_has_gemini_profile(hisu_gemini_key_t profile) { assert(!lookup_worker_running); assert(profile==HISU_GEMINI_CHAT); return web_key; }
static uint32_t mock_news_store[512];
static size_t mock_news_bytes;
static esp_err_t mock_news_save_error;
static unsigned mock_news_saves;
esp_err_t hisu_settings_load_news_history(void *out,size_t *bytes) {
    assert(!lookup_worker_running);
    if (!mock_news_bytes) return ESP_ERR_NOT_FOUND;
    assert(*bytes>=mock_news_bytes); memcpy(out,mock_news_store,mock_news_bytes); *bytes=mock_news_bytes; return ESP_OK;
}
esp_err_t hisu_settings_save_news_history(const void *data,size_t bytes) {
    assert(!lookup_worker_running && bytes<=sizeof(mock_news_store));
    if (mock_news_save_error!=ESP_OK) return mock_news_save_error;
    memcpy(mock_news_store,data,bytes); mock_news_bytes=bytes; ++mock_news_saves; return ESP_OK;
}
bool hisu_media_research_publish(const char *json) {
    assert((strstr(json,"search_suggestions") && strstr(json,"https://example.com/news")) ||
        (strstr(json,"snippets_only") && (strstr(json,"https://www.qbitai.com/") || strstr(json,"https://genk.vn/") || strstr(json,"https://www.ithome.com/") || strstr(json,"https://techcrunch.com/")))); ++research_published; return true;
}
static esp_err_t lookup_http_post(esp_http_client_handle_t http,const char *body,int bytes) {
    assert(http->config.method==HTTP_METHOD_POST && bytes==(int)strlen(body));
    cJSON *request=cJSON_Parse(body); assert(request);
    if (strstr(http->config.url,"generateContent")) {
        assert(!cJSON_GetObjectItem(request,"tools"));
        assert(!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(request,"generationConfig"),"responseMimeType")->valuestring,"application/json"));
    } else {
        assert(cJSON_IsFalse(cJSON_GetObjectItem(request,"store")));
        assert(!strcmp(cJSON_GetObjectItem(request,"model")->valuestring,"gemini-2.5-flash-lite"));
    }
    cJSON_Delete(request); return ESP_OK;
}
#define esp_http_client_set_post_field lookup_http_post
static esp_http_client_handle_t lookup_http_init(const esp_http_client_config_t *config) {
    if (strstr(config->url,"qbitai.com")) ++tech_qbit_attempts;
    assert(config->crt_bundle_attach && config->is_async && config->timeout_ms==1000 && config->disable_auto_redirect);
    assert(config->buffer_size==(strstr(config->url,"en.wiktionary.org") ? 2048 : 4096) && config->buffer_size_tx==config->buffer_size);
    assert(!strncmp(config->url,"https://en.wiktionary.org/",26) ||
        !strncmp(config->url,"https://www.vang.today/api/prices",31) ||
        !strcmp(config->url,"https://generativelanguage.googleapis.com/v1beta/interactions") ||
        !strncmp(config->url,"https://genk.vn/ajax-home/page-",30) ||
        !strncmp(config->url,"https://www.qbitai.com/wp-json/wp/v2/posts?",sizeof("https://www.qbitai.com/wp-json/wp/v2/posts?")-1) ||
        !strcmp(config->url,"https://api.ithome.com/json/newslist/news") ||
        !strcmp(config->url,"https://api.ithome.com/json/newscontent/1123456") ||
        !strcmp(config->url,"https://genk.vn/ai-chip-moi.chn") ||
        strstr(config->url,"https://techcrunch.com/wp-json/wp/v2/posts?") ||
        !strcmp(config->url,"https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent") ||
        !strncmp(config->url,"https://geocoding-api.open-meteo.com/v1/search?",44) ||
        !strncmp(config->url,"https://api.open-meteo.com/v1/forecast?",37));
    ++lookup_http_started; esp_http_client_handle_t http=calloc(1,sizeof(*http));
    if (http) { http->config=*config; ++mock_http_live; } return http;
}
static const char *gold_wire="{\"success\":true,\"timestamp\":1792483200,\"prices\":{\"SJL1L10\":{\"buy\":139600000,\"sell\":142600000,\"currency\":\"VND\"},\"XAUUSD\":{\"buy\":4123,\"sell\":0,\"currency\":\"USD\"}}}";
static const char *geo_wire="{\"results\":[{\"id\":1586203,\"name\":\"Cần Thơ\",\"country_code\":\"VN\",\"latitude\":10.03,\"longitude\":105.78}]}";
static esp_err_t lookup_http_perform(esp_http_client_handle_t http) {
    ++mock_http_calls;
    if (tech_qbit_timeout && strstr(http->config.url,"qbitai.com")) return ESP_ERR_HTTP_EAGAIN;
    if (!http->calls++ || mock_mode==7) return ESP_ERR_HTTP_EAGAIN;
    if (mock_mode==8) lookup_valid=false;
    const char *history_wire="{\"success\":true,\"type\":\"SJL1L10\",\"history\":["
        "{\"date\":\"2026-10-20\",\"prices\":{\"SJL1L10\":{\"buy\":139600000,\"sell\":142600000}}},"
        "{\"date\":\"2026-10-19\",\"prices\":{\"SJL1L10\":{\"buy\":139000000,\"sell\":142000000}}}]}";
    const char *wire=strstr(http->config.url,"?type=SJL1L10&days=7") ? history_wire :
        strstr(http->config.url,"/prices") ? gold_wire : strstr(http->config.url,"/search?") ? geo_wire : forecast_wire;
    if (strstr(http->config.url,"/interactions")) wire=web_wire;
    if (strstr(http->config.url,"genk.vn")) wire=tech_genk_wire;
    if (strstr(http->config.url,"qbitai.com")) wire=tech_qbit_wire;
    if (strstr(http->config.url,"api.ithome.com")) wire=tech_ithome_wire;
    if (strstr(http->config.url,"/newscontent/")) { ++article_gets; wire=article_wrong_id ? "{\"success\":true,\"newsid\":9,\"detail\":\"wrong article\"}" : article_ithome_wire; }
    if (!strcmp(http->config.url,"https://genk.vn/ai-chip-moi.chn")) { ++article_gets; wire=article_genk_wire; }
    if (strstr(http->config.url,"generateContent")) wire=tech_good_wire;
    if (strstr(http->config.url,"generateContent") && tech_empty_summaries) {
        --tech_empty_summaries;
        wire="{\"candidates\":[{\"finishReason\":\"STOP\",\"content\":{\"parts\":[{\"text\":\"{\\\"items\\\":[]}\"}]}}]}";
    }
    if (strstr(http->config.url,"https://techcrunch.com/wp-json/wp/v2/posts?")) wire=tech_tc_wire;
    if (strstr(http->config.url,"en.wiktionary.org")) {
        if (slang_cancel) lookup_valid=false;
        wire=strstr(http->config.url,"/w/api.php") ?
            "{\"query\":{\"categorymembers\":[{\"pageid\":1,\"title\":\"test term\"},{\"pageid\":2,\"title\":\"second term\"}]}}" :
            "{\"vi\":[{\"definitions\":[{\"definition\":\"<b>friend</b> &amp; peer\"}]}]}";
    }
    if (mock_mode==3) wire="<html>not JSON</html>";
    esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.user_data=http->config.user_data,
        .data=(void *)wire,.data_len=mock_mode==5 ? ((strstr(http->config.url,"qbitai.com") || strstr(http->config.url,"api.ithome.com") || strstr(http->config.url,"genk.vn")) ? 200000 : strstr(http->config.url,"/interactions") || strstr(http->config.url,"generateContent") ? 70000 : 20000) : (int)strlen(wire)};
    return http->config.event_handler(&event);
}
#define esp_http_client_init lookup_http_init
#define esp_http_client_perform lookup_http_perform
static int tech_http_status(esp_http_client_handle_t http) {
    if (strstr(http->config.url,"/newscontent/") || !strcmp(http->config.url,"https://genk.vn/ai-chip-moi.chn")) return mock_http_status;
    if (strstr(http->config.url,"https://techcrunch.com/wp-json/wp/v2/posts?")) return 200;
    if (strstr(http->config.url,"qbitai.com") || strstr(http->config.url,"api.ithome.com") || strstr(http->config.url,"genk.vn")) return 200;
    return esp_http_client_get_status_code(http);
}
#define esp_http_client_get_status_code tech_http_status
#include "../components/hisu_voice/src/hisu_voice_lookup_json.c"
#include "../components/hisu_voice/src/hisu_voice_lookup_data.c"
#include "../components/hisu_voice/src/hisu_voice_news.c"
#include "../components/hisu_voice/src/hisu_voice_tech_news.c"
#include "../components/hisu_voice/src/hisu_voice_web.c"
#include "../components/hisu_voice/src/hisu_voice_lookup.c"
#include "../components/hisu_voice/src/hisu_voice_slang.c"
void hisu_network_get_link(hisu_network_link_t *out) {
    *out=(hisu_network_link_t){.connected=lookup_online,.internet=lookup_online ? lookup_internet : HISU_INTERNET_OFFLINE};
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
    assert(!lookup_job->web_key[0]);
    assert(strstr(lookup_complete_log,lookup_stack_free<1536 ? "stack_low=1" : "stack_low=0"));
    voice_lookup_result_t *result=voice_lookup_take();
    assert(result && result->response==lookup_last_psram && lookup_last_psram_bytes<=32768);
    cJSON *o=decode_result(result); assert(!voice_lookup_busy() && !mock_http_live); return o;
}
static void assert_error(cJSON *o,const char *error) {
    if (!o || !cJSON_IsFalse(field(o,"ok")) || !cJSON_IsString(field(o,"error")) || strcmp(field(o,"error")->valuestring,error)) {
        char *actual=cJSON_PrintUnformatted(o);
        fprintf(stderr,"Expected lookup error %s, got %s\n",error,actual ? actual : "NULL"); free(actual);
    }
    assert(cJSON_IsFalse(field(o,"ok")) && !strcmp(field(o,"error")->valuestring,error)); cJSON_Delete(o);
}
static void concurrent_take(void) { assert(!voice_lookup_take() && voice_lookup_busy()); }
static void reset_lookup(void) {
    tech_qbit_timeout=false; tech_qbit_attempts=0; tech_empty_summaries=0; article_gets=0; article_wrong_id=false;
    cJSON_Delete(lookup_news_pool); lookup_news_pool=NULL; lookup_news_expires=0; lookup_news_next=0;
    cJSON_Delete(lookup_news_recent); lookup_news_recent=NULL;
    memset(lookup_news_pages,0,sizeof(lookup_news_pages)); memset(lookup_news_retry,0,sizeof(lookup_news_retry));
    assert(!voice_lookup_busy() && !mock_http_live);
    mock_now+=20000000; mock_mode=0; mock_http_status=200; lookup_valid=lookup_online=true;
    lookup_free_ram=32768; lookup_largest_ram=16384; mock_alloc_budget=-1; task_fail=false;
    lookup_stack_free=3072; web_key=true; lookup_internet=HISU_INTERNET_ONLINE;
    lookup_fail_print=false;
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
static void lookup_failure_cooldown_test(void) {
    reset_lookup(); mock_mode=7;
    const char *args="{\"topic\":\"gold\",\"gold_type\":\"SJL1L10\"}";
    cJSON *o=run_lookup(args);
    assert(cJSON_IsString(field(o,"message")) && cJSON_IsString(field(o,"speech_hint")));
    assert_error(o,"source_unavailable");
    unsigned before=lookup_http_started;
    o=run_lookup(args);
    assert(cJSON_IsTrue(field(o,"cached")) && lookup_http_started==before);
    assert_error(o,"source_unavailable");
    mock_now+=15000001; mock_mode=0;
    o=run_lookup(args); assert(cJSON_IsTrue(field(o,"ok")) && lookup_http_started==before+1);
    cJSON_Delete(o); reset_lookup();
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
    char *body=voice_lookup_encode_result(&job); assert(body && !strstr(body,"insufficient_memory") && strstr(body,"message")); free(body);
    job.response="{\"ok\":false,\"error\":\"source_unavailable\",\"message\":\"Unavailable\",\"choices\":[{\"location_id\":1}]}";
    body=voice_lookup_encode_result(&job);
    assert(body && !strstr(body,"source_unavailable") && strstr(body,"Unavailable") && strstr(body,"location_id")); free(body);
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
static void tech_news_tests(void)
{
    free(tech_history); tech_history=NULL;
    cJSON *feed=tech_news_feed(lookup_now,false),*qbit=cJSON_Parse(tech_qbit_wire),*ithome=cJSON_Parse(tech_ithome_wire);
    assert(feed && qbit && ithome);
    assert(tech_news_append(feed,qbit,true,lookup_now));
    assert(tech_news_append(feed,qbit,true,lookup_now));
    assert(cJSON_GetArraySize(field(feed,"items"))==1);
    assert(tech_news_append(feed,ithome,false,lookup_now));
    assert(tech_news_append_genk(feed,tech_genk_wire,lookup_now));
    assert(tech_news_append_genk(feed,tech_genk_wire,lookup_now));
    assert(cJSON_GetArraySize(field(feed,"items"))==3);
    assert(!strstr(field(cJSON_GetArrayItem(field(feed,"items"),0),"description")->valuestring,"<p>"));
    assert(tech_date("2026-10-20T15:30:00.123",false)==1792481400);
    assert(!tech_date("2026-02-30T00:00:00",true));
    lookup_query_t query={.news=true}; strcpy(query.mode,"read");
    char *body=tech_news_request(&query,feed); assert(body && !strstr(body,"google_search"));
    cJSON *request=cJSON_Parse(body); assert(request && !field(request,"tools") && field(request,"systemInstruction"));
    cJSON_Delete(request); free(body);
    cJSON *root=cJSON_Parse(tech_good_wire),*result=tech_news_result(root,feed);
    assert(cJSON_IsTrue(field(result,"ok")) && !strcmp(field(result,"scope")->valuestring,"last_7_days"));
    assert(strstr(field(cJSON_GetArrayItem(field(result,"sources"),0),"url")->valuestring,"qbitai.com"));
    lookup_history_loaded=false; lookup_history_saved=0; mock_news_bytes=0; mock_news_saves=0;
    voice_lookup_history_load(); assert(lookup_history_loaded);
    tech_news_stage(result,7,9);
    assert(!tech_was_seen(feed,"https://www.qbitai.com/2026/10/123.html","AI agent launch",lookup_now));
    tech_news_spoken(7,10);
    assert(!tech_history->count);
    tech_news_spoken(7,9); assert(tech_history->count==1);
    tech_news_spoken(7,9); assert(tech_history->count==1);
    tech_news_stage(result,7,11); tech_news_spoken(7,11); assert(tech_history->count==1);
    voice_lookup_history_flush(); assert(mock_news_saves==1 && mock_news_bytes==28);
    voice_lookup_history_flush(); assert(mock_news_saves==1);
    cJSON *unheard=tech_news_feed(lookup_now,false);
    assert(cJSON_GetArraySize(field(unheard,"already_heard_titles"))==1);
    assert(tech_news_append(unheard,qbit,true,lookup_now) && !cJSON_GetArraySize(field(unheard,"items")));
    assert(tech_was_seen(unheard,"https://other.example/article","AI - AGENT launch!",lookup_now));
    cJSON_Delete(unheard);
    cJSON *repeat=tech_news_feed(lookup_now,true);
    assert(tech_news_append(repeat,qbit,true,lookup_now) && cJSON_GetArraySize(field(repeat,"items"))==1);
    cJSON_Delete(repeat);
    uint32_t snapshot[HISU_TECH_HISTORY_BYTES/4]; uint32_t saved_revision=0;
    size_t snapshot_bytes=tech_news_history_export(snapshot,sizeof(snapshot),lookup_now,&saved_revision);
    assert(snapshot_bytes==28 && saved_revision==tech_news_history_revision());
    snapshot[4]^=1; assert(!tech_news_history_import(snapshot,snapshot_bytes)); snapshot[4]^=1;
    assert(!tech_news_history_import(snapshot,snapshot_bytes-1));
    free(tech_history); tech_history=NULL; lookup_history_loaded=false;
    voice_lookup_history_load(); assert(lookup_history_loaded && tech_history->count==1);
    assert(tech_was_seen(feed,"https://www.qbitai.com/2026/10/123.html","AI agent launch",lookup_now));
    assert(!tech_was_seen(feed,"https://www.qbitai.com/2026/10/123.html","AI agent launch",lookup_now+7*86400));
    size_t expired_bytes=tech_news_history_export(snapshot,sizeof(snapshot),lookup_now+7*86400,&saved_revision);
    assert(expired_bytes==16 && snapshot[2]==0);
    tech_news_stage(result,7,12); tech_news_spoken(7,12);
    mock_news_save_error=ESP_FAIL; voice_lookup_history_flush(); assert(mock_news_saves==1);
    mock_news_save_error=ESP_OK; voice_lookup_history_flush(); assert(mock_news_saves==2);
    cJSON *stored_source=cJSON_GetArrayItem(cJSON_GetObjectItem(result,"sources"),0);
    for (unsigned record=0;record<130;++record) {
        char url[96],title[64];
        snprintf(url,sizeof(url),"https://www.qbitai.com/article/%u",record);
        snprintf(title,sizeof(title),"AI event %u",record);
        cJSON_ReplaceItemInObjectCaseSensitive(stored_source,"url",cJSON_CreateString(url));
        cJSON_ReplaceItemInObjectCaseSensitive(stored_source,"title",cJSON_CreateString(title));
        tech_news_stage(result,7,20+record); tech_news_spoken(7,20+record);
    }
    assert(tech_history->count==128);
    snapshot_bytes=tech_news_history_export(snapshot,sizeof(snapshot),lookup_now,&saved_revision);
    assert(snapshot_bytes==HISU_TECH_HISTORY_BYTES && snapshot[2]==128);
    assert(tech_news_history_import(snapshot,snapshot_bytes));
    assert(tech_was_seen(feed,"https://www.qbitai.com/article/129","AI event 129",lookup_now));
    assert(!tech_was_seen(feed,"https://www.qbitai.com/article/0","AI event 0",lookup_now));
    cJSON *text=cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(root,"candidates"),0),"content"),"parts"),0),"text");
    const char *bad[]={"{\"items\":[{\"index\":11,\"summary\":\"bad\"}]}","{\"items\":[{\"index\":0,\"summary\":\"a\"},{\"index\":0,\"summary\":\"b\"}]}",
        "{\"items\":[{\"index\":0,\"summary\":\"<script>bad</script>\"}]}","{\"items\":[{\"index\":0,\"summary\":\"bad\",\"url\":\"https://evil\"}]}"};
    for (unsigned index=0;index<sizeof(bad)/sizeof(bad[0]);++index) {
        assert(cJSON_SetValuestring(text,bad[index]));
        assert_error(tech_news_result(root,feed),"web_unverified");
    }
    assert(cJSON_SetValuestring(text,"{\"items\":[]}"));
    assert_error(tech_news_result(root,feed),"news_batch_empty");
    cJSON *item=cJSON_GetArrayItem(qbit,0);
    cJSON_ReplaceItemInObjectCaseSensitive(item,"date_gmt",cJSON_CreateString("2026-10-13T08:00:00"));
    cJSON *window=tech_news_feed(lookup_now,true);
    assert(tech_news_append(window,qbit,true,lookup_now) && cJSON_GetArraySize(field(window,"items"))==1);
    cJSON_Delete(window);
    cJSON_ReplaceItemInObjectCaseSensitive(item,"date_gmt",cJSON_CreateString("2026-10-13T07:59:59"));
    window=tech_news_feed(lookup_now,true);
    assert(tech_news_append(window,qbit,true,lookup_now) && !cJSON_GetArraySize(field(window,"items")));
    cJSON_Delete(window);
    cJSON_ReplaceItemInObjectCaseSensitive(item,"date_gmt",cJSON_CreateString("2026-10-20T07:00:00"));
    cJSON_ReplaceItemInObjectCaseSensitive(item,"link",cJSON_CreateString("https://www.qbitai.com.evil/article"));
    window=tech_news_feed(lookup_now,true);
    assert(tech_news_append(window,qbit,true,lookup_now) && !cJSON_GetArraySize(field(window,"items")));
    cJSON_Delete(window);
    cJSON_Delete(result); cJSON_Delete(root); cJSON_Delete(feed); cJSON_Delete(qbit); cJSON_Delete(ithome);
    free(tech_history); tech_history=NULL;
    cJSON *args=cJSON_Parse("{\"topic\":\"news\",\"repeat\":true}");
    assert(lookup_query_parse(args,&query) && query.repeat); cJSON_Delete(args);
    args=cJSON_Parse("{\"topic\":\"news\",\"repeat\":\"true\"}");
    assert(!lookup_query_parse(args,&query)); cJSON_Delete(args);
    puts("Tech news tests passed: publisher JSON/HTML, 7-day boundary, dates, URL allowlist, spoken/cancel/repeat, NVS reboot/checksum/retry/rollover and grounded references.");
}

static void news_continuation_tests(void)
{
    reset_lookup(); free(tech_history); tech_history=NULL;
    lookup_news_pool=tech_news_feed(lookup_now,true); assert(lookup_news_pool);
    cJSON *data=cJSON_Parse(tech_qbit_wire); assert(data);
    assert(tech_news_append(lookup_news_pool,data,true,lookup_now)); cJSON_Delete(data);
    data=cJSON_Parse(tech_ithome_wire); assert(data);
    assert(tech_news_append(lookup_news_pool,data,false,lookup_now)); cJSON_Delete(data);
    assert(tech_news_append_genk(lookup_news_pool,tech_genk_wire,lookup_now));
    lookup_news_expires=mock_now+600000000;
    char previous[512]="";
    for (unsigned turn=0;turn<3;++turn) {
        unsigned requests=lookup_http_started;
        cJSON *result=run_lookup("{\"topic\":\"news\"}");
        assert(cJSON_IsTrue(field(result,"ok")) && lookup_http_started==requests+1);
        const char *url=field(cJSON_GetArrayItem(field(result,"sources"),0),"url")->valuestring;
        assert(strcmp(previous,url)); strcpy(previous,url);
        assert(field(result,"remaining_candidates")->valueint==(int)(2-turn));
        voice_lookup_spoken(7,9); cJSON_Delete(result); mock_now+=11000000;
    }
    tech_qbit_timeout=true; lookup_news_next=2; lookup_news_pages[3]=8;
    int64_t started=mock_now;
    assert_error(run_lookup("{\"topic\":\"news\"}"),"news_scan_incomplete");
    assert(tech_qbit_attempts==1 && lookup_news_retry[2]>mock_now && mock_now-started<4000000);
    mock_now+=11000000;
    assert_error(run_lookup("{\"topic\":\"news\"}"),"news_scan_incomplete");
    assert(tech_qbit_attempts==1);
    reset_lookup(); free(tech_history); tech_history=NULL;
    unsigned requests=lookup_http_started;
    cJSON *result=run_lookup("{\"topic\":\"news\"}");
    assert(cJSON_IsTrue(field(result,"ok")) && lookup_http_started==requests+2 && lookup_news_pages[0]==1 && !lookup_news_pages[1]);
    cJSON_Delete(result);
    reset_lookup(); free(tech_history); tech_history=NULL;
    tech_empty_summaries=1; requests=lookup_http_started;
    result=run_lookup("{\"topic\":\"news\"}");
    assert(cJSON_IsTrue(field(result,"ok")) && lookup_http_started==requests+4);
    assert(strstr(field(cJSON_GetArrayItem(field(result,"sources"),0),"url")->valuestring,"genk.vn"));
    assert(tech_history && !tech_history->count);
    cJSON_Delete(result);
    reset_lookup(); free(tech_history); tech_history=NULL;
    tech_empty_summaries=2; requests=lookup_http_started;
    assert_error(run_lookup("{\"topic\":\"news\"}"),"news_scan_incomplete");
    assert(lookup_http_started==requests+4 && !tech_history->count);
    reset_lookup(); free(tech_history); tech_history=NULL;
    result=run_lookup("{\"topic\":\"news\",\"source\":\"techcrunch\"}");
    assert(cJSON_IsTrue(field(result,"ok")));
    assert(strstr(field(cJSON_GetArrayItem(field(result,"sources"),0),"url")->valuestring,"techcrunch.com/"));
    cJSON_Delete(result);
    mock_now+=11000000; requests=lookup_http_started;
    result=run_lookup("{\"topic\":\"news\",\"source\":\"genk\"}");
    assert(cJSON_IsTrue(field(result,"ok")) && lookup_http_started==requests+2);
    assert(strstr(field(cJSON_GetArrayItem(field(result,"sources"),0),"url")->valuestring,"genk.vn/"));
    cJSON_Delete(result);
    reset_lookup(); free(tech_history); tech_history=NULL;
    puts("News continuation tests passed: one source per normal lookup, cached unseen batches with no source HTTP, different sources after heard completion, bounded timeout and source cooldown.");
}

static void news_tests(void)
{
    const hisu_internet_state_t states[]={HISU_INTERNET_UNKNOWN,HISU_INTERNET_CHECKING,HISU_INTERNET_CAPTIVE,HISU_INTERNET_OFFLINE};
    for (unsigned index=0; index<sizeof(states)/sizeof(states[0]); ++index) {
        reset_lookup(); lookup_internet=states[index];
        unsigned requests=lookup_http_started;
        cJSON *result=run_lookup("{\"topic\":\"news\"}");
        if (index<2) {
            assert(cJSON_IsTrue(field(result,"ok")) && lookup_http_started==requests+2);
            cJSON_Delete(result);
        } else {
            assert_error(result,"news_scan_incomplete");
            assert(lookup_http_started==requests);
        }
    }
    reset_lookup();
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
    reset_lookup();
    unsigned before=lookup_http_started,views=research_published;
    o=run_lookup("{\"topic\":\"news\"}");
    assert(cJSON_IsTrue(field(o,"ok")) && field(o,"speech") && lookup_http_started==before+2);
    assert(!field(o,"search_suggestions") && !field(o,"parts") && research_published==views+1);
    cJSON_Delete(o); before=lookup_http_started; mock_now+=11000000;
    o=run_lookup("{\"topic\":\"news\"}");
    assert(!field(o,"cached") && lookup_http_started==before+1); cJSON_Delete(o);
    reset_lookup();
    cJSON *snapshot_args=cJSON_Parse("{\"topic\":\"news\"}");
    assert(voice_lookup_submit(7,9,"test",snapshot_args,valid_turn));
    cJSON_Delete(snapshot_args);
    assert(!strcmp(lookup_job->web_key,"fake-key"));
    web_key=false;
    lookup_task(lookup_task_arg);
    assert(!lookup_job->web_key[0]);
    o=decode_result(voice_lookup_take());
    assert(cJSON_IsTrue(field(o,"ok"))); cJSON_Delete(o);
    reset_lookup(); web_key=false; before=lookup_http_started;
    assert_error(run_lookup("{\"topic\":\"news\"}"),"web_key_required"); assert(lookup_http_started==before);
    reset_lookup(); mock_http_status=429;
    assert_error(run_lookup("{\"topic\":\"news\"}"),"web_quota");
    reset_lookup(); mock_mode=3;
    assert_error(run_lookup("{\"topic\":\"news\"}"),"news_scan_incomplete");
    reset_lookup(); mock_mode=5;
    assert_error(run_lookup("{\"topic\":\"news\"}"),"news_scan_incomplete");
    reset_lookup(); mock_mode=7;
    assert_error(run_lookup("{\"topic\":\"news\"}"),"news_scan_incomplete");
    reset_lookup(); mock_mode=8; views=research_published;
    assert_error(run_lookup("{\"topic\":\"news\"}"),"news_scan_incomplete"); assert(research_published==views);
    reset_lookup();
    o=run_lookup("{\"topic\":\"slang\",\"query\":\"test slang\"}");
    assert(cJSON_IsTrue(field(o,"ok"))); cJSON_Delete(o);
    const char *invalid_web[]={"{\"topic\":\"slang\"}","{\"topic\":\"news\",\"mode\":\"detail\"}",
        "{\"topic\":\"news\",\"mode\":\"unknown\"}","{\"topic\":\"slang\",\"query\":\"a\\nb\"}",
        "{\"topic\":\"news\",\"query\":\"a\",\"query\":\"b\"}"};
    lookup_query_t web_query;
    for (unsigned i=0;i<sizeof(invalid_web)/sizeof(invalid_web[0]);++i) {
        cJSON *input=cJSON_Parse(invalid_web[i]); assert(!lookup_query_parse(input,&web_query)); cJSON_Delete(input);
    }
    cJSON *input=cJSON_Parse("{\"topic\":\"news\",\"mode\":\"detail\",\"query\":\"AI launch\"}");
    assert(lookup_query_parse(input,&web_query)); cJSON_Delete(input);
    char *request=lookup_web_request(&web_query,lookup_now); assert(request);
    input=cJSON_Parse(request); assert(input && strstr(field(input,"input")->valuestring,"AI launch"));
    cJSON_Delete(input); free(request);
    cJSON *raw=cJSON_Parse(web_wire); assert(raw);
    cJSON *steps=cJSON_GetObjectItem(raw,"steps");
    cJSON_DeleteItemFromArray(steps,0);
    assert_error(lookup_web_result(raw,lookup_now),"web_unverified"); cJSON_Delete(raw);
    raw=cJSON_Parse("{\"status\":\"completed\",\"steps\":[{\"type\":\"model_output\",\"content\":[{\"type\":\"text\",\"text\":\"unsupported answer\"}]}]}");
    assert_error(lookup_web_result(raw,lookup_now),"web_unverified"); cJSON_Delete(raw);
    reset_lookup();
}

static void news_article_tests(void)
{
    reset_lookup(); free(tech_history); tech_history=NULL;
    /* Seed a genuine selected story with a publisher id, then mark it heard. */
    cJSON *feed=tech_news_feed(lookup_now,true),*data=cJSON_Parse(tech_ithome_wire);
    assert(feed && data && tech_news_append(feed,data,0,lookup_now)); cJSON_Delete(data);
    cJSON *item=cJSON_GetArrayItem(field(feed,"items"),0);
    assert(cJSON_AddNumberToObject(item,"publisher_id",1123456));
    char id[9]; strcpy(id,field(item,"article_id")->valuestring);
    cJSON *root=cJSON_Parse(tech_good_wire),*result=tech_news_result(root,feed);
    lookup_news_remember(result); tech_news_stage(result,7,9); voice_lookup_spoken(7,9);
    cJSON_Delete(result); cJSON_Delete(root); cJSON_Delete(feed);
    assert(tech_history->count==1 && !lookup_news_pool);
    char args[160]; snprintf(args,sizeof(args),"{\"topic\":\"news\",\"mode\":\"detail\",\"article_id\":\"%s\"}",id);
    unsigned requests=lookup_http_started;
    result=run_lookup(args);
    assert(cJSON_IsTrue(field(result,"ok")) && cJSON_IsTrue(field(result,"article_loaded")) && cJSON_IsFalse(field(result,"snippets_only")));
    assert(article_gets==1 && lookup_http_started==requests+2);
    assert(!field(cJSON_GetArrayItem(field(result,"sources"),0),"article_text"));
    cJSON_Delete(result); mock_now+=11000000;
    requests=lookup_http_started;
    snprintf(args,sizeof(args),"{\"topic\":\"news\",\"mode\":\"brief\",\"article_id\":\"%s\"}",id);
    result=run_lookup(args); assert(cJSON_IsTrue(field(result,"ok")) && article_gets==1 && lookup_http_started==requests+1); cJSON_Delete(result);
    mock_now+=11000000; requests=lookup_http_started;
    assert_error(run_lookup("{\"topic\":\"news\",\"mode\":\"detail\",\"article_id\":\"deadbeef\"}"),"article_not_found");
    assert(lookup_http_started==requests);
    /* A different story ID from the publisher is never summarized. */
    cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetArrayItem(lookup_news_recent,0),"article_text");
    mock_now+=11000000; article_wrong_id=true;
    assert_error(run_lookup(args),"article_unverified");
    mock_now+=11000000; article_wrong_id=false; mock_http_status=302;
    assert_error(run_lookup(args),"article_unavailable");
    mock_now+=11000000; mock_http_status=200; mock_mode=8;
    unsigned views=research_published;
    assert_error(run_lookup(args),"article_unavailable"); assert(research_published==views);
    reset_lookup();
    cJSON *article=cJSON_Parse("{\"source\":\"GenK\",\"url\":\"https://genk.vn/ai-chip-moi.chn\"}"); bool truncated=false; char url[640];
    assert(tech_news_article_url(article,url,sizeof(url)) && !strcmp(url,"https://genk.vn/ai-chip-moi.chn"));
    char *text=tech_news_article_text(article,article_genk_wire,strlen(article_genk_wire),&truncated);
    assert(text && strstr(text,"Chip AI mới") && !strstr(text,"steal_key") && !strstr(text,"UNRELATED FOOTER") && !truncated); free(text);
    cJSON_ReplaceItemInObjectCaseSensitive(article,"url",cJSON_CreateString("https://genk.vn.evil/ai.chn"));
    assert(!tech_news_article_url(article,url,sizeof(url)));
    assert(!tech_news_article_text(article,"<html>blocked</html>",20,&truncated));
    cJSON_Delete(article);
    lookup_query_t query;
    data=cJSON_Parse("{\"topic\":\"news\",\"article_id\":\"https://evil\"}"); assert(!lookup_query_parse(data,&query)); cJSON_Delete(data);
    data=cJSON_Parse("{\"topic\":\"news\",\"source\":\"tinhte\"}"); assert(!lookup_query_parse(data,&query)); cJSON_Delete(data);
    free(tech_history); tech_history=NULL;
    puts("Article follow-up tests passed: heard story retained, exact reference, original-body fetch/cache, identity checks, no redirects, cancel, HTML entities and source removal.");
}
static bool slang_idle(void) { return mock_idle && lookup_valid; }
static void slang_tests(void)
{
    reset_lookup(); mock_idle=true; slang_enabled=false; slang_power=50; slang_writes=0;
    s_next_attempt=0; s_loaded=false; free(s_dictionary); s_dictionary=NULL;
    unsigned before=lookup_http_started;
    assert(!voice_slang_poll(slang_idle) && lookup_http_started==before);
    slang_enabled=true; slang_power=29;
    assert(!voice_slang_poll(slang_idle) && !slang_writes);
    slang_power=50; lookup_online=false;
    assert(!voice_slang_poll(slang_idle)); lookup_online=true; mock_idle=false;
    assert(!voice_slang_poll(slang_idle)); mock_idle=true;
    assert(voice_slang_poll(slang_idle) && slang_writes==1 && lookup_http_started==before+3);
    char *context=voice_slang_context(); assert(context && strstr(context,"friend & peer") && strstr(context,"CC-BY-SA"));
    cJSON *root=cJSON_Parse(context); assert(voice_slang_snapshot_valid(root,lookup_now)); cJSON_Delete(root); free(context);
    before=lookup_http_started;
    assert(!voice_slang_poll(slang_idle) && lookup_http_started==before);
    /* Reboot still respects the persisted successful refresh time. */
    free(s_dictionary); s_dictionary=NULL; s_loaded=false; s_next_attempt=0;
    assert(!voice_slang_poll(slang_idle) && lookup_http_started==before);
    lookup_now+=86401; mock_now+=86401000000LL; slang_cancel=true;
    assert(voice_slang_poll(slang_idle) && slang_writes==1);
    slang_cancel=false; lookup_valid=true; s_next_attempt=0; mock_mode=3;
    assert(voice_slang_poll(slang_idle) && slang_writes==1); mock_mode=0;
    s_next_attempt=0;
    assert(voice_slang_poll(slang_idle) && slang_writes==2);
    char meaning[321]; root=cJSON_Parse("{\"vi\":[{\"definitions\":[{\"definition\":\"<b>friend</b> &amp; peer\"}]}]}");
    assert(voice_slang_definition(root,meaning) && !strcmp(meaning,"friend & peer")); cJSON_Delete(root);
    root=cJSON_Parse("{\"en\":[{\"definitions\":[{\"definition\":\"wrong language\"}]}]}");
    assert(!voice_slang_definition(root,meaning)); cJSON_Delete(root);
    root=cJSON_Parse("{\"vi\":[{\"definitions\":[{\"definition\":\"broken <tag\"}]}]}");
    assert(!voice_slang_definition(root,meaning)); cJSON_Delete(root);
    free(s_dictionary); s_dictionary=NULL; free(slang_stored); slang_stored=NULL; s_loaded=false;
    lookup_now-=86401;
}
int main(int argc,char **argv) {
    if (argc==5 && !strcmp(argv[1],"--article-input")) {
        char *body=malloc(262145),metadata[2048]; assert(body);
        FILE *input=fopen(argv[2],"rb"); assert(input); size_t size=fread(metadata,1,sizeof(metadata)-1,input); metadata[size]=0; fclose(input);
        cJSON *item=cJSON_Parse(metadata); assert(item);
        input=fopen(argv[3],"rb"); assert(input); size=fread(body,1,262145,input); fclose(input); assert(size<=262144); body[size]=0;
        bool truncated=false; char *text=tech_news_article_text(item,body,size,&truncated); assert(text);
        printf("Actual article parsed: source=%s body_bytes=%u text_bytes=%u truncated=%d\n",field(item,"source")->valuestring,(unsigned)size,(unsigned)strlen(text),truncated);
        FILE *output=fopen(argv[4],"wb"); assert(output); fwrite(text,1,strlen(text),output); fclose(output);
        free(text); free(body); cJSON_Delete(item); return 0;
    }
    if (argc==6 && !strcmp(argv[1],"--publisher-input")) {
        char *body=malloc(131073); assert(body); FILE *input=fopen(argv[2],"rb"); assert(input);
        size_t size=fread(body,1,131073,input); fclose(input); assert(size<=131072); body[size]=0;
        int64_t now=_strtoi64(argv[4],NULL,10); cJSON *data=cJSON_Parse(body),*feed=tech_news_feed(now,false);
        assert(data && feed && tech_news_append(feed,data,(unsigned)atoi(argv[3]),now));
        assert(cJSON_GetArraySize(field(feed,"items"))>0);
        char *json=cJSON_PrintUnformatted(feed); assert(json); FILE *output=fopen(argv[5],"wb"); assert(output); fwrite(json,1,strlen(json),output); fclose(output);
        printf("Actual publisher parsed: bytes=%u items=%d\n",(unsigned)size,cJSON_GetArraySize(field(feed,"items")));
        free(json); cJSON_Delete(data); cJSON_Delete(feed); free(body); return 0;
    }
    if (argc==7 && !strcmp(argv[1],"--tech-input")) {
        int64_t now=_strtoi64(argv[5],NULL,10);
        cJSON *feed=tech_news_feed(now,false); assert(feed);
        char *body=malloc(131073); assert(body);
        for (unsigned source=0;source<3;++source) {
            FILE *input=fopen(argv[2+source],"rb"); assert(input);
            size_t bytes=fread(body,1,131073,input); fclose(input); assert(bytes<=131072); body[bytes]=0;
            if (source==2) assert(tech_news_append_genk(feed,body,now));
            else {
                cJSON *data=cJSON_Parse(body); assert(data && tech_news_append(feed,data,source==0,now)); cJSON_Delete(data);
            }
        }
        lookup_query_t query={.news=true}; strcpy(query.mode,"read");
        char *request=tech_news_request(&query,feed); assert(request);
        FILE *output=fopen(argv[6],"wb"); assert(output); fwrite(request,1,strlen(request),output); fclose(output);
        snprintf(body,131073,"%s.feed.json",argv[6]); output=fopen(body,"wb"); assert(output);
        char *json=cJSON_PrintUnformatted(feed); assert(json); fwrite(json,1,strlen(json),output); fclose(output);
        printf("Actual tech sources parsed: %d items; summary request generated without Search.\n",cJSON_GetArraySize(field(feed,"items")));
        free(json); free(request); free(body); cJSON_Delete(feed); free(tech_history); tech_history=NULL; return 0;
    }
    if (argc==4 && !strcmp(argv[1],"--tech-output")) {
        char *body=malloc(131073); assert(body); cJSON *data[2];
        for (unsigned input=0;input<2;++input) {
            FILE *file=fopen(argv[2+input],"rb"); assert(file);
            size_t bytes=fread(body,1,131073,file); fclose(file); assert(bytes<=131072); body[bytes]=0;
            data[input]=cJSON_Parse(body); assert(data[input]);
        }
        cJSON *result=tech_news_result(data[0],data[1]); assert(cJSON_IsTrue(field(result,"ok")));
        printf("Actual Gemini digest validated: sources=%d speech_bytes=%u scope=%s\n",cJSON_GetArraySize(field(result,"sources")),
            (unsigned)strlen(field(result,"speech")->valuestring),field(result,"scope")->valuestring);
        cJSON_Delete(result); cJSON_Delete(data[0]); cJSON_Delete(data[1]); free(body); return 0;
    }
    if (argc==4 && (!strcmp(argv[1],"--news") || !strcmp(argv[1],"--gold") || !strcmp(argv[1],"--weather"))) {
        FILE *f=fopen(argv[2],"rb"); assert(f);
        char *body=malloc(HISU_NEWS_BODY_LIMIT+1); assert(body);
        size_t bytes=fread(body,1,HISU_NEWS_BODY_LIMIT+1,f); fclose(f); assert(bytes<=HISU_NEWS_BODY_LIMIT);
        body[bytes]=0;
        int64_t now=_strtoi64(argv[3],NULL,10);
        cJSON *result;
        if (!strcmp(argv[1],"--news")) result=lookup_news(body,bytes,now);
        else {
            cJSON *root=cJSON_Parse(body); assert(root);
            lookup_query_t q={.weather=!strcmp(argv[1],"--weather")}; strcpy(q.type,"summary");
            cJSON *place=cJSON_CreateObject(); assert(place && cJSON_AddStringToObject(place,"name","Probe location"));
            result=q.weather ? lookup_weather(root,place,&q,now) : lookup_gold(root,&q,now);
            cJSON_Delete(root); cJSON_Delete(place);
        }
        assert(cJSON_IsTrue(field(result,"ok")));
        printf("Live provider parsed successfully: %s\n",argv[1]);
        cJSON_Delete(result); free(body); return 0;
    }
    tech_news_tests();
    lookup_psram_tests();
    lookup_cache_failure_test();
    lookup_failure_cooldown_test();
    news_continuation_tests();
    news_article_tests();
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
    reset_lookup(); calls=lookup_http_started;
    o=run_lookup("{\"topic\":\"gold\",\"gold_history\":true}");
    assert(lookup_http_started==calls+2 && cJSON_GetArraySize(field(o,"history"))==1);
    assert(!strcmp(field(o,"updated_date_vn")->valuestring,"2026-10-20"));
    price=cJSON_GetArrayItem(field(o,"history"),0);
    assert(field(price,"sell")->valuedouble==142000000);
    cJSON_Delete(o);
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
    slang_tests();
    free(tech_history); tech_history=NULL;
    assert(!lookup_live_bytes && !lookup_allocation_count);
    puts("Lookup tests passed: bounded HTTP, TLS host allowlist, cache, gold units, hourly weather, clarification, cancellation, timeout, memory guard.");
    return 0;
}
