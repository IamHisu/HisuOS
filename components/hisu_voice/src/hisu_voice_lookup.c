#include "hisu_voice_lookup.h"
#include "hisu_voice_lookup_data.h"
#include "hisu_voice_news.h"
#include "hisu_voice_tech_news.h"
#include "hisu_voice_web.h"
#include "hisu_settings.h"
#include "hisu_media.h"
#include "hisu_voice_lookup_json.h"
#include "hisu_memory_json_guard.h"
#include "hisu_network.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#define LOOKUP_BODY_LIMIT 16384U
#define LOOKUP_JSON_LIMIT 32768U
#define LOOKUP_STACK_BYTES 8192U
#define LOOKUP_STACK_LOW_BYTES 1536U
typedef struct {
    voice_lookup_result_t result;
    lookup_query_t query;
    bool (*current)(uint32_t,uint32_t);
    int64_t deadline;
    char *body;
    size_t used;
    bool overflow;
    const char *post_body;
    int http_status;
    bool article_get;
    size_t body_capacity;
    unsigned news_fetches;
    TaskHandle_t task;
    char url[640];
    char web_key[128];
} lookup_job_t;
static portMUX_TYPE lookup_mux=portMUX_INITIALIZER_UNLOCKED;
static lookup_job_t *lookup_job;
static bool lookup_done;
static bool lookup_reaping;
/* Worker-only RAM cache. Never persist locations or provider payloads. */
static struct { lookup_query_t query; char *json; int64_t expires,local_day; } lookup_cache[2];
static int64_t lookup_next_request;
static bool lookup_history_loaded;
static uint32_t lookup_history_saved;
static cJSON *lookup_news_pool;
/* RAM-only references to recently introduced stories, independent of heard-news
 * deduplication. Bodies are fetched only on explicit article follow-up. */
static cJSON *lookup_news_recent;
static int64_t lookup_news_expires,lookup_news_retry[4];
static unsigned lookup_news_pages[4],lookup_news_next;

void voice_lookup_history_load(void)
{
    if (lookup_history_loaded) return;
    uint32_t *words=heap_caps_malloc(HISU_TECH_HISTORY_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!words) return;
    size_t bytes=HISU_TECH_HISTORY_BYTES;
    esp_err_t err=hisu_settings_load_news_history(words,&bytes);
    bool resolved=err==ESP_OK || err==ESP_ERR_NOT_FOUND;
    if (err==ESP_OK && !tech_news_history_import(words,bytes)) err=ESP_FAIL;
    free(words);
    lookup_history_loaded=resolved;
    if (resolved) lookup_history_saved=tech_news_history_revision();
    ESP_LOGI("LOOKUP","News history load: err=%d",(int)err);
}

void voice_lookup_history_flush(void)
{
    if (!lookup_history_loaded || voice_lookup_busy() || tech_news_history_revision()==lookup_history_saved) return;
    uint32_t *words=heap_caps_malloc(HISU_TECH_HISTORY_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!words) return;
    uint32_t revision=0;
    size_t bytes=tech_news_history_export(words,HISU_TECH_HISTORY_BYTES,time(NULL),&revision);
    esp_err_t err=bytes ? hisu_settings_save_news_history(words,bytes) : ESP_FAIL;
    if (err==ESP_OK) lookup_history_saved=revision;
    free(words);
    ESP_LOGI("LOOKUP","News history save: err=%d bytes=%u",(int)err,(unsigned)bytes);
}

