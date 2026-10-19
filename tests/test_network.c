#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include "network_mock.h"
#include "../components/hisu_network/src/hisu_network.c"
#include "../components/hisu_network/src/hisu_connectivity.c"
static bool mock_recover_during_lookup, mock_manual_during_lookup;
static bool mock_saved_hidden[HISU_WIFI_SAVED_MAX];
static void mock_lookup_event(void) {
    if (mock_recover_during_lookup) {
        mock_recover_during_lookup = false;
        publish_internet(s_connection_generation, HISU_INTERNET_ONLINE);
    }
    if (mock_manual_during_lookup) {
        mock_manual_during_lookup = false;
        s_status.joining = true;
    }
}

int hisu_settings_get(hisu_setting_t key) { return 1; }
esp_err_t hisu_settings_set(hisu_setting_t key, int value) { return ESP_OK; }
esp_err_t hisu_settings_get_wifi_at(unsigned index, char ssid[33], char password[65]) {
    mock_lookup_event();
    ssid[0] = password[0] = 0;
    if (index >= mock_saved_count) return ESP_ERR_NVS_NOT_FOUND;
    memcpy(ssid, mock_saved_ssids[index], 33);
    memcpy(password, mock_saved_passwords[index], 65);
    return ESP_OK;
}
esp_err_t hisu_settings_get_wifi(char ssid[33], char password[65]) {
    return hisu_settings_get_wifi_at(0, ssid, password);
}
esp_err_t hisu_settings_find_wifi(const char *ssid, char password[65]) {
    mock_lookup_event();
    for (unsigned i = 0; i < mock_saved_count; ++i)
        if (!strcmp(ssid, mock_saved_ssids[i])) {
            memcpy(password, mock_saved_passwords[i], 65); return ESP_OK;
        }
    password[0] = 0; return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t hisu_settings_save_wifi(const char *ssid, const char *password) { return ESP_OK; }
esp_err_t hisu_settings_save_wifi_ex(const char *ssid, const char *password, bool hidden) { return ESP_OK; }
bool hisu_settings_wifi_hidden(const char *ssid) {
    for (unsigned i = 0; i < mock_saved_count; ++i)
        if (!strcmp(ssid, mock_saved_ssids[i])) return mock_saved_hidden[i];
    return false;
}

static void reset(void) {
    s_status = (hisu_network_status_t){.ready = true, .enabled = true};
    memset(s_failures, 0, sizeof(s_failures));
    memset(s_roam_tried, 0, sizeof(s_roam_tried));
    memset(mock_saved_hidden, 0, sizeof(mock_saved_hidden));
    memset(s_ssid, 0, sizeof(s_ssid));
    memset(s_password, 0, sizeof(s_password));
    s_started = s_want_connection = s_save_candidate = s_auto_join = false;
    s_performance = s_performance_requested = false; mock_wifi_ps = -1;
    s_had_connection = false;
    s_deadline = s_retry_at = 0; s_retries = 0;
    s_connection_generation = 0; s_internet_failures = 0;
    s_lock = s_events = (void *)1;
    mock_saved_count = mock_count = 0;
    mock_connects = 0; mock_connect_error = ESP_OK; mock_now = 1000000;
    mock_disconnects = 0; mock_scan_error = ESP_OK; mock_scans = 0;
    mock_http_count = 0; mock_http_cleanup_count = 0; mock_read_chunk = 0;
    mock_redirect_disabled = false;
    mock_probe_notifications = 0; mock_mode_error = ESP_OK;
    mock_recover_during_lookup = mock_manual_during_lookup = false;
    mock_http[0] = (mock_http_response_t){.status = 204, .body = ""};
    mock_http[1] = (mock_http_response_t){.status = 200, .body = "Microsoft Connect Test"};
}
static void saved(const char *ssid, bool visible, int rssi) {
    strcpy(mock_saved_ssids[mock_saved_count], ssid);
    strcpy(mock_saved_passwords[mock_saved_count++], "password123");
    if (visible) {
        wifi_ap_record_t *ap = &mock_aps[mock_count++];
        memset(ap, 0, sizeof(*ap)); strcpy((char *)ap->ssid, ssid);
        ap->rssi = rssi; ap->authmode = WIFI_AUTH_WPA2_PSK;
    }
}
static void fail_current(bool rejected) {
    record_join_failure(rejected);
    s_want_connection = false; s_deadline = s_retry_at = 0;
    failed(rejected ? ESP_ERR_INVALID_ARG : ESP_ERR_TIMEOUT);
}
int main(void) {
    hisu_network_link_t link;
    s_lock=NULL; hisu_network_get_link(&link);
    assert(!link.connected && !link.setup_enabled && link.internet==HISU_INTERNET_UNKNOWN);
    reset(); s_status.connected=true; s_status.setup_enabled=true; s_status.internet=HISU_INTERNET_CAPTIVE;
    hisu_network_get_link(&link);
    assert(link.connected && link.setup_enabled && link.internet==HISU_INTERNET_CAPTIVE);
    reset(); strcpy(s_ssid,"New home");
    begin_join();
    assert(!strcmp(s_status.ssid,"New home"));
    assert(mock_config.sta.sae_pwe_h2e==WPA3_SAE_PWE_BOTH);
    wifi_event_sta_disconnected_t disconnect={.ssid="Old cafe",.ssid_len=8,.reason=WIFI_REASON_AUTH_FAIL};
    unsigned sets=mock_event_sets;
    on_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&disconnect);
    assert(mock_event_sets==sets && s_status.disconnect_reason==0);
    memcpy(disconnect.ssid,"New home",8);
    on_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&disconnect);
    assert(mock_event_sets==sets+1 && s_status.disconnect_reason==WIFI_REASON_AUTH_FAIL);
    disconnect.ssid_len=33;
    on_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&disconnect);
    assert(mock_event_sets==sets+1);
    disconnect.ssid_len=0; /* No-AP failures may omit the SSID. */
    on_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&disconnect);
    assert(mock_event_sets==sets+2);

    reset(); s_status.connected=true; s_status.internet=HISU_INTERNET_ONLINE;
    uint32_t generation=s_connection_generation;
    setup_radio(true);
    assert(s_status.setup_enabled && mock_probe_notifications==0);
    setup_radio(false);
    assert(!s_status.setup_enabled && s_status.internet==HISU_INTERNET_CHECKING);
    assert(mock_probe_notifications==1 && s_connection_generation==generation+2);
    publish_internet(generation,HISU_INTERNET_OFFLINE);
    assert(s_status.internet==HISU_INTERNET_CHECKING);
    publish_internet(s_connection_generation,HISU_INTERNET_ONLINE);
    assert(s_status.internet==HISU_INTERNET_ONLINE);
    setup_radio(false);
    assert(mock_probe_notifications==1); /* No duplicate close probe. */

    reset(); s_status.setup_enabled=true; s_status.connected=true;
    mock_mode_error=ESP_ERR_INVALID_STATE;
    setup_radio(false);
    assert(s_status.setup_enabled && mock_probe_notifications==0 && s_connection_generation==0);
    reset(); s_status.setup_enabled=true;
    setup_radio(false);
    assert(!s_status.setup_enabled && mock_probe_notifications==0);
    reset(); s_status.setup_enabled=true; s_status.connected=true; s_performance_requested=true;
    setup_radio(false);
    assert(!s_status.setup_enabled && mock_probe_notifications==0);
    reset(); s_performance = true; assert(radio_start() == ESP_OK && mock_wifi_ps == WIFI_PS_NONE);
    reset(); assert(radio_start() == ESP_OK && mock_wifi_ps == WIFI_PS_MIN_MODEM);
    reset(); saved("Strong", true, -30); saved("Backup", true, -60);
    scan(); assert(!strcmp(s_ssid, "Strong"));
    fail_current(true); scan();
    assert(!strcmp(s_ssid, "Backup"));
    assert(s_status.error == ESP_OK && !s_status.aps[0].password_rejected);
    fail_current(false); mock_now += 60000000; scan();
    assert(!strcmp(s_ssid, "Strong")); /* One handshake timeout is not a bad-password verdict. */

    reset(); saved("Home",true,-30); strcpy(s_ssid,"Home"); strcpy(s_password,"wrongpass");
    s_save_candidate=true; record_join_failure(true);
    assert(auto_join_allowed("Home")); scan();
    assert(!strcmp(s_password,"password123") && !s_save_candidate);
    for (unsigned i=0;i<3;++i) { record_join_failure(true); mock_now+=60000000; }
    assert(!auto_join_allowed("Home") && join_failure("Home",false)->password_rejected);
    mock_now+=600000000;
    assert(auto_join_allowed("Home") && !join_failure("Home",false)->password_rejected);
    s_want_connection=true; s_had_connection=false; s_started=true;
    cancel_join(); assert(!s_want_connection && !s_password[0] && !s_deadline && !s_status.joining);
    s_want_connection=s_had_connection=s_status.connected=true;
    cancel_join(); assert(s_status.connected && s_want_connection);
    s_status.connected=false; s_status.joining=true;
    s_save_candidate=true; s_deadline=mock_now+1000000; s_retry_at=mock_now+500000;
    cancel_join(); /* Historical success must not block BACK after a lost connection. */
    assert(!s_want_connection && !s_save_candidate && !s_password[0]);
    assert(!s_status.joining && !s_deadline && !s_retry_at);

    reset(); saved("Cafe", false, 0); scan();
    assert(!s_want_connection && mock_connects == 0);
    reset(); saved("Hidden", false, 0); mock_saved_hidden[0] = true; scan();
    assert(s_want_connection && !strcmp(s_ssid, "Hidden"));
    assert(mock_config.sta.threshold.authmode == WIFI_AUTH_WPA2_PSK);

    reset(); saved("Hidden first", false, 0); saved("Visible", true, -80);
    mock_saved_hidden[0] = true;
    scan(); assert(!strcmp(s_ssid, "Visible"));
    fail_current(true); scan(); assert(!strcmp(s_ssid, "Hidden first"));

    reset(); saved("Clone", true, -30); mock_aps[0].authmode = WIFI_AUTH_OPEN;
    scan(); assert(!s_want_connection && mock_connects == 0);

    reset(); saved("Strong", true, -30); saved("Backup", true, -60);
    scan(); fail_current(false); scan(); assert(!strcmp(s_ssid, "Backup"));
    fail_current(false); scan(); assert(!s_want_connection);
    mock_now += 60000000; scan(); assert(!strcmp(s_ssid, "Strong"));

    reset(); saved("Saved", true, -30);
    strcpy(s_ssid, "Manual"); strcpy(s_password, "password123");
    s_want_connection = s_save_candidate = true;
    scan(); assert(!strcmp(s_ssid, "Manual") && s_want_connection && s_save_candidate);
    assert(mock_connects == 0 && s_retry_at > mock_now);

    reset(); saved("Broken", true, -30); saved("Fallback", true, -60);
    mock_connect_error = ESP_ERR_INVALID_STATE; scan();
    assert(mock_connects == 2 && !s_want_connection);
    assert(!auto_join_allowed("Broken") && !auto_join_allowed("Fallback"));

    reset(); assert(hisu_connectivity_probe() == HISU_INTERNET_ONLINE);
    assert(mock_http_count == 1 && mock_http_cleanup_count == 1 && mock_redirect_disabled);
    reset(); mock_http[0].error = ESP_ERR_TIMEOUT; mock_read_chunk = 3;
    assert(hisu_connectivity_probe() == HISU_INTERNET_ONLINE); /* Fragmented fallback body. */
    assert(mock_http_count == 2 && mock_http_cleanup_count == 2);
    reset(); mock_http[0].error = mock_http[1].error = ESP_ERR_TIMEOUT;
    assert(hisu_connectivity_probe() == HISU_INTERNET_OFFLINE);
    reset(); mock_http[0].error = ESP_ERR_TIMEOUT; mock_http[1].status = 302;
    assert(hisu_connectivity_probe() == HISU_INTERNET_CAPTIVE);
    reset(); mock_http[0].error = ESP_ERR_TIMEOUT; mock_http[1].body = "<html>Login required</html>";
    assert(hisu_connectivity_probe() == HISU_INTERNET_CAPTIVE);
    assert(mock_http[1].offset == 23); /* Bounded read, no redirect or full page download. */
    reset(); mock_http[0].status = 302;
    assert(hisu_connectivity_probe() == HISU_INTERNET_ONLINE); /* One blocked provider is insufficient. */
    reset(); mock_http[0].error = ESP_ERR_TIMEOUT; mock_http[1].status = 511;
    assert(hisu_connectivity_probe() == HISU_INTERNET_CAPTIVE);
    reset(); mock_http[0].error = ESP_ERR_TIMEOUT; mock_http[1].status = 503;
    assert(hisu_connectivity_probe() == HISU_INTERNET_OFFLINE);
    reset(); mock_http[0].error = ESP_ERR_TIMEOUT; mock_http[1].incomplete = true;
    assert(hisu_connectivity_probe() == HISU_INTERNET_OFFLINE);
    reset(); mock_http[0].error = ESP_ERR_TIMEOUT; mock_http[1].read_error = true;
    assert(hisu_connectivity_probe() == HISU_INTERNET_OFFLINE);

    reset(); s_status.connected = true; reset_internet(HISU_INTERNET_CHECKING);
    publish_internet(s_connection_generation - 1, HISU_INTERNET_OFFLINE);
    assert(s_status.internet == HISU_INTERNET_CHECKING && s_internet_failures == 0);
    publish_internet(s_connection_generation, HISU_INTERNET_CAPTIVE);
    assert(s_status.connected && hisu_network_has_internet_warning(&s_status));
    publish_internet(s_connection_generation, HISU_INTERNET_ONLINE);
    assert(!hisu_network_has_internet_warning(&s_status) && s_internet_failures == 0);
    s_status.connected = false;
    publish_internet(s_connection_generation, HISU_INTERNET_OFFLINE);
    assert(s_status.internet == HISU_INTERNET_ONLINE && !hisu_network_has_internet_warning(&s_status));

    reset(); saved("No Internet", true, -30); saved("Backup", true, -60); scan();
    s_status.connected = true; s_status.joining = false;
    reset_internet(HISU_INTERNET_CHECKING);
    publish_internet(s_connection_generation, HISU_INTERNET_OFFLINE);
    assert(!roam_without_internet());
    publish_internet(s_connection_generation, HISU_INTERNET_OFFLINE);
    assert(roam_without_internet() && !strcmp(s_ssid, "Backup"));
    assert(mock_scans == 2 && mock_connects == 2); /* One refresh, then direct join to the visible backup. */
    assert(!auto_join_allowed("No Internet"));

    reset(); saved("Only network", true, -30); scan();
    s_status.connected = true; s_status.joining = false; s_status.internet = HISU_INTERNET_CAPTIVE;
    s_internet_failures = 2;
    assert(!roam_without_internet() && s_status.connected && hisu_network_has_internet_warning(&s_status));
    saved("Backup", true, -60); s_auto_join = false; s_internet_failures = 2;
    assert(!roam_without_internet() && !strcmp(s_ssid, "Only network"));

    reset(); saved("Current", true, -30); saved("Absent router", false, 0); scan();
    mock_disconnects = 0;
    s_status.connected = true; s_status.joining = false; s_status.internet = HISU_INTERNET_OFFLINE;
    strcpy(s_status.ip, "192.168.1.55"); s_internet_failures = 2;
    assert(!roam_without_internet() && s_status.connected && !strcmp(s_ssid, "Current"));
    assert(mock_connects == 1 && mock_disconnects == 0 && !strcmp(s_status.ip, "192.168.1.55"));

    reset(); saved("Current", true, -30); saved("Stale backup", true, -60); scan();
    mock_disconnects = 0;
    s_status.connected = true; s_status.joining = false; s_status.internet = HISU_INTERNET_CAPTIVE;
    mock_count = 1; s_internet_failures = 2; /* Backup vanished since the previous scan. */
    assert(!roam_without_internet() && s_status.connected && mock_disconnects == 0);
    assert(s_status.count == 1 && mock_connects == 1);

    reset(); saved("Current", true, -30); saved("Backup", true, -60); scan();
    s_status.connected = true; s_status.joining = false; s_status.internet = HISU_INTERNET_OFFLINE;
    mock_scan_error = ESP_ERR_TIMEOUT; s_internet_failures = 2;
    mock_disconnects = 0;
    assert(!roam_without_internet() && s_status.connected && mock_disconnects == 0);
    assert(mock_connects == 1); /* A stale list cannot be used after a scan error. */

    reset(); saved("Current", true, -30); saved("Open clone", true, -60); scan();
    mock_disconnects = 0;
    s_status.connected = true; s_status.joining = false; s_status.internet = HISU_INTERNET_OFFLINE;
    mock_aps[1].authmode = WIFI_AUTH_OPEN; s_internet_failures = 2;
    assert(!roam_without_internet() && s_status.connected && mock_disconnects == 0);

    reset(); saved("Current", true, -30); saved("Backup", true, -60); scan();
    s_status.connected = true; s_status.joining = false; s_status.internet = HISU_INTERNET_OFFLINE;
    s_internet_failures = 2; mock_recover_during_lookup = true;
    assert(!roam_without_internet() && s_status.connected && s_status.internet == HISU_INTERNET_ONLINE);
    s_status.internet = HISU_INTERNET_OFFLINE; s_internet_failures = 2; mock_manual_during_lookup = true;
    assert(!roam_without_internet() && !strcmp(s_ssid, "Current"));

    reset(); saved("Home",true,-30); saved("Backup",true,-60); scan();
    s_status.connected=true; s_status.joining=false; s_status.internet=HISU_INTERNET_OFFLINE;
    s_internet_failures=2; s_performance=true;
    assert(!roam_without_internet() && mock_connects==1 && mock_scans==1);
    s_performance=false;
    assert(hisu_network_set_performance(true)==ESP_OK && s_performance_requested);
    assert(!roam_without_internet()); /* Protected even before the worker applies power mode. */
    assert(hisu_network_scan()==ESP_ERR_INVALID_STATE);
    assert(hisu_network_set_performance(false)==ESP_OK);
    s_performance=true;
    publish_internet(s_connection_generation,HISU_INTERNET_OFFLINE);
    assert(s_internet_failures==2); /* In-flight probes cannot change a live session. */
    s_performance=false; s_status.setup_enabled=true;
    assert(!roam_without_internet() && mock_connects==1);

    reset(); saved("Home",true,-30); scan_for(false);
    assert(mock_connects==0 && s_status.count==1);
    s_status.setup_enabled=true; scan();
    assert(mock_connects==0); /* Even an automatic scan cannot join during Setting. */

    reset(); saved("A",true,-30); saved("B",true,-50); saved("C",true,-70); scan();
    for (int i=0; i<2; ++i) {
        mock_now += 60000000;
        s_status.connected=true; s_status.joining=false; s_status.internet=HISU_INTERNET_OFFLINE;
        s_internet_failures=2;
        assert(roam_without_internet());
    }
    assert(!strcmp(s_ssid,"C"));
    mock_now += 300000000;
    s_status.connected=true; s_status.joining=false; s_status.internet=HISU_INTERNET_OFFLINE;
    s_internet_failures=2;
    int connects = mock_connects;
    assert(!roam_without_internet() && mock_connects==connects && s_status.connected);
    assert(!auto_join_allowed("A") && !auto_join_allowed("B"));
    s_status.connected=false; s_want_connection=false; scan();
    assert(!strcmp(s_ssid,"C")); /* Reconnect fallback cannot bypass the outage history. */

    reset(); saved("Cafe",true,-30); saved("Home",true,-60); scan();
    s_had_connection=true;
    mock_aps[0]=mock_aps[1]; mock_count=1; /* Board moved away from Cafe. */
    retry_join();
    assert(!strcmp(s_ssid,"Home") && mock_connects==2 && mock_scans==2);
    reset(); saved("Cafe",true,-30); scan(); mock_count=0;
    retry_join();
    assert(!s_want_connection && mock_connects==1 && !s_status.ssid[0]);
    reset(); saved("Home",true,-30); scan(); s_retries=4;
    retry_join(); assert(s_retries==4 && mock_connects==2);
    s_status.setup_enabled=true; retry_join(); assert(mock_connects==2 && s_retry_at>mock_now);
    reset(); saved("Hidden",false,0); mock_saved_hidden[0]=true; scan();
    retry_join(); assert(mock_connects==2 && mock_scans==1);

    puts("Network tests passed: saved/hidden fallback, auth history, connectivity probes, captive portals, stale results, warning recovery and visible-only offline roaming.");
    return 0;
}
