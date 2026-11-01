#include "hisu_voice_slang.h"
#include "hisu_voice_lookup_json.h"
#include "hisu_memory_json_guard.h"
#include "hisu_settings.h"
#include "hisu_network.h"
#include "hisu_power.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#define SLANG_BYTES 6144
#define SLANG_BODY 32768
#define SLANG_COUNT 12
static char *s_dictionary;
static bool s_loaded;
static int64_t s_next_attempt;
static const cJSON *slang_field(const cJSON *o,const char *key) { return cJSON_GetObjectItemCaseSensitive(o,key); }
static bool slang_text(const cJSON *o,size_t limit) {
    if (!cJSON_IsString(o) || !o->valuestring[0] || strlen(o->valuestring)>limit) return false;
    for (const unsigned char *p=(unsigned char *)o->valuestring;*p;++p) if (*p<32 || *p==127) return false;
    return true;
}
static bool slang_id(const cJSON *o) {
    return cJSON_IsNumber(o) && isfinite(o->valuedouble) && o->valuedouble>=1 &&
        o->valuedouble<=2147483647 && floor(o->valuedouble)==o->valuedouble;
}
bool voice_slang_snapshot_valid(const cJSON *root,int64_t now)
{
    const cJSON *stamp=slang_field(root,"updated_at_utc"),*rows=slang_field(root,"entries");
    if (!cJSON_IsNumber(stamp) || !isfinite(stamp->valuedouble) || stamp->valuedouble<1704067200 ||
        stamp->valuedouble>4102444800LL || (now>=1704067200 && stamp->valuedouble>now+300) ||
        !cJSON_IsArray(rows) || cJSON_GetArraySize(rows)<1 || cJSON_GetArraySize(rows)>SLANG_COUNT) return false;
    const cJSON *row;
    cJSON_ArrayForEach(row,rows)
        if (!slang_text(slang_field(row,"term"),64) || !slang_text(slang_field(row,"meaning"),320) ||
            !slang_id(slang_field(row,"pageid"))) return false;
    return true;
}
/* Only dictionary definition text, never quoted examples, HTML or scripts. */
static bool slang_plain(const char *html,char out[321])
{
    size_t used=0; bool tag=false;
    if (!html || strlen(html)>4096) return false;
    for (const unsigned char *p=(const unsigned char *)html;*p;++p) {
        if (*p=='<') { tag=true; continue; }
        if (tag) { if (*p=='>') tag=false; continue; }
        unsigned char c=*p;
        if (c=='&') {
            static const char *names[]={"amp;","lt;","gt;","quot;","apos;","#39;","nbsp;"};
            static const char chars[]={'&','<','>','"','\'','\'',' '};
            unsigned i=0; while (i<7 && strncmp((const char *)p+1,names[i],strlen(names[i]))) ++i;
            if (i==7) return false;
            c=chars[i]; p+=strlen(names[i]);
        }
        if (c<=32) c=' ';
        if (c==127) return false;
        if (c==' ' && (!used || out[used-1]==' ')) continue;
        if (used>=320) return false;
        out[used++]=(char)c;
    }
    while (used && out[used-1]==' ') --used;
    out[used]=0;
    return !tag && used>=3;
}
bool voice_slang_definition(const cJSON *root,char out[321])
{
    const cJSON *group,*definition;
    cJSON_ArrayForEach(group,slang_field(root,"vi")) {
        cJSON_ArrayForEach(definition,slang_field(group,"definitions")) {
            const cJSON *html=slang_field(definition,"definition");
            if (cJSON_IsString(html) && slang_plain(html->valuestring,out)) return true;
        }
    }
    out[0]=0; return false;
}
static void slang_load(void)
{
    if (s_loaded) return;
    s_loaded=true;
    char *text=heap_caps_malloc(SLANG_BYTES+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!text) { s_loaded=false; return; }
    cJSON *root=NULL;
    if (hisu_settings_load_slang(text,SLANG_BYTES+1)==ESP_OK &&
        hisu_memory_json_shallow(text,8) && !strstr(text,"\\u0000")) root=cJSON_Parse(text);
    if (voice_slang_snapshot_valid(root,time(NULL))) s_dictionary=text;
    else free(text);
    cJSON_Delete(root);
}
char *voice_slang_context(void)
{
    slang_load();
    if (!s_dictionary) return NULL;
    size_t bytes=strlen(s_dictionary)+1;
    char *copy=heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (copy) memcpy(copy,s_dictionary,bytes);
    return copy;
}
typedef struct {
    char *body; size_t used; bool overflow;
    bool (*idle)(void); int64_t deadline;
} slang_request_t;
static bool slang_allowed(bool (*idle)(void))
{
    if (!idle || !idle() || hisu_settings_get(HISU_SETTING_SLANG_AUTO)!=1) return false;
    hisu_power_status_t power; hisu_power_get(&power);
    hisu_network_link_t link; hisu_network_get_link(&link);
    return power.valid && (power.external_power || power.percent>=30) && link.connected &&
        link.internet==HISU_INTERNET_ONLINE && !link.setup_enabled;
}
static esp_err_t slang_receive(esp_http_client_event_t *event)
{
    slang_request_t *r=event->user_data;
    if (!slang_allowed(r->idle) || esp_timer_get_time()>=r->deadline) return ESP_FAIL;
    if (event->event_id==HTTP_EVENT_ON_DATA && event->data_len>0) {
        if ((size_t)event->data_len>SLANG_BODY-r->used || memchr(event->data,0,event->data_len)) {
            r->overflow=true; return ESP_FAIL;
        }
        memcpy(r->body+r->used,event->data,event->data_len);
        r->used+=event->data_len; r->body[r->used]=0;
    }
    return ESP_OK;
}
static cJSON *slang_fetch(slang_request_t *r,const char *url)
{
    if (!slang_allowed(r->idle) || heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<10240 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<2048) return NULL;
    r->used=0; r->overflow=false; r->body[0]=0;
    esp_http_client_config_t config={.url=url,.timeout_ms=1000,.is_async=true,.disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach,.event_handler=slang_receive,.user_data=r,.buffer_size=2048,.buffer_size_tx=2048};
    esp_http_client_handle_t http=esp_http_client_init(&config);
    if (!http) return NULL;
    esp_err_t err=esp_http_client_set_header(http,"User-Agent","HisuOS/1.0 (personal ESP32 dictionary updater)");
    if (err==ESP_OK) err=esp_http_client_set_header(http,"Accept-Encoding","identity");
    int64_t deadline=esp_timer_get_time()+5000000;
    if (err==ESP_OK) do {
        if (!slang_allowed(r->idle) || esp_timer_get_time()>=deadline || esp_timer_get_time()>=r->deadline) { err=ESP_FAIL; break; }
        err=esp_http_client_perform(http);
        if (err==ESP_ERR_HTTP_EAGAIN) vTaskDelay(pdMS_TO_TICKS(20));
    } while (err==ESP_ERR_HTTP_EAGAIN);
    bool ok=err==ESP_OK && esp_http_client_get_status_code(http)==200 && !r->overflow &&
        esp_http_client_is_complete_data_received(http);
    esp_http_client_cleanup(http);
    if (!ok || !slang_allowed(r->idle) || strstr(r->body,"\\u0000") || !hisu_memory_json_shallow(r->body,12)) return NULL;
    return cJSON_ParseWithOpts(r->body,NULL,true);
}
static void slang_encode(char out[193],const char *text)
{
    static const char hex[]="0123456789ABCDEF";
    for (const unsigned char *p=(const unsigned char *)text;*p;++p) {
        *out++='%'; *out++=hex[*p>>4]; *out++=hex[*p&15];
    }
    *out=0;
}
bool voice_slang_poll(bool (*idle)(void))
{
    int64_t now=time(NULL);
    if (now<1704067200 || now>=4102444800LL || esp_timer_get_time()<s_next_attempt || !slang_allowed(idle)) return false;
    slang_load();
    cJSON *dictionary=s_dictionary ? cJSON_Parse(s_dictionary) : NULL;
    const cJSON *stamp=slang_field(dictionary,"updated_at_utc");
    if (cJSON_IsNumber(stamp) && now>=stamp->valuedouble && now-stamp->valuedouble<86400) {
        cJSON_Delete(dictionary); s_next_attempt=esp_timer_get_time()+60000000; return false;
    }
    s_next_attempt=esp_timer_get_time()+3600000000LL;
    slang_request_t request={.idle=idle,.deadline=esp_timer_get_time()+30000000};
    request.body=heap_caps_malloc(SLANG_BODY+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    cJSON *list=request.body ? slang_fetch(&request,"https://en.wiktionary.org/w/api.php?action=query&format=json&formatversion=2&list=categorymembers&cmtitle=Category%3AVietnamese_internet_slang&cmnamespace=0&cmsort=timestamp&cmdir=descending&cmlimit=12") : NULL;
    const cJSON *members=slang_field(slang_field(list,"query"),"categorymembers");
    if (!dictionary) {
        dictionary=cJSON_CreateObject();
        if (dictionary) {
            cJSON_AddArrayToObject(dictionary,"entries");
            cJSON_AddStringToObject(dictionary,"source","Wiktionary contributors");
            cJSON_AddStringToObject(dictionary,"license","CC-BY-SA-4.0; definitions adapted by removing HTML; https://creativecommons.org/licenses/by-sa/4.0/");
        }
    }
    cJSON *entries=cJSON_GetObjectItemCaseSensitive(dictionary,"entries");
    unsigned updated=0;
    if (cJSON_IsArray(members) && cJSON_GetArraySize(members)<=12 && cJSON_IsArray(entries)) {
        unsigned count=cJSON_GetArraySize(members);
        for (unsigned n=0;n<count && n<6 && slang_allowed(idle) && esp_timer_get_time()<request.deadline;++n) {
            const cJSON *member=cJSON_GetArrayItem(members,n);
            const cJSON *title=slang_field(member,"title"),*id=slang_field(member,"pageid");
            if (!slang_text(title,64) || !slang_id(id)) continue;
            char encoded[193],url[272],meaning[321];
            slang_encode(encoded,title->valuestring);
            snprintf(url,sizeof(url),"https://en.wiktionary.org/api/rest_v1/page/definition/%s",encoded);
            cJSON *definition=slang_fetch(&request,url);
            bool valid=voice_slang_definition(definition,meaning); cJSON_Delete(definition);
            if (!valid) continue;
            cJSON *row=cJSON_CreateObject();
            if (!row || !cJSON_AddStringToObject(row,"term",title->valuestring) ||
                !cJSON_AddStringToObject(row,"meaning",meaning) || !cJSON_AddNumberToObject(row,"pageid",id->valuedouble)) {
                cJSON_Delete(row); break;
            }
            for (int i=cJSON_GetArraySize(entries)-1;i>=0;--i) {
                const cJSON *old=slang_field(cJSON_GetArrayItem(entries,i),"pageid");
                if (slang_id(old) && old->valuedouble==id->valuedouble) cJSON_DeleteItemFromArray(entries,i);
            }
            if (cJSON_GetArraySize(entries)>=SLANG_COUNT) cJSON_DeleteItemFromArray(entries,0);
            if (!cJSON_AddItemToArray(entries,row)) { cJSON_Delete(row); break; }
            ++updated;
        }
    }
    esp_err_t saved=ESP_FAIL;
    if (updated && slang_allowed(idle)) {
        cJSON_DeleteItemFromObjectCaseSensitive(dictionary,"updated_at_utc");
        cJSON_AddNumberToObject(dictionary,"updated_at_utc",(double)now);
        char *text=heap_caps_malloc(SLANG_BYTES+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (text && voice_slang_snapshot_valid(dictionary,now) && cJSON_PrintPreallocated(dictionary,text,SLANG_BYTES+1,false) && slang_allowed(idle)) {
            saved=hisu_settings_save_slang(text);
            if (saved==ESP_OK) { free(s_dictionary); s_dictionary=text; text=NULL; s_next_attempt=esp_timer_get_time()+86400000000LL; }
        }
        free(text);
    }
    if (!slang_allowed(idle)) s_next_attempt=esp_timer_get_time()+30000000;
    ESP_LOGI("SLANG","Auto update: definitions=%u saved=%d stack_free=%u",updated,(int)saved,(unsigned)uxTaskGetStackHighWaterMark(NULL));
    cJSON_Delete(list); cJSON_Delete(dictionary); free(request.body);
    return true;
}
