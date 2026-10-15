#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
enum {ESP_OK,ESP_FAIL,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_NO_MEM};
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
enum {MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_INTERNAL=4};
static size_t heap_caps_get_free_size(int caps) { return 1024*1024; }
static size_t heap_caps_get_largest_free_block(int caps) { return 256*1024; }
static int mock_music_alloc_left=-1;
static bool mock_music_allocate(void) {
    if (mock_music_alloc_left==0) return false;
    if (mock_music_alloc_left>0) --mock_music_alloc_left;
    return true;
}
static void *heap_caps_malloc(size_t n,int caps) { return mock_music_allocate() ? malloc(n) : NULL; }
static void *heap_caps_calloc(size_t n,size_t size,int caps) { return mock_music_allocate() ? calloc(n,size) : NULL; }
typedef struct {
    int event_id, data_len;
    void *user_data, *data;
    char *header_key, *header_value;
} esp_http_client_event_t;
enum {HTTP_EVENT_ON_HEADER,HTTP_EVENT_ON_DATA};
typedef struct {
    const char *url,*user_agent;
    int timeout_ms,buffer_size,buffer_size_tx;
    bool disable_auto_redirect;
    void *user_data;
    esp_err_t (*event_handler)(esp_http_client_event_t *);
    esp_err_t (*crt_bundle_attach)(void *);
} esp_http_client_config_t;
typedef struct { esp_http_client_config_t config; char url[4096]; int reads; } *esp_http_client_handle_t;
static int mock_mode, mock_http_live, mock_decoder_live, mock_outputs;
static unsigned mock_http_created;
static unsigned mock_redirect_headers;
static int mock_redirect_status=302, mock_http_peak;
static int mock_stream_read_size;
static bool mock_cancel_output;
static bool mock_cancel_home;
static bool mock_accept_json, mock_identity;
static unsigned char mock_gzip[512];
static size_t mock_gzip_size;
bool hisu_music_cancel(void);
static char mock_canonical[160], mock_signed[192], mock_api_url[1024];
static esp_err_t esp_crt_bundle_attach(void *arg) { return ESP_OK; }
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    esp_http_client_handle_t http=calloc(1,sizeof(*http));
    if (http) {
        http->config=*config; memcpy(http->url,config->url,strlen(config->url)+1);
        ++mock_http_live; ++mock_http_created;
        if (mock_http_live>mock_http_peak) mock_http_peak=mock_http_live;
    }
    return http;
}
static esp_err_t esp_http_client_open(esp_http_client_handle_t http,int length) { return ESP_OK; }
static int64_t esp_http_client_fetch_headers(esp_http_client_handle_t http) {
    if (mock_mode>=30 && http->config.event_handler && strstr(http->url,".zmdcdn.")) {
        ++mock_redirect_headers;
        char *location="https://b.zmdcdn.me/next.mp3";
        if (mock_mode==33) location="https://evil.example/next.mp3";
        if (mock_mode==34) location="http://b.zmdcdn.me/next.mp3";
        if (mock_mode==35) location="/next.mp3";
        if (mock_mode==39) location="//b.zmdcdn.me/next.mp3";
        static char oversized[4097];
        if (mock_mode==36) { memset(oversized,'x',4096); oversized[4096]=0; location=oversized; }
        esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_HEADER,.user_data=http->config.user_data,
            .header_key="Location",.header_value=location};
        if (mock_mode!=37) http->config.event_handler(&event);
        if (mock_mode==40) http->config.event_handler(&event);
        if (mock_mode==38) hisu_music_cancel();
    }
    if (http->config.event_handler && !strcmp(http->url,"https://zingmp3.vn/")) {
        esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_HEADER,.user_data=http->config.user_data,
            .header_key="Set-Cookie",.header_value="test=value; Secure; HttpOnly"};
        http->config.event_handler(&event);
        if (mock_cancel_home) hisu_music_cancel();
    }
    return 128;
}
static esp_err_t esp_http_client_set_header(esp_http_client_handle_t http,const char *key,const char *value) {
    if (!strcmp(key,"Accept")) mock_accept_json=!strcmp(value,"application/json");
    if (!strcmp(key,"Accept-Encoding")) mock_identity=!strcmp(value,"identity");
    return ESP_OK;
}
static esp_err_t esp_http_client_perform(esp_http_client_handle_t http) {
    memcpy(mock_api_url,http->url,strlen(http->url)+1);
    const char *body="{\"err\":0,\"data\":{\"items\":[{\"encodeId\":\"song1\",\"title\":\"Test\",\"artistsNames\":\"Artist\"}]}}";
    if (strstr(http->url,"/song/get/info")) body="{\"err\":0,\"data\":{\"title\":\"Test\",\"artistsNames\":\"Artist\"}}";
    if (strstr(http->url,"/song/get/streaming")) body="{\"err\":0,\"data\":{\"128\":\"https://a.zmdcdn.me/test.mp3\"}}";
    if (mock_mode==1) body="{\"err\":-201,\"msg\":\"Restricted\"}";
    if (mock_mode==6) body="not json";
    if (mock_mode==9) body="{\"err\":0}";
    if (mock_mode==10) body="{\"err\":0,\"data\":{\"items\":[]}}";
    if (mock_mode==13) body="{\"err\":-104,\"msg\":\"Incorect signature\"}";
    if (mock_mode==14) body="<!doctype html><html>Unavailable</html>";
    if (mock_mode==15) body="\x1f\x8b" "compressed";
    if (mock_mode==16 || mock_mode==17 || mock_mode==18 || mock_mode==19) {
        esp_http_client_event_t header={.event_id=HTTP_EVENT_ON_HEADER,.user_data=http->config.user_data,
            .header_key=mock_mode==16 ? "Content-Type" : "Content-Encoding",
            .header_value=mock_mode==16 ? "text/html; charset=utf-8" : "gzip"};
        http->config.event_handler(&header);
    }
    if (mock_mode==18 || mock_mode==19) body=(const char *)mock_gzip;
    esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.user_data=http->config.user_data,
        .data=(void *)body,.data_len=mock_mode==5 ? 65537 :
            (mock_mode==18 || mock_mode==19) ? (int)mock_gzip_size : (int)strlen(body)};
    if (mock_mode==19) {
        int total=event.data_len;
        event.data_len=7;
        if (http->config.event_handler(&event)!=ESP_OK) return ESP_FAIL;
        event.data=(void *)(body+7); event.data_len=total-7;
    }
    return http->config.event_handler(&event);
}
static int esp_http_client_get_status_code(esp_http_client_handle_t http) {
    bool home=!strcmp(http->url,"https://zingmp3.vn/");
    if (mock_mode==12 && home) return 302;
    if (mock_mode==26) return 302;
    if (mock_mode>=30 && strstr(http->url,".zmdcdn.")) {
        if (mock_mode==31) return mock_redirect_headers<=3 ? mock_redirect_status : 200;
        if (mock_mode==32) return mock_redirect_status;
        return mock_redirect_headers==1 ? mock_redirect_status : 200;
    }
    return mock_mode==2 || (mock_mode==11 && !home) ? 404 : 200;
}
static bool esp_http_client_is_complete_data_received(esp_http_client_handle_t http) { return mock_mode!=8; }
static int esp_http_client_read(esp_http_client_handle_t http,char *data,int capacity) {
    mock_stream_read_size=capacity;
    if (mock_mode==3) return -1;
    if (http->reads++) return 0;
    memset(data,1,128); return 128;
}
static esp_err_t esp_http_client_close(esp_http_client_handle_t http) { return ESP_OK; }
static esp_err_t esp_http_client_cleanup(esp_http_client_handle_t http) { free(http); --mock_http_live; return ESP_OK; }
enum {MBEDTLS_MD_SHA256,MBEDTLS_MD_SHA512};
static const void *mbedtls_md_info_from_type(int type) { return (void *)(uintptr_t)(type+1); }
static int mbedtls_md(const void *info,const unsigned char *data,size_t n,unsigned char *out) {
    memcpy(mock_canonical,data,n); mock_canonical[n]=0; memset(out,0xAB,32); return 0;
}
static int mbedtls_md_hmac(const void *info,const unsigned char *key,size_t key_size,
                         const unsigned char *data,size_t n,unsigned char *out) {
    memcpy(mock_signed,data,n); mock_signed[n]=0; memset(out,0xCD,64); return 0;
}

