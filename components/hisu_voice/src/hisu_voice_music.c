#include "hisu_voice_music.h"
#include "hisu_voice_provider_json.h"
#include "hisu_voice_music_id3.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_audio_simple_dec.h"
#include "esp_mp3_dec.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define JSON_LIMIT (64 * 1024)
static portMUX_TYPE s_music_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_ticket;
static bool s_busy;

bool hisu_music_completed(uint32_t ticket)
{
    portENTER_CRITICAL(&s_music_mux);
    bool completed=!s_busy && ticket==s_ticket;
    portEXIT_CRITICAL(&s_music_mux);
    return completed;
}
bool hisu_music_current(uint32_t ticket)
{
    portENTER_CRITICAL(&s_music_mux);
    bool current = s_busy && ticket == s_ticket;
    portEXIT_CRITICAL(&s_music_mux);
    return current;
}
bool hisu_music_cancel(void)
{
    portENTER_CRITICAL(&s_music_mux);
    bool busy = s_busy;
    ++s_ticket; s_busy = false;
    portEXIT_CRITICAL(&s_music_mux);
    return busy;
}
bool hisu_music_busy(void)
{
    portENTER_CRITICAL(&s_music_mux); bool busy=s_busy; portEXIT_CRITICAL(&s_music_mux);
    return busy;
}
void hisu_music_finish(uint32_t ticket)
{
    portENTER_CRITICAL(&s_music_mux);
    if (ticket == s_ticket) s_busy = false;
    portEXIT_CRITICAL(&s_music_mux);
}

typedef struct {
    char encoded_query[541];
    bool html, compressed, gzip_encoding;
    struct {
        char url[1024];
    } api;
    char *body;
    size_t used;
    bool overflow;
    uint32_t ticket;
    int timeout_ms;
} request_t;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    request_t *request = event->user_data;
    if (request->ticket && !hisu_music_current(request->ticket)) return ESP_FAIL;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value) {
        if (!strcasecmp(event->header_key,"Content-Encoding")) {
            request->compressed = strcasecmp(event->header_value,"identity") != 0;
            request->gzip_encoding = !strcasecmp(event->header_value,"gzip");
        }
        if (!strcasecmp(event->header_key,"Content-Type"))
            request->html = !strncasecmp(event->header_value,"text/html",9);
    }
    if (event->event_id == HTTP_EVENT_ON_DATA && request->body && event->data_len > 0) {
        if ((size_t)event->data_len > JSON_LIMIT - request->used) {
            request->overflow = true; return ESP_FAIL;
        }
        memcpy(request->body+request->used,event->data,event->data_len);
        request->used += event->data_len;
        request->body[request->used] = 0;
    }
    return ESP_OK;
}

static esp_http_client_handle_t http_new(const char *url, int timeout_ms,
    esp_err_t (*handler)(esp_http_client_event_t *), void *user_data)
{
    esp_http_client_config_t cfg = {.url=url, .timeout_ms=timeout_ms,
        .crt_bundle_attach=esp_crt_bundle_attach, .disable_auto_redirect=true,
        .event_handler=handler, .user_data=user_data,
        /* Above ALWAYSINTERNAL=2048 so IDF malloc prefers PSRAM for both buffers. */
        .buffer_size=4096, .buffer_size_tx=4096, .user_agent="HisuOS/0.1"};
    return esp_http_client_init(&cfg);
}

static uint32_t little32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}

