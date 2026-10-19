#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include "alert_mock.h"
#include "../components/hisu_alert/src/hisu_alert.c"
#include "../components/hisu_voice/src/hisu_voice_alert_tts.c"
static bool mock_connected=true;
void hisu_power_get(hisu_power_status_t *out) { *out=(hisu_power_status_t){.valid=true,.external_power=mock_charging,.percent=80}; }
void hisu_network_get(hisu_network_status_t *out) { *out=(hisu_network_status_t){.connected=mock_connected}; }
void hisu_network_get_link(hisu_network_link_t *out) { *out=(hisu_network_link_t){.connected=mock_connected}; }
esp_err_t hisu_settings_get_gemini_key(char out[128]) { strcpy(out,"fake-key"); return ESP_OK; }
esp_err_t hisu_settings_get_gemini_profile(hisu_gemini_key_t profile,char out[128]) {
    assert(profile==HISU_GEMINI_READER); strcpy(out,"fake-key"); return ESP_OK;
}
static bool idle(void) { return mock_idle; }
static void reboot(void) {
    s_lock=NULL; memset(s_records,0,sizeof(s_records)); memset(&s_status,0,sizeof(s_status));
    s_policy=(hisu_alert_policy_t){.last={-1,-1}};
    assert(hisu_alert_init()==ESP_OK);
}
static void reset(void) {
    memset(mock_flash,0xff,sizeof(mock_flash)); mock_flash_budget=-1; mock_alloc_budget=-1;
    mock_http_status=200; mock_mode=0; mock_charging=mock_idle=mock_connected=true;
    reboot();
}
int main(int argc, char **argv) {
    _set_error_mode(_OUT_TO_STDERR);
    hisu_alert_policy_t policy={.last={-1,-1}};
    assert(!hisu_alert_level(&policy,true,false,15,1000000));
    assert(hisu_alert_level(&policy,true,false,15,5000000)==1);
    policy.fired=1;
    assert(!hisu_alert_level(&policy,true,false,15,6000000));
    assert(!hisu_alert_level(&policy,true,false,5,7000000));
    assert(hisu_alert_level(&policy,true,false,5,11000000)==2);
    assert(!hisu_alert_level(&policy,true,false,2,12000000));
    policy.fired=3;
    assert(!hisu_alert_level(&policy,true,true,5,13000000));
    assert(!hisu_alert_level(&policy,true,true,5,23000000) && !policy.fired);
    assert(!hisu_alert_level(&policy,false,false,10,24000000));
    reset();
    assert(hisu_alert_request()==ESP_OK);
    unsigned index;
    assert(hisu_alert_prepare_take(&index) && index==0);
    assert(hisu_alert_request()==ESP_ERR_INVALID_STATE);
    hisu_alert_prepare_finish(ESP_FAIL,429);
    assert(!s_status.requested && s_status.http_status==429);
    for (unsigned i=0;i<HISU_ALERT_COUNT;++i) {
        if (!i) assert(hisu_alert_request()==ESP_OK);
        assert(hisu_voice_alert_prepare(idle));
        assert(s_status.error==ESP_OK && !mock_http_live && s_status.ready==i+1);
    }
    alert_record_t old_voice=s_records[0];
    old_voice.voice_crc=crc("Erinome",7);
    old_voice.header_crc=0;
    old_voice.header_crc=crc(&old_voice,sizeof(old_voice));
    assert(!header_valid(&old_voice,0));
    unsigned writes=mock_flash_writes;
    reboot(); assert(s_status.ready==HISU_ALERT_COUNT && mock_flash_writes==writes);
    uint8_t chunk[512];
    assert(hisu_alert_read(0,0,chunk,sizeof(chunk))==ESP_OK);
    assert(hisu_alert_read(0,24000,chunk,2)==ESP_ERR_INVALID_ARG);
    assert(hisu_alert_read(0,1,chunk,2)==ESP_ERR_INVALID_ARG);
    assert(hisu_alert_poll(true,false,15,1000000)==-1);
    int first=hisu_alert_poll(true,false,15,5000000); assert(first==0 || first==1);
    hisu_alert_played(first); assert(hisu_alert_poll(true,false,15,7000000)==-1);
    unsigned fired=s_policy.fired;
    hisu_alert_played(HISU_ALERT_REMINDER); assert(s_policy.fired==fired);
    assert(hisu_alert_poll(true,false,5,8000000)==-1);
    int urgent=hisu_alert_poll(true,false,5,12000000); assert(urgent==2 || urgent==3);
    hisu_alert_played(urgent);
    assert(hisu_alert_poll(true,false,15,14000000)==-1);
    assert(hisu_alert_poll(true,true,15,15000000)==-1);
    assert(hisu_alert_poll(true,true,15,25000000)==-1);
    assert(hisu_alert_poll(true,false,15,26000000)==-1);
    int second=hisu_alert_poll(true,false,15,30000000); assert(second>=0 && second!=first);
    mock_flash[MOCK_ALERT_BASE+ALERT_DATA_OFFSET]^=1; reboot();
    assert(s_status.ready==HISU_ALERT_COUNT-1 && !hisu_alert_size(0));
    for (int cut=0;cut<3;++cut) {
        reset(); assert(hisu_alert_request()==ESP_OK); mock_flash_budget=cut;
        assert(hisu_voice_alert_prepare(idle));
        assert(s_status.error!=ESP_OK && !mock_http_live && !s_status.ready);
        reboot(); assert(!s_status.ready);
    }
    for (int mode=1;mode<=9;++mode) {
        reset(); mock_mode=mode; assert(hisu_alert_request()==ESP_OK);
        writes=mock_flash_writes;
        assert(hisu_voice_alert_prepare(idle));
        assert(s_status.error!=ESP_OK && !s_status.ready && !mock_http_live);
        assert(mock_flash_writes==writes);
    }
    reset(); mock_http_status=429; assert(hisu_alert_request()==ESP_OK);
    assert(hisu_voice_alert_prepare(idle) && s_status.http_status==429 && !s_status.requested);
    reset(); mock_charging=false; assert(hisu_alert_request()==ESP_OK);
    unsigned calls=mock_http_calls; assert(hisu_voice_alert_prepare(idle) && mock_http_calls==calls);
    reset(); mock_idle=false; assert(hisu_alert_request()==ESP_OK);
    assert(hisu_voice_alert_prepare(idle) && s_status.error!=ESP_OK);
    reset(); mock_alloc_budget=0; assert(hisu_alert_request()==ESP_OK);
    assert(hisu_voice_alert_prepare(idle) && s_status.error==ESP_ERR_NO_MEM && !mock_http_live);
    reset(); mock_charging=false;
    int16_t *pcm=NULL; size_t pcm_bytes=0;
    assert(hisu_voice_reminder_tts("Tới giờ đi sửa xe",idle,&pcm,&pcm_bytes)==ESP_OK);
    assert(pcm && pcm_bytes==24000 && !mock_http_live); free(pcm);
    reset(); mock_http_status=429;
    assert(hisu_voice_reminder_tts("Lịch",idle,&pcm,&pcm_bytes)!=ESP_OK && !pcm && !mock_http_live);
    reset(); mock_mode=7;
    int64_t start=mock_now;
    assert(hisu_voice_reminder_tts("Lịch",idle,&pcm,&pcm_bytes)==ESP_ERR_TIMEOUT && !pcm && !mock_http_live);
    assert(mock_now-start<=8020000);
    if (argc == 2) {
        reset();
        FILE *image = fopen(argv[1],"rb"); assert(image);
        size_t bytes = HISU_ALERT_COUNT * ALERT_SLOT_BYTES;
        assert(fread(mock_flash + MOCK_ALERT_BASE,1,bytes,image)==bytes);
        assert(fgetc(image)==EOF); fclose(image);
        reboot(); assert(s_status.ready==HISU_ALERT_COUNT);
        puts("PC-generated image accepted by firmware CRC/header validation.");
    }
    puts("Offline alert tests passed: threshold/debounce/charge reset, variants, flash CRC/reboot/torn writes, Leda request, bounded PCM, cancellation, quota failure and cleanup. API/audio mocked.");
    return 0;
}
