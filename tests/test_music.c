#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include "music_mock.h"
#include "../components/hisu_voice/src/hisu_voice_music.c"
#include "../components/hisu_voice/src/hisu_voice_music_lyrics.c"

static struct { void *pointer; size_t bytes; } provider_allocations[2048];
static void provider_record(void *pointer,size_t bytes) {
    if (!pointer) return;
    assert(mock_provider_live<2048);
    provider_allocations[mock_provider_live].pointer=pointer;
    provider_allocations[mock_provider_live++].bytes=bytes;
}
static void provider_remove(void *pointer) {
    if (!pointer) return;
    for (unsigned i=0;i<mock_provider_live;++i) if (provider_allocations[i].pointer==pointer) {
        provider_allocations[i]=provider_allocations[--mock_provider_live]; return;
    }
    assert(false); /* Cross-allocator tree ownership would be a bug. */
}
static void *provider_malloc(size_t bytes,int caps) {
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    void *pointer=heap_caps_malloc(bytes,caps); provider_record(pointer,bytes); return pointer;
}
static void *provider_realloc(void *pointer,size_t bytes,int caps) {
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if (!mock_music_allocate()) return NULL;
    provider_remove(pointer);
    void *out=realloc(pointer,bytes); assert(out); provider_record(out,bytes); return out;
}
static void provider_free(void *pointer) { provider_remove(pointer); free(pointer); }

static void provider_json_tests(void) {
    const char *wire="{\"code\":0,\"data\":{\"name\":\"Test\",\"artistName\":\"Artist\"}}";
    for (int budget=0;budget<60;++budget) {
        mock_music_alloc_left=budget;
        cJSON *tree=voice_provider_json_parse(wire,strlen(wire),NULL);
        if (tree) assert(mock_provider_live>0 && cJSON_IsObject(tree));
        voice_provider_json_delete(tree); assert(!mock_provider_live);
    }
    mock_music_alloc_left=-1;
    for (int budget=0;budget<90;++budget) {
        hisu_music_track_t track={0};
        mock_music_alloc_left=budget;
        assert(hisu_music_queue("song1",&track)==ESP_OK);
        (void)hisu_music_resolve(&track);
        assert(!mock_http_live && !mock_provider_live);
        hisu_music_finish(track.ticket);
    }
    mock_music_alloc_left=-1;
    cJSON *normal=cJSON_Parse(wire);
    cJSON *external=voice_provider_json_parse(wire,strlen(wire),NULL);
    cJSON_Delete(normal); voice_provider_json_delete(external); assert(!mock_provider_live);
}