static bool gunzip_body(request_t *request)
{
    const unsigned char *in = (const unsigned char *)request->body;
    size_t n = request->used;
    if (n < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8 || (in[3]&0xe0)) return false;
    size_t end = n-8, pos = 10;
    /* RFC 1952 optional fields; raw DEFLATE itself is handled by ROM miniz. */
    if (in[3]&4) {
        if (end-pos < 2) return false;
        size_t extra = in[pos] | (size_t)in[pos+1]<<8;
        pos += 2;
        if (extra > end-pos) return false;
        pos += extra;
    }
    for (unsigned flag=8; flag<=16; flag*=2) {
        if (in[3]&flag) {
            const unsigned char *zero = memchr(in+pos,0,end-pos);
            if (!zero) return false;
            pos = (size_t)(zero-in)+1;
        }
    }
    if (in[3]&2) {
        if (end-pos < 2 || ((uint32_t)mz_crc32(0,in,pos)&0xffff) !=
            ((uint32_t)in[pos] | (uint32_t)in[pos+1]<<8)) return false;
        pos += 2;
    }
    if (pos >= end || little32(in+end+4) > JSON_LIMIT) return false;
    char *out = heap_caps_malloc(JSON_LIMIT+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    tinfl_decompressor *state = heap_caps_calloc(1,sizeof(*state),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!out || !state) { free(out); free(state); return false; }
    size_t input_size=end-pos, output_size=JSON_LIMIT;
    tinfl_init(state);
    tinfl_status status = tinfl_decompress(state,in+pos,&input_size,(unsigned char *)out,
        (unsigned char *)out,&output_size,TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    bool ok = status == TINFL_STATUS_DONE && input_size == end-pos &&
        output_size == little32(in+end+4) && (uint32_t)mz_crc32(0,(const unsigned char *)out,output_size) == little32(in+end);
    free(state);
    if (!ok) { free(out); return false; }
    out[output_size]=0;
    free(request->body); request->body=out; request->used=output_size;
    ESP_LOGI("MUSIC","Gzip decoded: %u -> %u bytes",(unsigned)n,(unsigned)output_size);
    return true;
}

static cJSON *api(request_t *request, const char *path, const char *id, const char *query)
{
    /* Anonymous NCT API; no account token, cookie, music key or request signing. */
    int n=!strcmp(path,"/api/v2/song/lyric/detail") ? snprintf(request->api.url,sizeof(request->api.url),
        "https://graph.nct.vn%s?songKey=%s",path,id) : query ? snprintf(request->api.url,sizeof(request->api.url),
        "https://graph.nct.vn%s?keyword=%s&pageindex=1&pagesize=5&correct=true",path,query) :
        snprintf(request->api.url,sizeof(request->api.url),"https://graph.nct.vn%s/%s",path,id);
    if (n < 0 || (size_t)n >= sizeof(request->api.url)) return NULL;
    esp_http_client_handle_t http = http_new(request->api.url,request->timeout_ms ? request->timeout_ms : 5000,http_event,request);
    if (!http) return NULL;
    request->used=0; request->overflow=false; request->body[0]=0;
    request->html=request->compressed=request->gzip_encoding=false;
    esp_http_client_set_header(http,"x-os","android");
    esp_http_client_set_header(http,"Content-Type","application/json");
    esp_http_client_set_header(http,"Accept","application/json");
    esp_http_client_set_header(http,"Accept-Encoding","identity");
    errno = 0;
    esp_err_t err = esp_http_client_perform(http);
    int system_error = errno;
    int status = esp_http_client_get_status_code(http);
    bool complete = esp_http_client_is_complete_data_received(http);
    bool ok = err == ESP_OK && status == 200 && !request->overflow && complete;
    esp_http_client_cleanup(http);
    if (!ok) {
        ESP_LOGW("MUSIC","%s failed: err=%d errno=%d HTTP=%d bytes=%u complete=%d overflow=%d internal=%u largest=%u PSRAM=%u",
            path,(int)err,system_error,status,(unsigned)request->used,complete,request->overflow,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return NULL;
    }
    bool gzip = request->used >= 2 && (unsigned char)request->body[0] == 0x1f && (unsigned char)request->body[1] == 0x8b;
    if ((request->compressed && !request->gzip_encoding) ||
        ((gzip || request->gzip_encoding) && !gunzip_body(request))) {
        ESP_LOGW("MUSIC","%s unsupported/corrupt compression or decode allocation failure: bytes=%u",
            path,(unsigned)request->used);
        return NULL;
    }
    const unsigned char *body = (const unsigned char *)request->body;
    size_t first = 0;
    while (first < request->used && strchr(" \r\n\t",body[first]) && body[first]) ++first;
    if (request->html || (first < request->used && body[first] == '<')) {
        ESP_LOGW("MUSIC","%s non-JSON response: bytes=%u HTML=%d",
            path,(unsigned)request->used,request->html);
        return NULL;
    }
    const char *parse_end = NULL;
    ESP_LOGI("MUSIC","JSON before parse: internal=%u min=%u largest=%u PSRAM=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cJSON *root = voice_provider_json_parse(request->body,request->used,&parse_end);
    ESP_LOGI("MUSIC","JSON after parse: internal=%u min=%u largest=%u PSRAM=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(root,"code");
    if (!root) {
        ESP_LOGW("MUSIC","%s JSON parse/allocation failed: bytes=%u offset=%u",
            path,(unsigned)request->used,(unsigned)(parse_end ? parse_end-request->body : 0));
    } else if (!cJSON_IsNumber(code)) {
        ESP_LOGW("MUSIC","%s invalid API envelope: bytes=%u",path,(unsigned)request->used);
    } else if (code->valuedouble != 0 || cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root,"success"))) {
        ESP_LOGW("MUSIC","%s API error=%g bytes=%u",path,code->valuedouble,(unsigned)request->used);
    } else {
        ESP_LOGI("MUSIC","%s OK: bytes=%u",path,(unsigned)request->used);
    }
    return root;
}

static request_t *request_new(const char *query, esp_err_t *error)
{
    *error = ESP_ERR_NO_MEM;
    request_t *request = heap_caps_calloc(1,sizeof(*request),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (request && query && !hisu_music_encode_query(query,request->encoded_query,sizeof(request->encoded_query))) {
        *error = ESP_ERR_INVALID_ARG; free(request); return NULL;
    }
    if (request) request->body = heap_caps_malloc(JSON_LIMIT+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (request && request->body) { *error=ESP_OK; return request; }
    if (request) { free(request->body); free(request); }
    return NULL;
}

esp_err_t hisu_music_search(const char *query, cJSON *results)
{
    if (!query) return ESP_ERR_INVALID_ARG;
    esp_err_t error;
    request_t *request = request_new(query,&error);
    if (!request) return error;
    cJSON *root = api(request,"/api/v1/search/song",NULL,request->encoded_query);
    bool ok = hisu_music_parse_search(root,results);
    if (!ok) ESP_LOGW("MUSIC","Search unavailable or invalid response");
    else ESP_LOGI("MUSIC","Search results: %d",cJSON_GetArraySize(results));
    voice_provider_json_delete(root); free(request->body); free(request);
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t hisu_music_queue(const char *id, hisu_music_track_t *track)
{
    if (!track || !hisu_music_valid_id(id)) return ESP_ERR_INVALID_ARG;
    memcpy(track->id,id,strlen(id)+1);
    track->title[0]=track->artist[0]=track->url[0]=0;
    track->lyrics=NULL;
    portENTER_CRITICAL(&s_music_mux);
    track->ticket = ++s_ticket; s_busy = true;
    portEXIT_CRITICAL(&s_music_mux);
    return ESP_OK;
}

esp_err_t hisu_music_prepare(const char *id, hisu_music_track_t *track)
{
    esp_err_t err = hisu_music_queue(id,track);
    if (err != ESP_OK) return err;
    err = hisu_music_resolve(track);
    if (err != ESP_OK) hisu_music_finish(track->ticket);
    return err;
}

esp_err_t hisu_music_repeat(uint32_t previous,const char *id,hisu_music_track_t *track)
{
    if (!track || !hisu_music_valid_id(id)) return ESP_ERR_INVALID_ARG;
    memset(track,0,sizeof(*track)); strcpy(track->id,id);
    portENTER_CRITICAL(&s_music_mux);
    bool current=s_busy && previous==s_ticket;
    if (current) { track->ticket=++s_ticket; s_busy=true; }
    portEXIT_CRITICAL(&s_music_mux);
    return current ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t hisu_music_resolve(hisu_music_track_t *track)
{
    if (!track || !hisu_music_valid_id(track->id)) return ESP_ERR_INVALID_ARG;
    if (!hisu_music_current(track->ticket)) return ESP_ERR_INVALID_STATE;
    esp_err_t error;
    request_t *request = request_new(NULL,&error);
    if (!request) return error;
    if (!hisu_music_current(track->ticket)) {
        free(request->body); free(request); return ESP_ERR_INVALID_STATE;
    }
    cJSON *info = api(request,"/api/v1/song/detail",track->id,NULL);
    bool ok = hisu_music_parse_track(info,track) && hisu_music_current(track->ticket);
    if (!ok) ESP_LOGW("MUSIC","Song unavailable, restricted, or cancelled");
    voice_provider_json_delete(info); free(request->body); free(request);
    return !hisu_music_current(track->ticket) ? ESP_ERR_INVALID_STATE : ok ? ESP_OK : ESP_FAIL;
}

hisu_music_lyrics_t *hisu_music_load_lyrics(const hisu_music_track_t *track)
{
    if (!track || !hisu_music_valid_id(track->id) || !hisu_music_current(track->ticket)) return NULL;
    esp_err_t error;
    request_t *request=request_new(NULL,&error);
    if (!request) return NULL;
    request->ticket=track->ticket; request->timeout_ms=2000;
    cJSON *root=api(request,"/api/v2/song/lyric/detail",track->id,NULL);
    const cJSON *code=cJSON_GetObjectItemCaseSensitive(root,"code");
    const cJSON *data=cJSON_GetObjectItemCaseSensitive(root,"data");
    const cJSON *url=cJSON_GetObjectItemCaseSensitive(data,"timedLyric");
    const cJSON *key=cJSON_GetObjectItemCaseSensitive(data,"keyDecryptLyric");
    const cJSON *song=cJSON_GetObjectItemCaseSensitive(data,"songKey");
    char decode_key[65]={0};
    bool valid=cJSON_IsNumber(code) && code->valuedouble==0 &&
        !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root,"success")) &&
        (!song || (cJSON_IsString(song) && !strcmp(song->valuestring,track->id))) &&
        cJSON_IsString(url) && hisu_music_valid_url(url->valuestring) && strlen(url->valuestring)<sizeof(request->api.url) &&
        cJSON_IsString(key) && key->valuestring[0] && strlen(key->valuestring)<sizeof(decode_key);
    if (valid) {
        strcpy(request->api.url,url->valuestring); strcpy(decode_key,key->valuestring);
    }
    /* Never overlap provider JSON allocations with the lyric-file TLS connection. */
    voice_provider_json_delete(root);
    hisu_music_lyrics_t *lyrics=NULL;
    if (valid && hisu_music_current(track->ticket)) {
        /* API gzip decoding may have replaced body with a much smaller buffer.
         * Restore download capacity before reusing the bounded HTTP callback. */
        free(request->body);
        request->body=heap_caps_malloc(JSON_LIMIT+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!request->body) valid=false;
    }
    if (valid && hisu_music_current(track->ticket)) {
        request->used=0; request->body[0]=0; request->overflow=false;
        request->html=request->compressed=request->gzip_encoding=false;
        esp_http_client_handle_t http=http_new(request->api.url,2000,http_event,request);
        if (http) {
            esp_http_client_set_header(http,"Accept-Encoding","identity");
            esp_err_t err=esp_http_client_perform(http);
            bool complete=esp_http_client_is_complete_data_received(http);
            int status=esp_http_client_get_status_code(http);
            esp_http_client_cleanup(http);
            if (err==ESP_OK && complete && status==200 && !request->overflow && !request->html && !request->compressed &&
                hisu_music_current(track->ticket) && hisu_music_lyrics_decode(request->body,&request->used,decode_key))
                lyrics=hisu_music_lyrics_parse(request->body,request->used);
        }
    }
    memset(decode_key,0,sizeof(decode_key));
    free(request->body); free(request);
    if (lyrics && !hisu_music_current(track->ticket)) { hisu_music_lyrics_free(lyrics); lyrics=NULL; }
    ESP_LOGI("MUSIC","Timed lyrics: lines=%u, optional fallback=%d",lyrics ? lyrics->count : 0,lyrics==NULL);
    return lyrics;
}

typedef struct {
    char *location;
    bool seen, invalid;
} stream_headers_t;

static esp_err_t stream_event(esp_http_client_event_t *event)
{
    stream_headers_t *headers=event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        !strcasecmp(event->header_key,"Location")) {
        size_t n=strlen(event->header_value);
        if (headers->seen || n >= 4096) headers->invalid=true;
        else memcpy(headers->location,event->header_value,n+1);
        headers->seen=true;
    }
    return ESP_OK;
}

static bool redirect_url(const char *base, const char *location, char *out, size_t capacity)
{
    if (!hisu_music_valid_url(base) || !location || !*location) return false;
    int n;
    if (!strncmp(location,"https://",8)) n=snprintf(out,capacity,"%s",location);
    else if (!strncmp(location,"//",2)) n=snprintf(out,capacity,"https:%s",location);
    else if (location[0]=='/') {
        size_t origin=(size_t)(strchr(base+8,'/')-base), path=strlen(location);
        if (origin >= capacity || path >= capacity-origin) return false;
        memmove(out,base,origin); memcpy(out+origin,location,path+1);
        return hisu_music_valid_url(out);
    } else return false;
    return n>=0 && (size_t)n<capacity && hisu_music_valid_url(out);
}

esp_err_t hisu_music_play(const hisu_music_track_t *track, hisu_music_pcm_fn output)
{
    if (!track || !output || !hisu_music_valid_url(track->url)) return ESP_ERR_INVALID_ARG;
    if (!hisu_music_current(track->ticket)) return ESP_ERR_INVALID_STATE;
    static bool registered;
    if (!registered) {
        if (esp_mp3_dec_register() != ESP_AUDIO_ERR_OK) return ESP_FAIL;
        registered = true;
    }
    esp_audio_simple_dec_cfg_t config = {.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_MP3};
    esp_audio_simple_dec_handle_t decoder = NULL;
    esp_http_client_handle_t http = NULL;
    uint8_t *encoded = heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *pcm = heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    char *current_url=heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    stream_headers_t headers={.location=(char *)encoded};
    esp_err_t err = ESP_FAIL;
    const char *stage="allocation";
    esp_audio_err_t codec_ret=ESP_AUDIO_ERR_OK;
    size_t pcm_capacity=8192, received=0, decoded=0;
    bool first_decode=true;
    music_id3_t id3={0};
    int http_status=0;
    ESP_LOGI("MUSIC","Playback start: initializing MP3 stream");
    if (!encoded || !pcm || !current_url) { err=ESP_ERR_NO_MEM; goto done; }
    memcpy(current_url,track->url,strlen(track->url)+1);
    stage="decoder open";
    codec_ret=esp_audio_simple_dec_open(&config,&decoder);
    if (codec_ret != ESP_AUDIO_ERR_OK) goto done;
    for (unsigned redirects=0;;) {
        if (!hisu_music_current(track->ticket)) { err=ESP_ERR_INVALID_STATE; goto done; }
        headers.seen=headers.invalid=false; headers.location[0]=0;
        http=http_new(current_url,2000,stream_event,&headers);
        if (!http) { err=ESP_ERR_NO_MEM; goto done; }
        stage="CDN connect";
        ESP_LOGI("MUSIC","CDN connect start");
        err=esp_http_client_open(http,0);
        if (err != ESP_OK) goto done;
        err=ESP_FAIL;
        stage="CDN headers";
        ESP_LOGI("MUSIC","CDN connected; waiting for headers");
        int64_t content_length=esp_http_client_fetch_headers(http);
        http_status=esp_http_client_get_status_code(http);
        ESP_LOGI("MUSIC","CDN headers: HTTP=%d length=%lld",http_status,(long long)content_length);
        if (content_length < 0) goto done;
        if (http_status==200) break;
        bool redirect=http_status==301 || http_status==302 || http_status==303 || http_status==307 || http_status==308;
        stage="CDN redirect";
        if (!redirect || redirects>=3 || !headers.seen || headers.invalid ||
            !redirect_url(current_url,headers.location,current_url,4096)) {
            ESP_LOGW("MUSIC","CDN redirect rejected: status=%d hops=%u missing=%d invalid=%d",
                http_status,redirects,!headers.seen,headers.invalid);
            goto done;
        }
        /* Drop the old socket before opening the validated HTTPS destination. */
        esp_http_client_close(http); esp_http_client_cleanup(http); http=NULL;
        ++redirects;
        ESP_LOGI("MUSIC","Following validated CDN redirect %u/3",redirects);
    }
    bool played=false;
    while (hisu_music_current(track->ticket)) {
        stage="CDN read";
        int n = esp_http_client_read(http,(char *)encoded,2048);
        if (n < 0) goto done;
        if (!n) {
            if (played && esp_http_client_is_complete_data_received(http)) err=ESP_OK;
            break;
        }
        if (!received) ESP_LOGI("MUSIC","CDN first data: %d bytes, prefix=%02x %02x %02x",n,
            encoded[0],n>1 ? encoded[1] : 0,n>2 ? encoded[2] : 0);
        received+=(size_t)n;
        size_t audio_bytes=(size_t)n;
        stage="ID3 metadata";
        if (!music_id3_feed(&id3,encoded,&audio_bytes,8192)) goto done;
        if (!audio_bytes) continue;
        if (first_decode && id3.discarded)
            ESP_LOGI("MUSIC","Skipped ID3 metadata: %u bytes in %u tags",(unsigned)id3.discarded,id3.tags);
        esp_audio_simple_dec_raw_t raw = {.buffer=encoded,.len=(uint32_t)audio_bytes,
            .eos=esp_http_client_is_complete_data_received(http)};
        unsigned iterations=0;
        while (raw.len && hisu_music_current(track->ticket)) {
            stage="decode";
            if (++iterations > 1024) goto done;
            raw.consumed=0;
            esp_audio_simple_dec_out_t frame = {.buffer=pcm,.len=(uint32_t)pcm_capacity};
            if (first_decode) {
                ESP_LOGI("MUSIC","MP3 decode input: %u bytes",(unsigned)raw.len);
                first_decode=false;
            }
            codec_ret = esp_audio_simple_dec_process(decoder,&raw,&frame);
            if (codec_ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                if (raw.consumed || frame.needed_size <= pcm_capacity || frame.needed_size > 32768) goto done;
                uint8_t *larger=heap_caps_malloc(frame.needed_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
                if (!larger) { err=ESP_ERR_NO_MEM; goto done; }
                free(pcm); pcm=larger; pcm_capacity=frame.needed_size;
                ESP_LOGI("MUSIC","MP3 PCM buffer expanded: %u bytes",(unsigned)pcm_capacity);
                continue;
            }
            if (codec_ret != ESP_AUDIO_ERR_OK || raw.consumed > raw.len || frame.decoded_size > pcm_capacity) goto done;
            if (frame.decoded_size) {
                stage="decoder format";
                esp_audio_simple_dec_info_t info;
                codec_ret=esp_audio_simple_dec_get_info(decoder,&info);
                if (codec_ret != ESP_AUDIO_ERR_OK || info.bits_per_sample != 16 ||
                    (info.channel != 1 && info.channel != 2) || info.sample_rate < 8000 || info.sample_rate > 48000 ||
                    frame.decoded_size % (2*info.channel)) goto done;
                if (!played) ESP_LOGI("MUSIC","MP3 decoded; opening output: %u Hz, %u channels, %u bytes",
                    (unsigned)info.sample_rate,(unsigned)info.channel,(unsigned)frame.decoded_size);
                stage="speaker output";
                if (!output((int16_t *)pcm,frame.decoded_size/2,info.sample_rate,info.channel)) goto done;
                if (!played) ESP_LOGI("MUSIC","MP3 playback: %u Hz, %u channels",(unsigned)info.sample_rate,(unsigned)info.channel);
                decoded+=frame.decoded_size;
                played=true;
            }
            if (!raw.consumed && !frame.decoded_size) goto done;
            raw.buffer += raw.consumed; raw.len -= raw.consumed;
        }
    }
done:
    if (err != ESP_OK) ESP_LOGW("MUSIC","Playback stopped: stage=%s err=%d codec=%d HTTP=%d input=%u PCM=%u cancelled=%d",
        stage,(int)err,(int)codec_ret,http_status,(unsigned)received,(unsigned)decoded,!hisu_music_current(track->ticket));
    if (http) { esp_http_client_close(http); esp_http_client_cleanup(http); }
    if (decoder) esp_audio_simple_dec_close(decoder);
    free(encoded); free(pcm); free(current_url);
    return err;
}
