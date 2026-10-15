#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include "music_mock.h"
#include "../components/hisu_voice/src/hisu_voice_music.c"

static bool pcm(const int16_t *data,size_t samples,int rate,int channels) {
    assert(samples==2 && rate==44100 && channels==2);
    ++mock_outputs;
    if (mock_cancel_output) hisu_music_cancel();
    return true;
}
static void put32(unsigned char *out,uint32_t n) {
    for (unsigned i=0;i<4;++i) out[i]=(unsigned char)(n>>(8*i));
}
static void gzip_fixture(void) {
    const char text[]="{\"err\":0,\"data\":{\"items\":[{\"encodeId\":\"song1\",\"title\":\"Test\",\"artistsNames\":\"Artist\"}]}}";
    size_t n=strlen(text);
    memset(mock_gzip,0,sizeof(mock_gzip));
    mock_gzip[0]=0x1f; mock_gzip[1]=0x8b; mock_gzip[2]=8;
    mock_gzip[10]=1; mock_gzip[11]=(unsigned char)n; mock_gzip[12]=(unsigned char)(n>>8);
    mock_gzip[13]=(unsigned char)~n; mock_gzip[14]=(unsigned char)(~n>>8);
    memcpy(mock_gzip+15,text,n);
    put32(mock_gzip+15+n,mz_crc32(0,(const unsigned char *)text,n));
    put32(mock_gzip+19+n,(uint32_t)n);
    mock_gzip_size=n+23;
}
static void gzip_tests(void) {
    assert(mz_crc32(0,(const unsigned char *)"123456789",9)==0xcbf43926U);
    for (int mode=18;mode<=19;++mode) {
        gzip_fixture(); mock_mode=mode;
        cJSON *songs=cJSON_CreateArray();
        assert(hisu_music_search("test",songs)==ESP_OK && cJSON_GetArraySize(songs)==1);
        cJSON_Delete(songs); assert(!mock_http_live);
    }
    gzip_fixture();
    memmove(mock_gzip+20,mock_gzip+10,mock_gzip_size-10);
    mock_gzip_size+=10; mock_gzip[3]=0x1e;
    const unsigned char optional[]={2,0,1,2,'x',0,'y',0};
    memcpy(mock_gzip+10,optional,sizeof(optional));
    uint32_t header_crc=mz_crc32(0,mock_gzip,18);
    mock_gzip[18]=(unsigned char)header_crc; mock_gzip[19]=(unsigned char)(header_crc>>8);
    cJSON *optional_songs=cJSON_CreateArray();
    assert(hisu_music_search("test",optional_songs)==ESP_OK && cJSON_GetArraySize(optional_songs)==1);
    cJSON_Delete(optional_songs); assert(!mock_http_live);
    for (int bad=0;bad<9;++bad) {
        gzip_fixture();
        if (bad==0) mock_gzip[3]=0xe0;
        if (bad==1) mock_gzip[mock_gzip_size-8]^=1; /* CRC */
        if (bad==2) put32(mock_gzip+mock_gzip_size-4,65537); /* Size ceiling */
        if (bad==3) --mock_gzip_size; /* Truncation */
        if (bad==4) mock_gzip[10]=7; /* Invalid DEFLATE */
        if (bad==5) mock_gzip[3]=4; /* Bad extra field length */
        if (bad==6) mock_gzip[3]=8; /* No valid stream after filename */
        if (bad==7) mock_gzip[3]=2; /* Bad header CRC */
        if (bad==8) put32(mock_gzip+mock_gzip_size-4,1); /* Size mismatch */
        cJSON *songs=cJSON_CreateArray();
        assert(hisu_music_search("test",songs)!=ESP_OK && !mock_http_live);
        cJSON_Delete(songs);
    }
    for (int alloc=2;alloc<4;++alloc) {
        gzip_fixture(); mock_music_alloc_left=alloc;
        cJSON *songs=cJSON_CreateArray();
        assert(hisu_music_search("test",songs)!=ESP_OK && !mock_http_live);
        cJSON_Delete(songs);
    }
    mock_music_alloc_left=-1; mock_mode=0;
}
int main(void) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
#endif
    gzip_tests();
    hisu_music_track_t queued={0};
    unsigned created=mock_http_created;
    mock_music_alloc_left=0;
    assert(hisu_music_queue("song1",&queued)==ESP_OK && !strcmp(queued.id,"song1"));
    assert(mock_http_created==created && !queued.url[0] && !queued.title[0]);
    hisu_music_cancel(); mock_music_alloc_left=-1;
    assert(hisu_music_resolve(&queued)==ESP_ERR_INVALID_STATE && mock_http_created==created);
    assert(hisu_music_queue("song1",&queued)==ESP_OK);
    uint32_t ticket=queued.ticket;
    assert(hisu_music_resolve(&queued)==ESP_OK && queued.ticket==ticket && hisu_music_current(ticket));
    assert(mock_http_live==0 && !strcmp(queued.title,"Test"));
    hisu_music_finish(ticket);
    assert(hisu_music_queue("song1",&queued)==ESP_OK);
    mock_mode=1;
    assert(hisu_music_resolve(&queued)!=ESP_OK && hisu_music_current(queued.ticket) && !mock_http_live);
    hisu_music_finish(queued.ticket); mock_mode=0;
    cJSON *songs=cJSON_CreateArray();
    assert(hisu_music_search(NULL,songs)==ESP_ERR_INVALID_ARG);
    assert(hisu_music_search("",songs)==ESP_ERR_INVALID_ARG && mock_http_live==0);
    assert(hisu_music_search("Lac Troi Son Tung",songs)==ESP_OK && cJSON_GetArraySize(songs)==1);
    assert(!strncmp(mock_canonical,"count=5ctime=",13));
    assert(strstr(mock_canonical,"page=1type=songversion=1.6.34"));
    assert(!strncmp(mock_signed,"/api/v2/search" "abab",strlen("/api/v2/search" "abab")));
    assert(strstr(mock_api_url,"&q=Lac%20Troi%20Son%20Tung&type=song&page=1&count=5"));
    assert(mock_accept_json && mock_identity);
    cJSON_Delete(songs); assert(mock_http_live==0);
    for (int mode=8;mode<=17;++mode) {
        mock_mode=mode;
        songs=cJSON_CreateArray();
        assert((hisu_music_search("test",songs)==ESP_OK)==(mode==10));
        assert(cJSON_GetArraySize(songs)==0 && mock_http_live==0);
        cJSON_Delete(songs);
    }
    mock_mode=0;
    hisu_music_track_t track={0};
    for (int i=0;i<2;++i) {
        mock_music_alloc_left=i;
        songs=cJSON_CreateArray();
        assert(hisu_music_search("test",songs)==ESP_ERR_NO_MEM && mock_http_live==0);
        cJSON_Delete(songs);
        mock_music_alloc_left=i;
        assert(hisu_music_prepare("song1",&track)==ESP_ERR_NO_MEM && !s_busy && mock_http_live==0);
    }
    mock_music_alloc_left=-1;
    assert(hisu_music_prepare("song1",&track)==ESP_OK);
    assert(s_busy && hisu_music_current(track.ticket) && !strcmp(track.title,"Test"));
    assert(strstr(mock_canonical,"id=song1version=1.6.34"));
    assert(hisu_music_play(&track,pcm)==ESP_OK && mock_outputs==2);
    assert(mock_http_live==0 && mock_decoder_live==0);
    assert(mock_stream_read_size==2048);
    hisu_music_finish(track.ticket); assert(!s_busy);
    for (int mode=20;mode<=27;++mode) {
        mock_mode=0; assert(hisu_music_prepare("song1",&track)==ESP_OK);
        mock_mode=mode;
        assert((hisu_music_play(&track,pcm)==ESP_OK)==(mode==20));
        assert(mock_http_live==0 && mock_decoder_live==0);
        hisu_music_finish(track.ticket);
    }
    mock_mode=0; assert(hisu_music_prepare("song1",&track)==ESP_OK);
    mock_mode=20; mock_music_alloc_left=3;
    assert(hisu_music_play(&track,pcm)==ESP_ERR_NO_MEM && !mock_http_live && !mock_decoder_live);
    mock_music_alloc_left=-1; hisu_music_finish(track.ticket);
    const int redirect_statuses[]={301,302,303,307,308};
    for (unsigned i=0;i<sizeof(redirect_statuses)/sizeof(redirect_statuses[0]);++i) {
        mock_redirect_status=redirect_statuses[i];
        for (int mode=30;mode<=40;++mode) {
            mock_mode=0; assert(hisu_music_prepare("song1",&track)==ESP_OK);
            mock_mode=mode; mock_redirect_headers=0;
            unsigned before=mock_http_created;
            bool success=mode==30 || mode==31 || mode==35 || mode==39;
            assert((hisu_music_play(&track,pcm)==ESP_OK)==success);
            assert(!mock_http_live && !mock_decoder_live && mock_http_peak==1);
            unsigned connections=mode==31 || mode==32 ? 4 : success ? 2 : 1;
            assert(mock_http_created-before==connections);
            hisu_music_finish(track.ticket);
        }
    }
    char resolved[4096];
    assert(redirect_url("https://a.zmdcdn.me/base","/next?key=value",resolved,sizeof(resolved)));
    assert(!strcmp(resolved,"https://a.zmdcdn.me/next?key=value"));
    const char *blocked[]={"https://a.zmdcdn.me.evil.example/x","https://user@a.zmdcdn.me/x",
        "https://a.zmdcdn.me:443/x","https://a.zmdcdn.me/x#frag","//evil.example/x","/x\r\nInjected: true","relative.mp3"};
    for (unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);++i)
        assert(!redirect_url("https://a.zmdcdn.me/base",blocked[i],resolved,sizeof(resolved)));
    assert(!redirect_url("https://a.zmdcdn.me/base","/next",resolved,8));
    mock_mode=0; mock_redirect_status=302;
    for (int mode=1;mode<=6;++mode) {
        mock_mode=mode;
        if (mode==1 || mode==2 || mode==5 || mode==6) {
            songs=cJSON_CreateArray();
            assert(hisu_music_search("test",songs)!=ESP_OK); cJSON_Delete(songs);
            assert(hisu_music_prepare("song1",&track)!=ESP_OK && !s_busy);
        } else {
            mock_mode=0; assert(hisu_music_prepare("song1",&track)==ESP_OK); mock_mode=mode;
            assert(hisu_music_play(&track,pcm)!=ESP_OK);
        }
        assert(mock_http_live==0 && mock_decoder_live==0);
    }
    mock_mode=0;
    assert(hisu_music_prepare("song1",&track)==ESP_OK);
    mock_cancel_output=true;
    assert(hisu_music_play(&track,pcm)!=ESP_OK);
    assert(!hisu_music_current(track.ticket) && mock_http_live==0 && mock_decoder_live==0);
    assert(hisu_music_play(&track,pcm)==ESP_ERR_INVALID_STATE);
    assert(hisu_music_prepare("../unsafe",&track)==ESP_ERR_INVALID_ARG);
    mock_cancel_home=true; mock_mode=0;
    assert(hisu_music_prepare("song1",&track)==ESP_ERR_INVALID_STATE && !s_busy && mock_http_live==0);
    mock_cancel_home=false;
    mock_cancel_output=false; mock_mode=0;
    assert(hisu_music_prepare("song1",&track)==ESP_OK);
    mock_mode=8;
    assert(hisu_music_play(&track,pcm)!=ESP_OK); /* A truncated HTTP body is not success. */
    assert(mock_http_live==0 && mock_decoder_live==0);
    puts("Music worker tests passed: API parsing/signature inputs, restricted/error responses, cleanup, decoding progress, cancellation and truncated HTTP. HTTP/crypto/MP3/RTOS mocked.");
    return 0;
}
