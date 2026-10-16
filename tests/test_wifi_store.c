#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include "settings_mock.h"
#include "../components/hisu_settings/src/hisu_settings.c"

static esp_err_t read_during_failed_commit(void)
{
    unsigned takes = mock_semaphore_takes;
    assert(hisu_settings_get(HISU_SETTING_VOLUME) == 55);
    assert(mock_semaphore_takes == takes);
    return ESP_ERR_INVALID_STATE;
}

int main(void)
{
    assert(hisu_settings_get(HISU_SETTING_VOLUME) == -1);
    assert(hisu_settings_init()==ESP_OK);
    assert(hisu_settings_get((hisu_setting_t)-1) == -1);
    assert(hisu_settings_get(HISU_SETTING_COUNT) == -1);
    mock_during_commit = read_during_failed_commit;
    assert(hisu_settings_set(HISU_SETTING_VOLUME, 60) == ESP_ERR_INVALID_STATE);
    assert(hisu_settings_get(HISU_SETTING_VOLUME) == 55);
    mock_during_commit = NULL;
    assert(hisu_settings_set(HISU_SETTING_VOLUME, 60) == ESP_OK);
    assert(hisu_settings_get(HISU_SETTING_VOLUME) == 60);
    mock_wallpaper_default=1; /* Old selection must not hide uploaded media after an upgrade. */
    s_lock=NULL;
    assert(hisu_settings_init()==ESP_OK);
    assert(!mock_legacy_wallpaper_reads);
    char secret[128];
    assert(!hisu_settings_has_gemini_key());
    assert(hisu_settings_set_gemini_key("legacy-chat")==ESP_OK);
    assert(!strcmp(mock_secrets[0],"legacy-chat"));
    for (unsigned i=1;i<HISU_GEMINI_KEY_COUNT;++i) {
        char value[32]; snprintf(value,sizeof(value),"profile-%u",i);
        assert(hisu_settings_set_gemini_profile((hisu_gemini_key_t)i,value)==ESP_OK);
        assert(hisu_settings_get_gemini_profile((hisu_gemini_key_t)i,secret)==ESP_OK && !strcmp(secret,value));
    }
    assert(hisu_settings_get_gemini_key(secret)==ESP_OK && !strcmp(secret,"legacy-chat"));
    uint32_t revision=hisu_settings_gemini_revision();
    assert(hisu_settings_set_gemini_profile(HISU_GEMINI_READER,"bad\nheader")==ESP_ERR_INVALID_ARG);
    assert(hisu_settings_set_gemini_profile(HISU_GEMINI_KEY_COUNT,"invalid")==ESP_ERR_INVALID_ARG);
    assert(hisu_settings_gemini_revision()==revision);
    s_lock=NULL; assert(hisu_settings_init()==ESP_OK);
    assert(hisu_settings_get_gemini_profile(HISU_GEMINI_TRANSLATE,secret)==ESP_OK && !strcmp(secret,"profile-3"));
    assert(hisu_settings_set_gemini_key("")==ESP_OK && hisu_settings_has_gemini_key()); /* Backup-only Chat is usable. */
    assert(hisu_settings_set_gemini_profile(HISU_GEMINI_CHAT_BACKUP,"")==ESP_OK && !hisu_settings_has_gemini_key());
    assert(hisu_settings_has_gemini_profile(HISU_GEMINI_READER) && hisu_settings_has_gemini_profile(HISU_GEMINI_TRANSLATE));
    memset(secret,'x',sizeof(secret));
    assert(hisu_settings_get_gemini_key(secret)!=ESP_OK);
    for (unsigned i=0;i<sizeof(secret);++i) assert(!secret[i]);
    wifi_credentials_t legacy={"Old cafe","password123"};
    memcpy(mock_wifi_legacy,&legacy,sizeof(legacy)); mock_wifi_legacy_size=sizeof(legacy);
    char ssid[33], password[65];
    assert(hisu_settings_get_wifi(ssid,password)==ESP_OK && !strcmp(ssid,"Old cafe"));
    assert(!hisu_settings_wifi_hidden(ssid));

    wifi_store_t old={.version=1,.count=2,.entries={{"Home","password123"},{"Cafe","password456"}}};
    memcpy(mock_wifi_list,&old,offsetof(wifi_store_t,hidden_mask));
    mock_wifi_list_size=offsetof(wifi_store_t,hidden_mask);
    assert(hisu_settings_get_wifi_at(1,ssid,password)==ESP_OK && !strcmp(ssid,"Cafe"));
    assert(!hisu_settings_wifi_hidden(ssid));
    assert(hisu_settings_save_wifi_ex("Hidden","hiddenpass",true)==ESP_OK);
    assert(hisu_settings_wifi_hidden("Hidden") && !hisu_settings_wifi_hidden("Home"));
    assert(hisu_settings_get_wifi_at(2,ssid,password)==ESP_OK && !strcmp(ssid,"Cafe"));
    assert(hisu_settings_save_wifi("Home","newpassword")==ESP_OK);
    assert(hisu_settings_wifi_hidden("Hidden") && !hisu_settings_wifi_hidden("Home"));
    assert(hisu_settings_save_wifi_ex("Hidden","newhiddenpass",true)==ESP_OK);
    assert(hisu_settings_get_wifi(ssid,password)==ESP_OK && !strcmp(password,"newhiddenpass"));
    assert(hisu_settings_wifi_hidden("Hidden"));
    assert(hisu_settings_save_wifi("Hidden","visiblepass")==ESP_OK);
    assert(!hisu_settings_wifi_hidden("Hidden"));

    for (unsigned i=0;i<10;++i) {
        char name[33]; snprintf(name,sizeof(name),"Network%u",i);
        assert(hisu_settings_save_wifi_ex(name,"password123",i%2==0)==ESP_OK);
    }
    for (unsigned i=0;i<8;++i) {
        char expected[33]; snprintf(expected,sizeof(expected),"Network%u",9-i);
        assert(hisu_settings_get_wifi_at(i,ssid,password)==ESP_OK && !strcmp(ssid,expected));
        assert(hisu_settings_wifi_hidden(ssid)==((9-i)%2==0));
    }
    wifi_store_t bad={.version=2,.count=1,.entries={{"Bad","password123"}},.hidden_mask=2};
    memcpy(mock_wifi_list,&bad,sizeof(bad)); mock_wifi_list_size=sizeof(bad);
    assert(hisu_settings_get_wifi(ssid,password)==ESP_ERR_INVALID_SIZE);
    assert(!hisu_settings_wifi_hidden("Bad") && !ssid[0] && !password[0]);
    mock_wifi_list_size=sizeof(bad)-1;
    assert(hisu_settings_get_wifi(ssid,password)==ESP_ERR_INVALID_SIZE);
    puts("Wi-Fi store tests passed: legacy/v1 migration, hidden flags, MRU reorder, eviction and invalid records.");
    return 0;
}