/* Keep long-lived strings external without changing cJSON's global allocator. */
static char *lookup_print(const cJSON *data) {
    if (!data) return NULL;
    for (unsigned capacity=1024;capacity<=LOOKUP_JSON_LIMIT;capacity*=2) {
        char *json=heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!json) return NULL;
        if (cJSON_PrintPreallocated((cJSON *)data,json,(int)capacity,false)) return json;
        free(json);
    }
    return NULL;
}
char *voice_lookup_encode_result(const voice_lookup_result_t *job) {
    if (!job || !memchr(job->id,0,sizeof(job->id))) return NULL;
    /* Only worker-serialized JSON crosses this boundary, not provider bodies. */
    const char *response=job->response ? job->response : "{\"ok\":false,\"error\":\"insufficient_memory\"}";
    size_t length=strnlen(response,LOOKUP_JSON_LIMIT);
    if (length==LOOKUP_JSON_LIMIT) return NULL;
    cJSON *root=cJSON_CreateObject(),*tool=root ? cJSON_AddObjectToObject(root,"toolResponse") : NULL;
    cJSON *list=tool ? cJSON_AddArrayToObject(tool,"functionResponses") : NULL;
    cJSON *row=list ? cJSON_CreateObject() : NULL;
    /* Keep diagnostic codes in monitor/cache, not in the model's spoken answer.
     * Successful (potentially large) weather payloads remain zero-tree raw JSON. */
    bool failure=!strncmp(response,"{\"ok\":false",11);
    cJSON *data=row ? (failure ? cJSON_Parse(response) : cJSON_CreateRaw(response)) : NULL;
    if (failure && data) {
        cJSON_DeleteItemFromObjectCaseSensitive(data,"error");
        if (!cJSON_GetObjectItemCaseSensitive(data,"message") &&
            !cJSON_AddStringToObject(data,"message","Hi Su chưa lấy được thông tin lúc này.")) {
            cJSON_Delete(data); data=NULL;
        }
    }
    bool built=row && data && cJSON_AddItemToArray(list,row);
    if (!built) cJSON_Delete(row);
    else {
        built=cJSON_AddStringToObject(row,"id",job->id) &&
            cJSON_AddStringToObject(row,"name","lookup_info") && cJSON_AddItemToObject(row,"response",data);
        if (built) data=NULL;
    }
    cJSON_Delete(data);
    size_t capacity=length+6*strlen(job->id)+192;
    char *json=built ? heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) : NULL;
    if (json && !cJSON_PrintPreallocated(root,json,(int)capacity,false)) { free(json); json=NULL; }
    cJSON_Delete(root);
    ESP_LOGI("LOOKUP","Envelope ready: json_bytes=%u memory=PSRAM internal_free=%u internal_largest=%u stack_free=%u",
        json ? (unsigned)strlen(json) : 0,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return json;
}
static void lookup_log_memory(const lookup_job_t *j,const char *phase) {
    ESP_LOGI("LOOKUP","RAM phase=%s topic=%s body_bytes=%u internal_free=%u internal_largest=%u PSRAM_free=%u stack_free=%u",
        phase,j->query.slang ? "slang" : j->query.news ? "news" : j->query.weather ? "weather" : "gold",(unsigned)j->used,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

bool voice_lookup_busy(void) {
    portENTER_CRITICAL(&lookup_mux); bool busy=lookup_job!=NULL; portEXIT_CRITICAL(&lookup_mux); return busy;
}
static bool lookup_link_allowed(const hisu_network_link_t *link) {
    return link->connected && !link->setup_enabled &&
        link->internet!=HISU_INTERNET_CAPTIVE && link->internet!=HISU_INTERNET_OFFLINE;
}
static bool lookup_current(lookup_job_t *j) {
    hisu_network_link_t link; hisu_network_get_link(&link);
    return j->current(j->result.epoch,j->result.generation) && lookup_link_allowed(&link) &&
        esp_timer_get_time()<j->deadline;
}
static esp_err_t lookup_receive(esp_http_client_event_t *event) {
    lookup_job_t *j=event->user_data;
    if (!lookup_current(j)) return ESP_FAIL;
    if (event->event_id==HTTP_EVENT_ON_DATA && event->data_len>0) {
        size_t limit=j->article_get ? 262144 : j->query.news && !j->post_body ? 131072 : (j->query.news || j->query.slang) ? HISU_WEB_BODY_LIMIT : LOOKUP_BODY_LIMIT;
        if (j->body_capacity && limit>j->body_capacity) limit=j->body_capacity;
        if ((size_t)event->data_len>limit-j->used || memchr(event->data,0,(size_t)event->data_len)) {
            j->overflow=true; return ESP_FAIL;
        }
        memcpy(j->body+j->used,event->data,(size_t)event->data_len);
        j->used+=(size_t)event->data_len; j->body[j->used]=0;
    }
    return ESP_OK;
}
static bool lookup_download(lookup_job_t *j,bool rss) {
    if (!lookup_current(j)) {
        hisu_network_link_t link; hisu_network_get_link(&link);
        ESP_LOGI("LOOKUP","HTTP preflight blocked: connected=%d setup=%d internet=%d current=%d expired=%d",
            link.connected,link.setup_enabled,(int)link.internet,j->current(j->result.epoch,j->result.generation),
            esp_timer_get_time()>=j->deadline);
        return false;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<6144 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<2048) {
        lookup_log_memory(j,"http_memory_blocked"); return false;
    }
    j->used=0; j->overflow=false; j->body[0]=0;
    /* Larger than ALWAYSINTERNAL: HTTP buffers can live in PSRAM, leaving socket/DMA headroom. */
    esp_http_client_config_t config={.url=j->url,.timeout_ms=1000,.is_async=true,.disable_auto_redirect=true,
        .method=j->post_body ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .crt_bundle_attach=esp_crt_bundle_attach,.event_handler=lookup_receive,.user_data=j,.buffer_size=4096,.buffer_size_tx=4096};
    esp_http_client_handle_t http=esp_http_client_init(&config);
    if (!http) { lookup_log_memory(j,"http_init_failed"); return false; }
    esp_err_t err=esp_http_client_set_header(http,"Accept-Encoding","identity");
    if (err==ESP_OK && j->post_body) {
        err=esp_http_client_set_header(http,"x-goog-api-key",j->web_key);
        if (err==ESP_OK) err=esp_http_client_set_header(http,"Content-Type","application/json");
        if (err==ESP_OK) err=esp_http_client_set_post_field(http,j->post_body,(int)strlen(j->post_body));
    }
    int64_t started=esp_timer_get_time();
    int64_t source_wait=j->article_get ? 8000000 : j->query.news ? (strstr(j->url,"qbitai.com") ? 2500000 : 5000000) : 7000000;
    int64_t request_deadline=rss ? started+source_wait : j->deadline;
    bool timed_out=false;
    if (err==ESP_OK) err=esp_http_client_set_header(http,"Accept",j->query.news && !j->post_body ? "application/json,text/html" : "application/json");
    if (err==ESP_OK) do {
        if (!lookup_current(j) || esp_timer_get_time()>=request_deadline) {
            timed_out=esp_timer_get_time()>=request_deadline || esp_timer_get_time()>=j->deadline;
            err=ESP_FAIL; break;
        }
        errno=0;
        err=esp_http_client_perform(http);
        if (err==ESP_ERR_HTTP_EAGAIN) vTaskDelay(pdMS_TO_TICKS(20));
    } while (err==ESP_ERR_HTTP_EAGAIN);
    int status=esp_http_client_get_status_code(http); j->http_status=status;
    int socket_error=errno;
    bool ok=err==ESP_OK && status==200 && !j->overflow && esp_http_client_is_complete_data_received(http);
    esp_http_client_cleanup(http);
    ESP_LOGI("LOOKUP","HTTP=%d err=%d bytes=%u elapsed_ms=%u timeout=%d socket_errno=%d overflow=%d stack=%u",
        status,(int)err,(unsigned)j->used,(unsigned)((esp_timer_get_time()-started)/1000),timed_out,
        socket_error,j->overflow,(unsigned)uxTaskGetStackHighWaterMark(NULL));
    return ok && lookup_current(j);
}
static cJSON *lookup_fetch(lookup_job_t *j) {
    if (!lookup_download(j,false) || strstr(j->body,"\\u0000") || !hisu_memory_json_shallow(j->body,10)) return NULL;
    lookup_log_memory(j,"before_json_parse");
    cJSON *data=cJSON_ParseWithOpts(j->body,NULL,true);
    lookup_log_memory(j,"after_json_parse");
    return data;
}
static void lookup_encode(char *out,const char *in) {
    static const char hex[]="0123456789ABCDEF";
    for (const unsigned char *p=(const unsigned char *)in;*p;++p) {
        if ((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='-' || *p=='_') *out++=(char)*p;
        else { *out++='%'; *out++=hex[*p>>4]; *out++=hex[*p&15]; }
    }
    *out=0;
}
static cJSON *lookup_web(lookup_job_t *j)
{
    if (!j->web_key[0]) return lookup_error("web_key_required");
    char *request=lookup_web_request(&j->query,time(NULL));
    if (!request) return lookup_error("insufficient_memory");
    strcpy(j->url,"https://generativelanguage.googleapis.com/v1beta/interactions");
    j->post_body=request;
    bool downloaded=lookup_download(j,false);
    j->post_body=NULL; free(request);
    if (!downloaded) return lookup_error(j->http_status==429 || j->http_status==403 || j->http_status==401 ? "web_quota" : "source_unavailable");
    if (strstr(j->body,"\\u0000") || !hisu_memory_json_shallow(j->body,16)) return lookup_error("web_unverified");
    cJSON *root=cJSON_ParseWithOpts(j->body,NULL,true);
    cJSON *result=lookup_web_result(root,time(NULL)); cJSON_Delete(root);
    if (lookup_current(j) && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok"))) {
        char *view=lookup_print(result);
        if (!view || !hisu_media_research_publish(view)) {
            free(view); cJSON_Delete(result); return lookup_error("insufficient_memory");
        }
        free(view);
        /* Search suggestions/annotations belong in the view, not in the spoken tool prompt. */
        cJSON_DeleteItemFromObjectCaseSensitive(result,"parts");
        cJSON_DeleteItemFromObjectCaseSensitive(result,"search_suggestions");
    }
    return result;
}
static int lookup_news_preferred(const lookup_query_t *query)
{
    if (!strcmp(query->source,"ithome")) return 0;
    if (!strcmp(query->source,"genk")) return 1;
    if (!strcmp(query->source,"qbitai")) return 2;
    if (!strcmp(query->source,"techcrunch")) return 3;
    return -1;
}
static cJSON *lookup_news_candidates(const cJSON *excluded,const lookup_query_t *query)
{
    static const char *sources[]={"IT之家 / IT Home","GenK","QbitAI / 量子位","TechCrunch"};
    int preferred=lookup_news_preferred(query);
    return tech_news_candidates_for(lookup_news_pool,time(NULL),query->repeat,preferred>=0 ? sources[preferred] : NULL,excluded);
}
static cJSON *lookup_tech_news_attempt(lookup_job_t *j,const cJSON *excluded,cJSON **rejected)
{
    if (!j->web_key[0]) return lookup_error("web_key_required");
    if (!lookup_news_pool || esp_timer_get_time()>=lookup_news_expires) {
        cJSON_Delete(lookup_news_pool); lookup_news_pool=tech_news_feed(time(NULL),true);
        memset(lookup_news_pages,0,sizeof(lookup_news_pages)); lookup_news_next=0;
        lookup_news_expires=esp_timer_get_time()+600000000;
    }
    if (!lookup_news_pool) return lookup_error("insufficient_memory");
    tech_news_prune(lookup_news_pool,time(NULL),j->query.repeat);
    cJSON *feed=lookup_news_candidates(excluded,&j->query);
    if (!feed) return lookup_error("insufficient_memory");
    bool changed_query=j->query.query[0]!=0 || excluded!=NULL;
    unsigned fetched=0,budget=1;
    while (lookup_current(j) && fetched<budget && j->news_fetches<2 &&
        (changed_query || !cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(feed,"items")))) {
        unsigned source=4;
        int preferred=lookup_news_preferred(&j->query);
        for (unsigned offset=0;offset<(preferred>=0 ? 1U : 4U);++offset) {
            unsigned candidate=preferred>=0 ? (unsigned)preferred : (lookup_news_next+offset)%4;
            if (lookup_news_pages[candidate]<((candidate==0) ? 1U : 8U) &&
                esp_timer_get_time()>=lookup_news_retry[candidate]) { source=candidate; break; }
        }
        if (source==4) break;
        lookup_news_next=(source+1)%4;
        unsigned page=++lookup_news_pages[source];
        if (source==0) snprintf(j->url,sizeof(j->url),"%s",HISU_TECH_ITHOME_URL);
        else if (source==1) snprintf(j->url,sizeof(j->url),"https://genk.vn/ajax-home/page-%u.chn",page);
        else if (source==2) snprintf(j->url,sizeof(j->url),"%s&page=%u",HISU_TECH_QBIT_URL,page);
        else snprintf(j->url,sizeof(j->url),"%s&page=%u",HISU_TECH_TECHCRUNCH_URL,page);
        ++fetched; ++j->news_fetches; changed_query=false;
        bool downloaded=lookup_download(j,true);
        if (!downloaded || strstr(j->body,"\\u0000")) {
            --lookup_news_pages[source];
            if (lookup_current(j)) lookup_news_retry[source]=esp_timer_get_time()+300000000;
            budget=2;
            continue;
        }
        int previous_count=cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(lookup_news_pool,"items"));
        cJSON *data=source!=1 && hisu_memory_json_shallow(j->body,12) ? cJSON_ParseWithOpts(j->body,NULL,true) : NULL;
        bool parsed=source==1 ? tech_news_append_genk(lookup_news_pool,j->body,time(NULL)) :
            tech_news_append(lookup_news_pool,data,source==0 ? 0 : source==2 ? 1 : 2,time(NULL));
        /* Put newly fetched candidates first, so a rejected old batch cannot
         * hide another publisher behind the twelve-item summary limit. */
        cJSON *pool=cJSON_GetObjectItemCaseSensitive(lookup_news_pool,"items");
        int added=cJSON_GetArraySize(pool)-previous_count;
        for (int i=0;i<added;++i) {
            cJSON *row=cJSON_DetachItemFromArray(pool,previous_count+i);
            if (!cJSON_InsertItemInArray(pool,i,row)) { cJSON_Delete(row); parsed=false; break; }
        }
        cJSON_Delete(data);
        if (!parsed) { --lookup_news_pages[source]; lookup_news_retry[source]=esp_timer_get_time()+300000000; }
        ESP_LOGI("LOOKUP","Tech source=%s page=%u parsed=%d pool=%d",source==0 ? "ITHome" : source==1 ? "GenK" : source==2 ? "QbitAI" : "TechCrunch",page,parsed,
            cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(lookup_news_pool,"items")));
        cJSON_Delete(feed); feed=lookup_news_candidates(excluded,&j->query);
        if (!feed) return lookup_error("insufficient_memory");
        if (!cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(feed,"items"))) budget=2;
    }
    int available=cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(feed,"items"));
    ESP_LOGI("LOOKUP","Tech batch: candidates=%d fetched=%u pool=%d",available,fetched,
        cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(lookup_news_pool,"items")));
    if (!lookup_current(j) || !available) {
        cJSON_Delete(feed);
        return lookup_error("news_scan_incomplete");
    }
    char *request=tech_news_request(&j->query,feed);
    if (!request) { cJSON_Delete(feed); return lookup_error("insufficient_memory"); }
    snprintf(j->url,sizeof(j->url),"%s",HISU_TECH_SUMMARY_URL);
    j->post_body=request;
    int64_t summary_start=esp_timer_get_time();
    bool downloaded=lookup_download(j,false);
    j->post_body=NULL; free(request);
    if (!downloaded) { cJSON_Delete(feed); return lookup_error(j->http_status==429 || j->http_status==403 || j->http_status==401 ? "web_quota" : "source_unavailable"); }
    cJSON *root=!strstr(j->body,"\\u0000") && hisu_memory_json_shallow(j->body,16) ? cJSON_ParseWithOpts(j->body,NULL,true) : NULL;
    cJSON *result=tech_news_result(root,feed);
    cJSON_Delete(root);
    const cJSON *error=cJSON_GetObjectItemCaseSensitive(result,"error");
    if (cJSON_IsString(error) && !strcmp(error->valuestring,"news_batch_empty")) { *rejected=feed; feed=NULL; }
    cJSON_Delete(feed);
    ESP_LOGI("LOOKUP","Tech summary: elapsed_ms=%u remaining=%d",(unsigned)((esp_timer_get_time()-summary_start)/1000),
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(result,"remaining_candidates")) ? cJSON_GetObjectItemCaseSensitive(result,"remaining_candidates")->valueint : -1);
    if (lookup_current(j) && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok"))) {
        char *view=lookup_print(result);
        bool published=view && hisu_media_research_publish(view);
        free(view);
        if (!published) { cJSON_Delete(result); return lookup_error("insufficient_memory"); }
    }
    return result;
}
static void lookup_news_remember(const cJSON *result)
{
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok"))) return;
    if (!lookup_news_recent) lookup_news_recent=cJSON_CreateArray();
    if (!lookup_news_recent) return;
    const cJSON *row;
    cJSON_ArrayForEach(row,cJSON_GetObjectItemCaseSensitive(result,"sources")) {
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(row,"article_id"),*old;
        if (!cJSON_IsString(id)) continue;
        bool exists=false;
        cJSON_ArrayForEach(old,lookup_news_recent) {
            const cJSON *previous=cJSON_GetObjectItemCaseSensitive(old,"article_id");
            if (cJSON_IsString(previous) && !strcmp(previous->valuestring,id->valuestring)) { exists=true; break; }
        }
        if (exists) continue;
        cJSON *copy=cJSON_Duplicate(row,true);
        if (!copy || !cJSON_InsertItemInArray(lookup_news_recent,0,copy)) { cJSON_Delete(copy); continue; }
        while (cJSON_GetArraySize(lookup_news_recent)>12) cJSON_DeleteItemFromArray(lookup_news_recent,12);
    }
}
static bool lookup_news_contains(const char *title,const char *query)
{
    if (!query[0]) return false;
    for (const char *start=title;*start;++start) {
        const unsigned char *a=(const unsigned char *)start,*b=(const unsigned char *)query;
        while (*a && *b) {
            unsigned char x=*a,y=*b;
            if (x>='A' && x<='Z') x=(unsigned char)(x+'a'-'A');
            if (y>='A' && y<='Z') y=(unsigned char)(y+'a'-'A');
            if (x!=y) break;
            ++a; ++b;
        }
        if (!*b) return true;
    }
    return false;
}
static cJSON *lookup_news_article_pick(const lookup_query_t *query)
{
    cJSON *selected=NULL,*row; unsigned matches=0;
    cJSON_ArrayForEach(row,lookup_news_recent) {
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(row,"article_id"),*title=cJSON_GetObjectItemCaseSensitive(row,"title"),*published=cJSON_GetObjectItemCaseSensitive(row,"published_at_utc");
        if (!cJSON_IsString(id) || !cJSON_IsString(title) || !cJSON_IsNumber(published) ||
            published->valuedouble<time(NULL)-7*86400 || published->valuedouble>time(NULL)+300) continue;
        if (query->article_id[0] ? !strcmp(id->valuestring,query->article_id) : lookup_news_contains(title->valuestring,query->query)) { selected=row; ++matches; }
    }
    return matches==1 ? selected : NULL;
}
static cJSON *lookup_news_article(lookup_job_t *j)
{
    cJSON *article=lookup_news_article_pick(&j->query);
    if (!article) {
        cJSON *error=lookup_error("article_not_found"),*choices=lookup_news_recent ? cJSON_Duplicate(lookup_news_recent,true) : NULL;
        /* Never expose cached body text just to ask which story was meant. */
        cJSON *row;
        cJSON_ArrayForEach(row,choices) { cJSON_DeleteItemFromObjectCaseSensitive(row,"article_text"); cJSON_DeleteItemFromObjectCaseSensitive(row,"content_expires"); }
        if (choices && !cJSON_AddItemToObject(error,"choices",choices)) cJSON_Delete(choices);
        return error;
    }
    if (!j->web_key[0]) return lookup_error("web_key_required");
    const cJSON *body=cJSON_GetObjectItemCaseSensitive(article,"article_text"),*expires=cJSON_GetObjectItemCaseSensitive(article,"content_expires");
    bool cached=cJSON_IsString(body) && cJSON_IsNumber(expires) && expires->valuedouble>esp_timer_get_time();
    if (!cached) {
        if (!tech_news_article_url(article,j->url,sizeof(j->url))) return lookup_error("article_unverified");
        j->article_get=true;
        bool downloaded=lookup_download(j,true); j->article_get=false;
        if (!downloaded) return lookup_error("article_unavailable");
        bool truncated=false;
        char *text=tech_news_article_text(article,j->body,j->used,&truncated);
        if (!text) return lookup_error("article_unverified");
        cJSON_DeleteItemFromObjectCaseSensitive(article,"article_text");
        cJSON_DeleteItemFromObjectCaseSensitive(article,"article_truncated");
        cJSON_DeleteItemFromObjectCaseSensitive(article,"content_expires");
        bool stored=cJSON_AddStringToObject(article,"article_text",text) &&
            cJSON_AddBoolToObject(article,"article_truncated",truncated) &&
            cJSON_AddNumberToObject(article,"content_expires",(double)(esp_timer_get_time()+600000000));
        free(text);
        if (!stored) return lookup_error("insufficient_memory");
    }
    cJSON *feed=tech_news_feed(time(NULL),true),*copy=cJSON_Duplicate(article,true);
    if (!feed || !copy) { cJSON_Delete(feed); cJSON_Delete(copy); return lookup_error("insufficient_memory"); }
    cJSON_DeleteItemFromObjectCaseSensitive(copy,"content_expires");
    if (!cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(feed,"items"),copy)) { cJSON_Delete(copy); cJSON_Delete(feed); return lookup_error("insufficient_memory"); }
    if (!cJSON_AddBoolToObject(feed,"requested_article",true)) { cJSON_Delete(feed); return lookup_error("insufficient_memory"); }
    char *request=tech_news_request(&j->query,feed);
    if (!request) { cJSON_Delete(feed); return lookup_error("insufficient_memory"); }
    snprintf(j->url,sizeof(j->url),"%s",HISU_TECH_SUMMARY_URL); j->post_body=request;
    bool downloaded=lookup_download(j,false); j->post_body=NULL; free(request);
    if (!downloaded) { cJSON_Delete(feed); return lookup_error(j->http_status==429 ? "web_quota" : "source_unavailable"); }
    cJSON *root=!strstr(j->body,"\\u0000") && hisu_memory_json_shallow(j->body,16) ? cJSON_ParseWithOpts(j->body,NULL,true) : NULL;
    cJSON *result=tech_news_result(root,feed); cJSON_Delete(root); cJSON_Delete(feed);
    if (lookup_current(j) && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok"))) {
        char *view=lookup_print(result); bool published=view && hisu_media_research_publish(view); free(view);
        if (!published) { cJSON_Delete(result); return lookup_error("insufficient_memory"); }
    }
    ESP_LOGI("LOOKUP","Article lookup: id=%s cached_body=%d ok=%d",j->query.article_id,cached,cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok")));
    return result;
}
static cJSON *lookup_tech_news(lookup_job_t *j)
{
    if (j->query.article_id[0] || !strcmp(j->query.mode,"detail")) return lookup_news_article(j);
    cJSON *excluded=NULL,*rejected=NULL;
    cJSON *result=lookup_tech_news_attempt(j,NULL,&rejected);
    if (rejected && lookup_current(j)) {
        ESP_LOGI("LOOKUP","News batch rejected by summary; trying another source, not marking heard");
        excluded=rejected; rejected=NULL; cJSON_Delete(result);
        result=lookup_tech_news_attempt(j,excluded,&rejected);
    }
    cJSON_Delete(excluded); cJSON_Delete(rejected);
    const cJSON *error=cJSON_GetObjectItemCaseSensitive(result,"error");
    if (cJSON_IsString(error) && !strcmp(error->valuestring,"news_batch_empty")) {
        cJSON_Delete(result); result=lookup_error("news_scan_incomplete");
    }
    if (lookup_current(j)) lookup_news_remember(result);
    return result;
}
static cJSON *lookup_run(lookup_job_t *j) {
    if (time(NULL)<1700000000) return lookup_error("clock_unsynced");
    if (j->query.weather && !j->query.place[0]) return lookup_error("place_required");
    bool web=j->query.news || j->query.slang;
    unsigned slot=j->query.weather ? 1 : 0;
    if (!web && lookup_cache[slot].json && esp_timer_get_time()<lookup_cache[slot].expires &&
        (!slot || lookup_cache[slot].local_day==((int64_t)time(NULL)+25200)/86400) &&
        !memcmp(&lookup_cache[slot].query,&j->query,sizeof(j->query))) {
        cJSON *cached=cJSON_Parse(lookup_cache[slot].json);
        if (cached) {
            ESP_LOGI("LOOKUP","Cache hit: slot=%u json_bytes=%u",slot,(unsigned)strlen(lookup_cache[slot].json));
            cJSON_AddBoolToObject(cached,"cached",true); return cached;
        }
    }
    if (esp_timer_get_time()<lookup_next_request) return lookup_error("rate_limited");
    lookup_next_request=esp_timer_get_time()+(web ? 10000000 : 2000000);
    j->body_capacity=j->query.news ? (j->query.article_id[0] || !strcmp(j->query.mode,"detail") ? 262144 : 131072) : web ? HISU_WEB_BODY_LIMIT : LOOKUP_BODY_LIMIT;
    j->body=heap_caps_malloc(j->body_capacity+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!j->body) return lookup_error("insufficient_memory");
    cJSON *result=NULL;
    if (j->query.news) result=lookup_tech_news(j);
    else if (web) result=lookup_web(j);
    else if (!j->query.weather) {
        strcpy(j->url,"https://www.vang.today/api/prices");
        cJSON *root=lookup_fetch(j);
        result=root ? lookup_gold(root,&j->query,time(NULL)) : lookup_error("source_unavailable"); cJSON_Delete(root);
        if (j->query.gold_history && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok")) &&
            !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"stale"))) {
            snprintf(j->url,sizeof(j->url),"https://www.vang.today/api/prices?type=%s&days=7",j->query.type);
            root=lookup_fetch(j);
            lookup_gold_history(result,root,&j->query);
            cJSON_Delete(root);
        }
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
    const cJSON *error=cJSON_GetObjectItemCaseSensitive(result,"error");
    bool unavailable=cJSON_IsString(error) &&
        (!strcmp(error->valuestring,"source_unavailable") || !strcmp(error->valuestring,"news_unavailable"));
    /* Cache short failures too: the model must not repeat a 12-second request in one turn.
     * A deadline is not cancellation; never cache an interrupted or disconnected request. */
    hisu_network_link_t link; hisu_network_get_link(&link);
    bool valid=j->current(j->result.epoch,j->result.generation) && lookup_link_allowed(&link);
    if (!web && ((unavailable && valid) || (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok")) && lookup_current(j)))) {
        char *json=lookup_print(result);
        if (json) {
            free(lookup_cache[slot].json); lookup_cache[slot].json=json;
            lookup_cache[slot].query=j->query;
            int64_t ttl=unavailable ? 15000000 : slot ? 600000000 : 300000000;
            lookup_cache[slot].expires=esp_timer_get_time()+ttl;
            lookup_cache[slot].local_day=((int64_t)time(NULL)+25200)/86400;
            ESP_LOGI("LOOKUP","Cache stored: slot=%u json_bytes=%u memory=PSRAM",slot,(unsigned)strlen(json));
        }
    }
    return result;
}
static void lookup_gold_preview(lookup_job_t *j, const cJSON *data)
{
    const cJSON *price = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(data,"prices"),0);
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(price,"name"), *stamp = cJSON_GetObjectItemCaseSensitive(data,"updated_at_utc");
    if (!cJSON_IsString(name) || !cJSON_IsNumber(stamp) || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(data,"stale"))) return;

    time_t local = (time_t)stamp->valuedouble + 25200;
    struct tm date;
    char clock[6];
    if (!gmtime_r(&local,&date) || !strftime(clock,sizeof(clock),"%H:%M",&date)) return;
    const char *key = cJSON_GetObjectItemCaseSensitive(price,"spot_reference") ? "spot_reference" : "sell";
    const cJSON *latest = cJSON_GetObjectItemCaseSensitive(price,key);
    if (!cJSON_IsNumber(latest)) return;
    bool world = !strcmp(key,"spot_reference");
    double scale = world ? 1.0 : 1000000.0;
    const char *unit = world ? "USD/oz" : "trđ/lượng";
    const cJSON *entry, *previous = NULL, *previous_date = NULL;
    cJSON_ArrayForEach(entry,cJSON_GetObjectItemCaseSensitive(data,"history")) {
        const cJSON *day = cJSON_GetObjectItemCaseSensitive(entry,"date"), *value = cJSON_GetObjectItemCaseSensitive(entry,key);
        if (cJSON_IsString(day) && cJSON_IsNumber(value) &&
            (!previous_date || strcmp(day->valuestring,previous_date->valuestring)>0)) {
            previous_date = day;
            previous = value;
        }
    }
    if (previous) {
        double change = (latest->valuedouble - previous->valuedouble)/scale;
        snprintf(j->result.gold_preview,sizeof(j->result.gold_preview),"%s  ·  %s\n%s %+.2f %s",
                 name->valuestring,clock,change > 0 ? "Tăng" : change < 0 ? "Giảm" : "Ngang",change,unit);
    } else {
        snprintf(j->result.gold_preview,sizeof(j->result.gold_preview),"%s  ·  %s\n%.2f %s",
                 name->valuestring,clock,latest->valuedouble/scale,unit);
    }
}