static bool pcm(const int16_t *data,size_t samples,int rate,int channels) {
    assert(samples==2 && rate==44100 && channels==2);
    ++mock_outputs;
    if (mock_cancel_output) hisu_music_cancel();
    return true;
}
static void put32(unsigned char *out,uint32_t n) {
    for (unsigned i=0;i<4;++i) out[i]=(unsigned char)(n>>(8*i));
}
static void gzip_fixture_body(const char *text) {
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
static void gzip_fixture(void) {
    gzip_fixture_body("{\"code\":0,\"data\":{\"songs\":[{\"key\":\"song1\",\"name\":\"Test\",\"artistName\":\"Artist\"}]}}");
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

static void id3_tests(void) {
    const uint8_t audio[]={0xff,0xfb,0x90,0,1,2,3,4,5,6,7,8,9,10,11,12};
    uint8_t chunk[2058];
    for (unsigned split=1;split<=10;++split) {
        music_id3_t filter={0};
        memcpy(chunk,audio,split); size_t n=split;
        assert(music_id3_feed(&filter,chunk,&n,sizeof(chunk)));
        assert(n==(split==10 ? 10 : 0));
        memcpy(chunk,audio+split,sizeof(audio)-split); n=sizeof(audio)-split;
        assert(music_id3_feed(&filter,chunk,&n,sizeof(chunk)));
        if (split<10) assert(n==sizeof(audio) && !memcmp(chunk,audio,n));
        else assert(n==sizeof(audio)-split);
    }
    music_id3_t filter={0};
    const uint32_t artwork=241770;
    uint8_t header[]={ 'I','D','3',3,0,0,0,0,0,0 };
    for (unsigned i=0;i<4;++i) header[9-i]=(uint8_t)((artwork>>(i*7))&0x7f);
    for (unsigned i=0;i<10;++i) {
        chunk[0]=header[i]; size_t n=1;
        assert(music_id3_feed(&filter,chunk,&n,sizeof(chunk)) && !n);
    }
    for (uint32_t left=artwork;left;) {
        size_t n=left>2048 ? 2048 : left; left-=(uint32_t)n;
        memset(chunk,0x55,n);
        assert(music_id3_feed(&filter,chunk,&n,sizeof(chunk)) && !n);
    }
    memcpy(chunk,audio,sizeof(audio)); size_t n=sizeof(audio);
    assert(music_id3_feed(&filter,chunk,&n,sizeof(chunk)) && n==sizeof(audio) && !memcmp(chunk,audio,n));
    assert(filter.discarded==artwork+10 && filter.tags==1 && filter.audio);
    for (unsigned bad=0;bad<5;++bad) {
        filter=(music_id3_t){0}; memcpy(chunk,header,10);
        if (bad==0) chunk[3]=5;
        if (bad==1) chunk[6]=0x80;
        if (bad==2) chunk[6]=0x7f;
        if (bad==3) chunk[5]=1;
        if (bad==4) filter.discarded=2*1024*1024;
        n=10; assert(!music_id3_feed(&filter,chunk,&n,sizeof(chunk)));
    }
    filter=(music_id3_t){0}; memcpy(chunk,"ID3\004\000\020\000\000\000\000",10);
    memset(chunk+10,0,10); memcpy(chunk+20,audio,sizeof(audio)); n=20+sizeof(audio);
    assert(music_id3_feed(&filter,chunk,&n,sizeof(chunk)) && n==sizeof(audio) && filter.discarded==20);
}

static void nct_protocol_tests(void) {
    const char *cases[]={
        "{\"typeUI\":\"128kbps\",\"stream\":\"https://stream.nct.vn/x.mp3\",\"onlyVIP\":true}",
        "{\"typeUI\":\"320kbps\",\"stream\":\"https://stream.nct.vn/x.mp3\",\"onlyVIP\":false}",
        "{\"typeUI\":\"128kbps\",\"stream\":\"https://evil.example/x.mp3\",\"onlyVIP\":false}",
        "{\"typeUI\":\"128kbps\",\"stream\":\"https://stream.nct.vn/x.mp3\"}",
        "{\"typeUI\":\"128kbps\",\"stream\":\"https://stream.nct.vn/x.mp3\",\"onlyVIP\":\"false\"}",
        "{\"typeUI\":\"128kbps\",\"stream\":\"https://stream.nct.vn/x.mp3\",\"onlyVIP\":false}",
    };
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        char wire[512];
        snprintf(wire,sizeof(wire),"{\"code\":0,\"data\":{\"key\":\"song1\",\"name\":\"Test\",\"artistName\":\"Artist\",\"streamURL\":[%s]}}",cases[i]);
        cJSON *root=cJSON_Parse(wire); hisu_music_track_t track={.id="song1"};
        assert(hisu_music_parse_track(root,&track)==(i==5));
        if (i==5) { strcpy(track.id,"other"); assert(!hisu_music_parse_track(root,&track)); }
        cJSON_Delete(root);
    }
    char long_artist[256]; memset(long_artist,'a',sizeof(long_artist));
    memcpy(long_artist+126,"\xe1\xba\xa1",3); long_artist[255]=0;
    cJSON *root=cJSON_CreateObject(), *data=cJSON_AddObjectToObject(root,"data"), *songs=cJSON_AddArrayToObject(data,"songs");
    cJSON_AddNumberToObject(root,"code",0);
    cJSON *song=cJSON_CreateObject(); cJSON_AddStringToObject(song,"key","song1");
    cJSON_AddStringToObject(song,"name","Test"); cJSON_AddStringToObject(song,"artistName",long_artist);
    cJSON_AddItemToArray(songs,song); cJSON *result=cJSON_CreateArray();
    assert(hisu_music_parse_search(root,result) && cJSON_GetArraySize(result)==1);
    const char *artist=cJSON_GetObjectItem(cJSON_GetArrayItem(result,0),"artist")->valuestring;
    assert(strlen(artist)==126); /* Truncation must not split Vietnamese UTF-8. */
    cJSON_Delete(root); cJSON_Delete(result);
}
static void lyric_tests(void) {
    char known[]="bbf316e8d940af0ad3"; size_t bytes=strlen(known);
    assert(hisu_music_lyrics_decode(known,&bytes,"Key") && bytes==9 && !strcmp(known,"Plaintext"));
    char odd[]="abc"; bytes=3; assert(!hisu_music_lyrics_decode(odd,&bytes,"Key"));
    char invalid[]="zz"; bytes=2; assert(!hisu_music_lyrics_decode(invalid,&bytes,"Key"));
    const char lrc[]="\xef\xbb\xbf[ar:Test]\r\n[00:02.50][00:04] Hai\r\n[00:01.000]Mot\n[00:03]\n[offset:+500]\n";
    hisu_music_lyrics_t *lyric=hisu_music_lyrics_parse(lrc,strlen(lrc));
    assert(lyric && lyric->count==4);
    assert(hisu_music_lyrics_index(lyric,499)==-1);
    assert(!strcmp(lyric->text+lyric->lines[hisu_music_lyrics_index(lyric,500)].offset,"Mot"));
    assert(!strcmp(lyric->text+lyric->lines[hisu_music_lyrics_index(lyric,2000)].offset,"Hai"));
    assert(!lyric->text[lyric->lines[hisu_music_lyrics_index(lyric,2500)].offset]);
    assert(hisu_music_lyrics_index(lyric,999999)==3);
    hisu_music_lyrics_free(lyric);
    const char *ties="[00:03]Later\n[00:01]Old\n[00:01]New\n[offset:-500]";
    lyric=hisu_music_lyrics_parse(ties,strlen(ties));
    assert(lyric && hisu_music_lyrics_index(lyric,1499)==-1);
    assert(!strcmp(lyric->text+lyric->lines[hisu_music_lyrics_index(lyric,1500)].offset,"New"));
    hisu_music_lyrics_free(lyric);
    const char *bad[]={"plain lyrics","[01:99]Bad","[00:00]\xc0\x80","[offset:70000]\n[00:00]Bad"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) assert(!hisu_music_lyrics_parse(bad[i],strlen(bad[i])));
    char long_line[1024]; strcpy(long_line,"[00:01]");
    for (unsigned i=7;i<1006;i+=3) memcpy(long_line+i,"\xe1\xba\xa1",3);
    long_line[1006]=0;
    lyric=hisu_music_lyrics_parse(long_line,strlen(long_line));
    assert(lyric && strlen(lyric->text)==255); hisu_music_lyrics_free(lyric);
    char many[4096]="";
    for (unsigned i=0;i<257;++i) strcat(many,"[00:00]X\n");
    assert(!hisu_music_lyrics_parse(many,strlen(many)));
    for (int budget=0;budget<4;++budget) {
        mock_music_alloc_left=budget;
        lyric=hisu_music_lyrics_parse(lrc,strlen(lrc));
        assert((lyric!=NULL)==(budget==3)); hisu_music_lyrics_free(lyric);
    }
    mock_music_alloc_left=-1;
    const char original[]="[00:01]Hello\n[00:02]Good night\n";
    unsigned char cipher[sizeof(original)]; memcpy(cipher,original,sizeof(original)-1);
    rc4_skip((const uint8_t *)"Key",3,0,cipher,sizeof(original)-1);
    char hex[2*sizeof(original)+1];
    for (size_t i=0;i<sizeof(original)-1;++i) sprintf(hex+2*i,"%02x",cipher[i]);
    mock_lyric_metadata="{\"code\":0,\"data\":{\"timedLyric\":\"https://lyric.nct.vn/test\",\"keyDecryptLyric\":\"Key\"}}";
    mock_lyric_file=hex;
    hisu_music_track_t track={0}; mock_mode=0;
    assert(hisu_music_queue("song1",&track)==ESP_OK);
    lyric=hisu_music_load_lyrics(&track);
    assert(lyric && lyric->count==2 && !mock_http_live && !mock_provider_live);
    assert(!strcmp(mock_api_url,"https://lyric.nct.vn/test")); hisu_music_lyrics_free(lyric);
    /* A gzip API body can be far smaller than the following timed file. */
    char big_lrc[4096]="";
    for (unsigned i=0;i<100;++i) strcat(big_lrc,"[00:01]Original test lyric\n");
    unsigned char big_cipher[4096]; size_t big_bytes=strlen(big_lrc);
    memcpy(big_cipher,big_lrc,big_bytes); rc4_skip((const uint8_t *)"Key",3,0,big_cipher,big_bytes);
    char big_hex[8193];
    for (size_t i=0;i<big_bytes;++i) sprintf(big_hex+2*i,"%02x",big_cipher[i]);
    gzip_fixture_body(mock_lyric_metadata); mock_lyric_gzip=true; mock_lyric_file=big_hex;
    lyric=hisu_music_load_lyrics(&track);
    assert(lyric && lyric->count==100); hisu_music_lyrics_free(lyric);
    mock_lyric_gzip=false; mock_lyric_file=hex;
    mock_lyric_status=302; assert(!hisu_music_load_lyrics(&track)); mock_lyric_status=200;
    mock_mode=8; assert(!hisu_music_load_lyrics(&track)); mock_mode=0;
    mock_lyric_file="invalid hex"; assert(!hisu_music_load_lyrics(&track)); mock_lyric_file=hex;
    const char *blocked[]={
        "{\"code\":0,\"data\":{\"timedLyric\":null}}",
        "{\"code\":0,\"data\":{\"timedLyric\":\"https://evil.example/test\",\"keyDecryptLyric\":\"Key\"}}",
        "{\"code\":0,\"data\":{\"songKey\":\"other\",\"timedLyric\":\"https://lyric.nct.vn/test\",\"keyDecryptLyric\":\"Key\"}}"};
    const char *valid=mock_lyric_metadata;
    for (unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);++i) {
        mock_lyric_metadata=blocked[i]; unsigned before=mock_http_created;
        assert(!hisu_music_load_lyrics(&track) && mock_http_created==before+1);
    }
    mock_lyric_metadata=valid;
    for (int budget=0;budget<95;++budget) {
        mock_music_alloc_left=budget; lyric=hisu_music_load_lyrics(&track);
        hisu_music_lyrics_free(lyric); assert(!mock_http_live && !mock_provider_live);
    }
    mock_music_alloc_left=-1; mock_lyric_cancel=true;
    assert(!hisu_music_load_lyrics(&track) && !hisu_music_current(track.ticket));
    mock_lyric_cancel=false; mock_lyric_metadata=mock_lyric_file=NULL;
    puts("Timed lyric tests passed: SDK RC4 vector, UTF-8 LRC/timestamps/offsets/bounds, optional HTTP failures, cancellation and OOM.");
}