/* ROM boundary mock: only stored DEFLATE blocks used by the gzip wrapper fixtures. */
typedef struct { unsigned state; } tinfl_decompressor;
typedef int tinfl_status;
enum { TINFL_STATUS_DONE=0, TINFL_STATUS_FAILED=-1, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF=4 };
#define tinfl_init(state) ((state)->state=0)
static tinfl_status tinfl_decompress(tinfl_decompressor *state,const unsigned char *in,size_t *input,
    unsigned char *start,unsigned char *out,size_t *output,unsigned flags) {
    if (*input<5 || in[0]!=1) return TINFL_STATUS_FAILED;
    size_t n=in[1] | (size_t)in[2]<<8;
    unsigned complement=in[3] | (unsigned)in[4]<<8;
    if ((n^complement)!=65535 || n>*input-5 || n>*output) return TINFL_STATUS_FAILED;
    memcpy(out,in+5,n); *input=n+5; *output=n; return TINFL_STATUS_DONE;
}
static uint32_t mz_crc32(uint32_t crc,const unsigned char *data,size_t n) {
    crc=~crc;
    for (size_t i=0;i<n;++i) {
        crc^=data[i];
        for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320U & (0U-(crc&1)));
    }
    return ~crc;
}
enum {ESP_AUDIO_ERR_OK,ESP_AUDIO_ERR_FAIL,ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,ESP_AUDIO_ERR_BUFF_NOT_ENOUGH};
typedef int esp_audio_err_t;
typedef void *esp_audio_simple_dec_handle_t;
typedef struct { int dec_type; bool use_frame_dec; } esp_audio_simple_dec_cfg_t;
typedef struct { uint8_t *buffer; uint32_t len,consumed; bool eos; } esp_audio_simple_dec_raw_t;
typedef struct { uint8_t *buffer; uint32_t len,decoded_size,needed_size; } esp_audio_simple_dec_out_t;
typedef struct { int bits_per_sample,channel,sample_rate; } esp_audio_simple_dec_info_t;
static int esp_mp3_dec_register(void) { return ESP_AUDIO_ERR_OK; }
static int esp_audio_simple_dec_open(esp_audio_simple_dec_cfg_t *config,void **decoder) {
    *decoder=(void *)1; ++mock_decoder_live; return ESP_AUDIO_ERR_OK;
}
static int esp_audio_simple_dec_process(void *decoder,esp_audio_simple_dec_raw_t *raw,esp_audio_simple_dec_out_t *frame) {
    if ((mock_mode==20 && frame->len<16384) || mock_mode==21 || mock_mode==22 || mock_mode==23) {
        raw->consumed=mock_mode==23 ? 1 : 0;
        frame->needed_size=mock_mode==21 ? 65536 : mock_mode==22 ? frame->len : 16384;
        return ESP_AUDIO_ERR_BUFF_NOT_ENOUGH;
    }
    if (mock_mode==27) { raw->consumed=raw->len; frame->decoded_size=0; return ESP_AUDIO_ERR_OK; }
    if (mock_mode==4) { raw->consumed=frame->decoded_size=0; return ESP_AUDIO_ERR_OK; }
    raw->consumed=raw->len>64 ? 64 : raw->len;
    frame->decoded_size=4; memset(frame->buffer,0,4); return ESP_AUDIO_ERR_OK;
}
static int esp_audio_simple_dec_get_info(void *decoder,esp_audio_simple_dec_info_t *info) {
    if (mock_mode==24) return ESP_AUDIO_ERR_FAIL;
    *info=(esp_audio_simple_dec_info_t){.bits_per_sample=16,.channel=2,.sample_rate=44100};
    if (mock_mode==25) info->sample_rate=96000;
    return ESP_AUDIO_ERR_OK;
}
static void esp_audio_simple_dec_close(void *decoder) { --mock_decoder_live; }