static void lookup_worker(void *arg) {
    lookup_job_t *j=arg;
    lookup_log_memory(j,"worker_start");
    cJSON *data=lookup_run(j);
    memset(j->web_key,0,sizeof(j->web_key));
    const cJSON *error=cJSON_GetObjectItemCaseSensitive(data,"error");
    if (cJSON_IsString(error)) ESP_LOGI("LOOKUP","Result error=%s",error->valuestring);
    j->result.response=lookup_print(data);
    if (!j->query.news && !j->query.weather && !j->query.slang) lookup_gold_preview(j,data);
    if (j->query.news && j->result.response && lookup_current(j)) tech_news_stage(data,j->result.epoch,j->result.generation);
    cJSON_Delete(data);
    free(j->body); j->body=NULL;
    lookup_log_memory(j,"worker_cleaned");
    unsigned stack_free=(unsigned)uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI("LOOKUP","Complete: topic=%s response_bytes=%u stack_free=%u stack_bytes=%u stack_low=%d",
        j->query.slang ? "slang" : j->query.news ? "news" : j->query.weather ? "weather" : "gold",
        j->result.response ? (unsigned)strlen(j->result.response) : 0,stack_free,
        LOOKUP_STACK_BYTES,stack_free<LOOKUP_STACK_LOW_BYTES);
    portENTER_CRITICAL(&lookup_mux); lookup_done=true; portEXIT_CRITICAL(&lookup_mux);
    /* A consumer reaps this suspended task, avoiding a temporary cleanup task. */
    vTaskSuspend(NULL);
}
static bool lookup_reject(const char *reason) {
    ESP_LOGW("LOOKUP","Rejected: %s; internal free=%u largest=%u PSRAM free=%u",reason,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    return false;
}
bool voice_lookup_submit(uint32_t epoch,uint32_t generation,const char *id,const cJSON *args,
                         bool (*current)(uint32_t,uint32_t)) {
    if (!id || !id[0] || strlen(id)>=128 || !current) return lookup_reject("invalid_call");
    if (voice_lookup_busy()) return lookup_reject("busy");
    lookup_job_t *j=heap_caps_calloc(1,sizeof(*j),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!j) return lookup_reject("job_allocation");
    if (!lookup_query_parse(args,&j->query)) { free(j); return lookup_reject("invalid_arguments"); }
    /* Stack is external; retain internal headroom for TCB, sockets and HTTP/TLS. */
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<10240 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<2048) {
        free(j); return lookup_reject("internal_memory");
    }
    j->result.epoch=epoch; j->result.generation=generation; strcpy(j->result.id,id);
    j->current=current; j->deadline=esp_timer_get_time()+((j->query.news || j->query.slang) ? 45000000 : 12000000);
    if (j->query.news || j->query.slang) {
        if (hisu_settings_get_gemini_profile(HISU_GEMINI_CHAT,j->web_key)!=ESP_OK)
            memset(j->web_key,0,sizeof(j->web_key));
    }
    portENTER_CRITICAL(&lookup_mux); lookup_job=j; lookup_done=false; portEXIT_CRITICAL(&lookup_mux);
    TaskHandle_t task=NULL;
    if (xTaskCreateWithCaps(lookup_worker,"info_lookup",LOOKUP_STACK_BYTES,j,3,&task,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
        portENTER_CRITICAL(&lookup_mux); lookup_job=NULL; portEXIT_CRITICAL(&lookup_mux);
        memset(j->web_key,0,sizeof(j->web_key));
        free(j); return lookup_reject("worker_allocation");
    }
    portENTER_CRITICAL(&lookup_mux); j->task=task; portEXIT_CRITICAL(&lookup_mux);
    return true;
}
voice_lookup_result_t *voice_lookup_take(void) {
    portENTER_CRITICAL(&lookup_mux);
    lookup_job_t *j=lookup_done && !lookup_reaping ? lookup_job : NULL;
    TaskHandle_t task=j ? j->task : NULL;
    if (task) lookup_reaping=true;
    portEXIT_CRITICAL(&lookup_mux);
    if (!task) return NULL;
    /* Completion can be published before suspension; serialize both possible consumers. */
    if (eTaskGetState(task)!=eSuspended) {
        portENTER_CRITICAL(&lookup_mux); lookup_reaping=false; portEXIT_CRITICAL(&lookup_mux);
        return NULL;
    }
    vTaskDeleteWithCaps(task);
    portENTER_CRITICAL(&lookup_mux);
    lookup_job=NULL; lookup_done=lookup_reaping=false;
    portEXIT_CRITICAL(&lookup_mux); return &j->result;
}
void voice_lookup_free(voice_lookup_result_t *result) {
    if (result) { free(result->response); free(result); }
}
void voice_lookup_spoken(uint32_t epoch,uint32_t generation) { tech_news_spoken(epoch,generation); }