int main(void) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
#endif
    hisu_music_track_t repeated={0},original={0};
    assert(hisu_music_queue("song1",&original)==ESP_OK && hisu_music_busy());
    assert(hisu_music_repeat(original.ticket,"song1",&repeated)==ESP_OK);
    hisu_music_finish(original.ticket);
    assert(hisu_music_current(repeated.ticket) && repeated.ticket!=original.ticket);
    hisu_music_cancel();
    assert(hisu_music_repeat(repeated.ticket,"song1",&original)==ESP_ERR_INVALID_STATE && !hisu_music_busy());
    lyric_tests(); gzip_tests();
    id3_tests(); nct_protocol_tests();
    provider_json_tests();
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
    assert(!strcmp(mock_api_url,"https://graph.nct.vn/api/v1/search/song?keyword=Lac%20Troi%20Son%20Tung&pageindex=1&pagesize=5&correct=true"));
    assert(mock_accept_json && mock_identity && mock_nct_os);
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
    assert(!strcmp(mock_api_url,"https://graph.nct.vn/api/v1/song/detail/song1"));
    assert(hisu_music_play(&track,pcm)==ESP_OK && mock_outputs==2);
    assert(mock_http_live==0 && mock_decoder_live==0);
    assert(!mock_provider_live);
    assert(mock_stream_read_size==2048);
    hisu_music_finish(track.ticket); assert(!s_busy);
    assert(hisu_music_prepare("song1",&track)==ESP_OK);
    mock_stream_id3_size=241770; mock_decoder_input_bytes=0;
    assert(hisu_music_play(&track,pcm)==ESP_OK && mock_decoder_input_bytes==128);
    assert(!mock_http_live && !mock_decoder_live);
    mock_stream_id3_size=0; hisu_music_finish(track.ticket);
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
    assert(redirect_url("https://stream.nct.vn/base","/next?key=value",resolved,sizeof(resolved)));
    assert(!strcmp(resolved,"https://stream.nct.vn/next?key=value"));
    const char *blocked[]={"https://stream.nct.vn.evil.example/x","https://user@stream.nct.vn/x",
        "https://stream.nct.vn:443/x","https://stream.nct.vn/x#frag","//evil.example/x","/x\r\nInjected: true","relative.mp3"};
    for (unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);++i)
        assert(!redirect_url("https://stream.nct.vn/base",blocked[i],resolved,sizeof(resolved)));
    assert(!redirect_url("https://stream.nct.vn/base","/next",resolved,8));
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
    mock_cancel_request=true; mock_mode=0;
    assert(hisu_music_prepare("song1",&track)==ESP_ERR_INVALID_STATE && !s_busy && mock_http_live==0);
    mock_cancel_request=false;
    mock_cancel_output=false; mock_mode=0;
    assert(hisu_music_prepare("song1",&track)==ESP_OK);
    mock_mode=8;
    assert(hisu_music_play(&track,pcm)!=ESP_OK); /* A truncated HTTP body is not success. */
    assert(mock_http_live==0 && mock_decoder_live==0);
    puts("NCT music tests passed: anonymous API mapping, same-key/non-VIP streams, PSRAM/OOM cleanup, incremental ID3, redirects, decoding, cancellation and truncated HTTP. HTTP/MP3/RTOS mocked.");
    return 0;
}

/* Keep aliases after the tests: Live/result trees still use normal cJSON. */
#define heap_caps_malloc provider_malloc
#define heap_caps_realloc provider_realloc
#define free provider_free
#include "../components/hisu_voice/src/hisu_voice_lookup_json.c"
#undef free
#undef heap_caps_realloc
#undef heap_caps_malloc
