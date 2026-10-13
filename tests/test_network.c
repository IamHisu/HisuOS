#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include "network_mock.h"
#include "../components/hisu_network/src/hisu_network.c"
#include "../components/hisu_network/src/hisu_connectivity.c"
static bool mock_recover_during_lookup, mock_manual_during_lookup;

int hisu_settings_get(hisu_setting_t key) { return 1; }
esp_err_t hisu_settings_set(hisu_setting_t key, int value) { return ESP_OK; }
esp_err_t hisu_settings_get_wifi_at(unsigned index, char ssid[33], char password[65]) {
    if (mock_recover_during_lookup) {
        mock_recover_during_lookup = false;
        publish_internet(s_connection_generation, HISU_INTERNET_ONLINE);
    }
    if (mock_manual_during_lookup) {
        mock_manual_during_lookup = false;
        s_status.joining = true;
    }
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
    for (unsigned i = 0; i < mock_saved_count; ++i)
        if (!strcmp(ssid, mock_saved_ssids[i])) {
            memcpy(password, mock_saved_passwords[i], 65); return ESP_OK;
        }
    password[0] = 0; return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t hisu_settings_save_wifi(const char *ssid, const char *password) { return ESP_OK; }

static void reset(void) {
    s_status = (hisu_network_status_t){.ready = true, .enabled = true};
    memset(s_failures, 0, sizeof(s_failures));
    memset(s_ssid, 0, sizeof(s_ssid));
    memset(s_password, 0, sizeof(s_password));
    s_started = s_want_connection = s_save_candidate = s_auto_join = false;
    s_deadline = s_retry_at = 0; s_retries = 0;
    s_connection_generation = 0; s_internet_failures = 0;
    s_lock = s_events = (void *)1;
    mock_saved_count = mock_count = 0;
    mock_connects = 0; mock_connect_error = ESP_OK; mock_now = 1000000;
    mock_http_count = 0; mock_http_cleanup_count = 0; mock_read_chunk = 0;
    mock_redirect_disabled = false;
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
    reset(); saved("Strong", true, -30); saved("Backup", true, -60);
    scan(); assert(!strcmp(s_ssid, "Strong"));
    fail_current(true); scan();
    assert(!strcmp(s_ssid, "Backup"));
    assert(s_status.error == ESP_OK && s_status.aps[0].password_rejected);
    fail_current(false); mock_now += 60000000; scan();
    assert(!strcmp(s_ssid, "Backup")); /* Rejected password stays blocked. */

    reset(); saved("Hidden", false, 0); scan();
    assert(s_want_connection && !strcmp(s_ssid, "Hidden"));
    assert(mock_config.sta.threshold.authmode == WIFI_AUTH_WPA2_PSK);

    reset(); saved("Hidden first", false, 0); saved("Visible", true, -80);
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
    assert(!auto_join_allowed("No Internet"));

    reset(); saved("Only network", true, -30); scan();
    s_status.connected = true; s_status.joining = false; s_status.internet = HISU_INTERNET_CAPTIVE;
    s_internet_failures = 2;
    assert(!roam_without_internet() && s_status.connected && hisu_network_has_internet_warning(&s_status));
    saved("Backup", true, -60); s_auto_join = false; s_internet_failures = 2;
    assert(!roam_without_internet() && !strcmp(s_ssid, "Only network"));

    reset(); saved("Current", true, -30); saved("Backup", true, -60); scan();
    s_status.connected = true; s_status.joining = false; s_status.internet = HISU_INTERNET_OFFLINE;
    s_internet_failures = 2; mock_recover_during_lookup = true;
    assert(!roam_without_internet() && s_status.connected && s_status.internet == HISU_INTERNET_ONLINE);
    s_status.internet = HISU_INTERNET_OFFLINE; s_internet_failures = 2; mock_manual_during_lookup = true;
    assert(!roam_without_internet() && !strcmp(s_ssid, "Current"));

    puts("Network tests passed: saved/hidden fallback, auth history, connectivity probes, captive portals, stale results, warning recovery and offline roaming.");
    return 0;
}
