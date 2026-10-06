#include "hisu_network.h"
#include "hisu_settings.h"
#include "hisu_clock.h"
#include "hisu_wifi_input.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

enum {CMD_SCAN, CMD_JOIN, CMD_ENABLE, CMD_SETUP};
enum {EVENT_IP = 1, EVENT_DISCONNECTED = 2};
typedef struct { int type; bool enabled; char ssid[33], password[65]; } command_t;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_commands;
static EventGroupHandle_t s_events;
static hisu_network_status_t s_status;
static esp_netif_t *s_netif;
static esp_netif_t *s_setup_netif;
static esp_event_handler_instance_t s_wifi_handler, s_ip_handler;
/* Worker owns radio and credentials; callbacks publish connection events. */
static bool s_started, s_want_connection, s_save_candidate;
static char s_ssid[33], s_password[65];
static int s_retries;
static int64_t s_deadline, s_retry_at;

bool hisu_network_valid_password(const char *password)
{
    return hisu_wifi_password_is_valid(password) != 0;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && (id == WIFI_EVENT_AP_STACONNECTED ||
        id == WIFI_EVENT_AP_STADISCONNECTED || id == WIFI_EVENT_AP_STOP)) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.setup_client_connected = id == WIFI_EVENT_AP_STACONNECTED;
        ++s_status.revision;
        xSemaphoreGive(s_lock);
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.disconnect_reason = event->reason;
        xSemaphoreGive(s_lock);
        xEventGroupSetBits(s_events, EVENT_DISCONNECTED);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        xEventGroupSetBits(s_events, EVENT_IP);
}

static void failed(esp_err_t error)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.error = error;
    s_status.joining = false;
    s_status.scanning = false;
    ++s_status.revision;
    xSemaphoreGive(s_lock);
}

static esp_err_t radio_start(void)
{
    if (s_started) return ESP_OK;
    esp_err_t err = esp_wifi_start();
    if (err == ESP_OK) s_started = true;
    return err;
}

static esp_err_t radio_mode(bool station, bool setup)
{
    return esp_wifi_set_mode(setup ? (station ? WIFI_MODE_APSTA : WIFI_MODE_AP) : WIFI_MODE_STA);
}

static void setup_radio(bool enabled)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool station = s_status.enabled, previous = s_status.setup_enabled;
    xSemaphoreGive(s_lock);
    if (previous == enabled) return;
    esp_err_t err = ESP_OK;
    wifi_config_t config = {0};
    if (enabled) {
        uint8_t mac[6];
        err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        if (err != ESP_OK) { failed(err); return; }
        snprintf((char *)config.ap.ssid, sizeof(config.ap.ssid), "Hisu-Setting-%02X%02X%02X", mac[3], mac[4], mac[5]);
        /* STA radio provides RF entropy, including when station Wi-Fi is disabled. */
        err = radio_start();
        if (err != ESP_OK) { failed(err); return; }
        uint8_t random[8];
        esp_fill_random(random, sizeof(random));
        hisu_wifi_setup_password(random, (char *)config.ap.password);
        memset(random, 0, sizeof(random));
        config.ap.ssid_len = strlen((char *)config.ap.ssid);
        config.ap.channel = 1; config.ap.max_connection = 1;
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        config.ap.pmf_cfg.capable = true;
        err = radio_mode(station, true);
        if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &config);
        if (err == ESP_OK) err = radio_start();
        if (err == ESP_OK) esp_wifi_deauth_sta(0);
    } else {
        err = radio_mode(station, false);
        if (err == ESP_OK && !station && s_started) {
            err = esp_wifi_stop();
            if (err == ESP_OK) s_started = false;
        }
    }
    esp_netif_ip_info_t ip = {0};
    if (err == ESP_OK && enabled) err = esp_netif_get_ip_info(s_setup_netif, &ip);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (err == ESP_OK) {
        s_status.setup_enabled = enabled;
        s_status.setup_client_connected = false;
        memset(s_status.setup_ssid, 0, sizeof(s_status.setup_ssid));
        memset(s_status.setup_password, 0, sizeof(s_status.setup_password));
        s_status.setup_ip[0] = 0;
        if (enabled) {
            memcpy(s_status.setup_ssid, config.ap.ssid, config.ap.ssid_len);
            memcpy(s_status.setup_password, config.ap.password, 16);
            snprintf(s_status.setup_ip, sizeof(s_status.setup_ip), IPSTR, IP2STR(&ip.ip));
        }
        ++s_status.revision;
    } else s_status.error = err;
    xSemaphoreGive(s_lock);
    memset(&config, 0, sizeof(config));
    if (err != ESP_OK && enabled) {
        radio_mode(station, false);
        if (!station && esp_wifi_stop() == ESP_OK) s_started = false;
    }
}

static void begin_join(void)
{
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, s_ssid, strlen(s_ssid));
    memcpy(config.sta.password, s_password, strlen(s_password));
    config.sta.pmf_cfg.capable = true;
    config.sta.threshold.authmode = s_password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    xEventGroupClearBits(s_events, EVENT_IP | EVENT_DISCONNECTED);
    if (s_started && esp_wifi_disconnect() == ESP_OK)
        xEventGroupWaitBits(s_events, EVENT_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(500));
    /* Disconnect event for the previous AP must not cancel the new attempt. */
    xEventGroupClearBits(s_events, EVENT_IP | EVENT_DISCONNECTED);
    esp_err_t err = radio_start();
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) err = esp_wifi_connect();
    memset(&config, 0, sizeof(config));
    if (err != ESP_OK) { s_want_connection = false; failed(err); return; }
    s_retry_at = 0;
    s_deadline = esp_timer_get_time() + 25000000;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.joining = true;
    s_status.connected = false;
    s_status.ip[0] = 0;
    s_status.error = ESP_OK;
    s_status.disconnect_reason = 0;
    memcpy(s_status.ssid, s_ssid, sizeof(s_ssid));
    ++s_status.revision;
    xSemaphoreGive(s_lock);
}

/* Caller owns s_lock. Saved networks first; strongest signal in each group. */
static void sort_access_points(void)
{
    for (int i = 1; i < s_status.count; ++i) {
        hisu_wifi_ap_t ap = s_status.aps[i];
        int j = i;
        while (j > 0 && (ap.saved > s_status.aps[j - 1].saved ||
               (ap.saved == s_status.aps[j - 1].saved && ap.rssi > s_status.aps[j - 1].rssi))) {
            s_status.aps[j] = s_status.aps[j - 1];
            --j;
        }
        s_status.aps[j] = ap;
    }
}

static void scan(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool enabled = s_status.enabled;
    bool connected = s_status.connected;
    if (enabled) {
        s_status.scanning = true;
        s_status.error = ESP_OK;
        ++s_status.revision;
    }
    xSemaphoreGive(s_lock);
    if (!enabled) { failed(ESP_ERR_INVALID_STATE); return; }
    /* A user scan takes priority over reconnecting to an unavailable AP.
     * The worker owns both operations, so retries cannot run during the scan. */
    bool resume_connection = s_want_connection && !connected;
    if (resume_connection) {
        s_want_connection = false;
        s_deadline = s_retry_at = 0;
        xEventGroupClearBits(s_events, EVENT_IP | EVENT_DISCONNECTED);
        if (s_started && esp_wifi_disconnect() == ESP_OK)
            xEventGroupWaitBits(s_events, EVENT_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(500));
        xEventGroupClearBits(s_events, EVENT_IP | EVENT_DISCONNECTED);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.joining = false;
        s_status.connected = false;
        s_status.ip[0] = 0;
        xSemaphoreGive(s_lock);
    }
    esp_err_t err = radio_start();
    wifi_scan_config_t config = {.show_hidden = false};
    if (err == ESP_OK) err = esp_wifi_scan_start(&config, true);
    wifi_ap_record_t records[32];
    uint16_t count = 32;
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) { esp_wifi_clear_ap_list(); failed(err); goto done; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.count = 0;
    /* IDF orders by RSSI. Collapse mesh/multiple BSSID entries to one SSID. */
    for (unsigned i = 0; i < count; ++i) {
        if (!records[i].ssid[0]) continue;
        bool duplicate = false;
        for (int j = 0; j < s_status.count; ++j)
            if (!strncmp(s_status.aps[j].ssid, (char *)records[i].ssid, 32)) duplicate = true;
        if (duplicate) continue;
        hisu_wifi_ap_t candidate;
        hisu_wifi_ap_t *ap = &candidate;
        memset(ap, 0, sizeof(*ap));
        memcpy(ap->ssid, records[i].ssid, 32);
        ap->rssi = records[i].rssi;
        ap->secure = records[i].authmode != WIFI_AUTH_OPEN;
        char saved_password[65] = {0};
        ap->saved = hisu_settings_find_wifi(ap->ssid, saved_password) == ESP_OK;
        memset(saved_password, 0, sizeof(saved_password));
        ap->supported = records[i].authmode == WIFI_AUTH_OPEN || records[i].authmode == WIFI_AUTH_WPA2_PSK ||
            records[i].authmode == WIFI_AUTH_WPA_WPA2_PSK || records[i].authmode == WIFI_AUTH_WPA3_PSK ||
            records[i].authmode == WIFI_AUTH_WPA2_WPA3_PSK;
        if (s_status.count < HISU_WIFI_MAX_APS) s_status.aps[s_status.count++] = candidate;
        else {
            hisu_wifi_ap_t *last = &s_status.aps[s_status.count - 1];
            if (candidate.saved < last->saved ||
                (candidate.saved == last->saved && candidate.rssi <= last->rssi)) continue;
            *last = candidate;
        }
        sort_access_points();
    }
    sort_access_points();
    s_status.scanning = false;
    s_status.error = ESP_OK;
    ++s_status.revision;
    xSemaphoreGive(s_lock);
    if (!connected && !resume_connection) {
        /* The list is ordered by signal strength within the saved group.
         * Never replace an explicit join, roam a connected station, or use
         * credentials from an unsuccessful provisioning attempt. */
        for (int i = 0; i < s_status.count; ++i) {
            hisu_wifi_ap_t candidate;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            candidate = s_status.aps[i];
            xSemaphoreGive(s_lock);
            if (!candidate.saved || !candidate.supported) continue;
            char password[65] = {0};
            esp_err_t saved = hisu_settings_find_wifi(candidate.ssid, password);
            /* A saved protected SSID must not auto-join an open lookalike. */
            bool usable = hisu_wifi_can_auto_join(saved == ESP_OK, candidate.supported,
                                                 candidate.secure, password);
            if (usable) {
                memcpy(s_ssid, candidate.ssid, sizeof(s_ssid));
                memcpy(s_password, password, sizeof(s_password));
            }
            memset(password, 0, sizeof(password));
            if (!usable) continue;
            s_save_candidate = false;
            s_retries = 0;
            s_want_connection = true;
            begin_join();
            break;
        }
    }
done:
    if (resume_connection) {
        /* Leave time to select a different network before the next retry. */
        xEventGroupClearBits(s_events, EVENT_IP | EVENT_DISCONNECTED);
        s_want_connection = true;
        s_retry_at = esp_timer_get_time() + 10000000;
    }
}

static void worker(void *arg)
{
    (void)arg;
    const int64_t auto_scan_interval_us = 30000000;
    int64_t next_auto_scan = 0;
    if (s_status.enabled) {
        esp_err_t err = radio_start();
        if (err != ESP_OK) failed(err);
        else scan();
        next_auto_scan = esp_timer_get_time() + auto_scan_interval_us;
    }
    for (;;) {
        command_t command;
        if (xQueueReceive(s_commands, &command, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (command.type == CMD_SETUP) setup_radio(command.enabled);
            else if (command.type == CMD_SCAN) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                bool enabled = s_status.enabled;
                xSemaphoreGive(s_lock);
                if (command.enabled && !enabled) {
                    esp_err_t err = hisu_settings_set(HISU_SETTING_WIFI_ENABLED, 1);
                    if (err == ESP_OK) err = radio_mode(true, s_status.setup_enabled);
                    if (err != ESP_OK) { failed(err); continue; }
                    xSemaphoreTake(s_lock, portMAX_DELAY);
                    s_status.enabled = true;
                    xSemaphoreGive(s_lock);
                }
                scan();
                next_auto_scan = esp_timer_get_time() + auto_scan_interval_us;
            }
            else if (command.type == CMD_JOIN) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                bool enabled = s_status.enabled;
                xSemaphoreGive(s_lock);
                if (!enabled && command.enabled) {
                    esp_err_t err = hisu_settings_set(HISU_SETTING_WIFI_ENABLED, 1);
                    if (err == ESP_OK) err = radio_mode(true, s_status.setup_enabled);
                    if (err != ESP_OK) { failed(err); memset(&command, 0, sizeof(command)); continue; }
                    xSemaphoreTake(s_lock, portMAX_DELAY);
                    s_status.enabled = true;
                    xSemaphoreGive(s_lock);
                    enabled = true;
                }
                if (!enabled) {
                    memset(&command, 0, sizeof(command));
                    failed(ESP_ERR_INVALID_STATE); continue;
                }
                memcpy(s_ssid, command.ssid, sizeof(s_ssid));
                memcpy(s_password, command.password, sizeof(s_password));
                s_save_candidate = true;
                s_retries = 0;
                s_want_connection = true;
                begin_join();
            } else {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                bool previous_enabled = s_status.enabled;
                xSemaphoreGive(s_lock);
                if (previous_enabled == command.enabled) continue;
                esp_err_t err = hisu_settings_set(HISU_SETTING_WIFI_ENABLED, command.enabled);
                if (err == ESP_OK) {
                    s_want_connection = false; s_deadline = s_retry_at = 0;
                    s_save_candidate = false;
                    err = radio_mode(command.enabled, s_status.setup_enabled);
                    if (err == ESP_OK && !command.enabled && s_started && !s_status.setup_enabled) {
                        err = esp_wifi_stop();
                        if (err == ESP_OK) s_started = false;
                    } else if (err == ESP_OK && command.enabled) err = radio_start();
                    if (err == ESP_OK) {
                        xSemaphoreTake(s_lock, portMAX_DELAY);
                        s_status.enabled = command.enabled;
                        s_status.connected = false; s_status.joining = false; s_status.ip[0] = 0;
                        ++s_status.revision;
                        xSemaphoreGive(s_lock);
                    } else hisu_settings_set(HISU_SETTING_WIFI_ENABLED, previous_enabled);
                    if (command.enabled && err == ESP_OK) {
                        scan();
                        next_auto_scan = esp_timer_get_time() + auto_scan_interval_us;
                    }
                }
                if (err != ESP_OK) failed(err);
            }
            memset(&command, 0, sizeof(command));
        }
        EventBits_t events = xEventGroupClearBits(s_events, EVENT_IP | EVENT_DISCONNECTED);
        wifi_ap_record_t ap;
        if ((events & EVENT_IP) && s_want_connection && esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
            !strncmp((char *)ap.ssid, s_ssid, 32)) {
            esp_netif_ip_info_t ip;
            esp_err_t err = esp_netif_get_ip_info(s_netif, &ip);
            bool saved = false;
            if (err == ESP_OK && s_save_candidate) {
                err = hisu_settings_save_wifi(s_ssid, s_password);
                s_save_candidate = err != ESP_OK;
                saved = err == ESP_OK;
            }
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (saved) {
                for (int i = 0; i < s_status.count; ++i) {
                    char saved_password[65] = {0};
                    s_status.aps[i].saved = hisu_settings_find_wifi(s_status.aps[i].ssid, saved_password) == ESP_OK;
                    memset(saved_password, 0, sizeof(saved_password));
                }
                sort_access_points();
            }
            s_status.connected = true; s_status.joining = false; s_status.error = err;
            s_status.rssi = ap.rssi;
            if (esp_netif_get_ip_info(s_netif, &ip) == ESP_OK)
                snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&ip.ip));
            ++s_status.revision;
            xSemaphoreGive(s_lock);
            s_retries = 0; s_deadline = 0; s_retry_at = 0;
            hisu_clock_network_connected();
            events &= ~EVENT_DISCONNECTED;
        }
        if ((events & EVENT_DISCONNECTED) && s_want_connection) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            int reason = s_status.disconnect_reason;
            if (reason == WIFI_REASON_ASSOC_LEAVE && s_deadline &&
                esp_timer_get_time() < s_deadline - 24500000) {
                xSemaphoreGive(s_lock);
                continue; /* late completion of our explicit disconnect */
            }
            s_status.connected = false; s_status.joining = true; s_status.ip[0] = 0;
            ++s_status.revision;
            xSemaphoreGive(s_lock);
            if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                reason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
                s_want_connection = false; s_deadline = 0; s_retry_at = 0;
                failed(ESP_ERR_INVALID_ARG);
            } else if (!s_retry_at) s_retry_at = esp_timer_get_time() + (1000000LL << (s_retries < 5 ? s_retries : 5));
        }
        int64_t now = esp_timer_get_time();
        static int64_t next_signal_poll;
        if (now >= next_signal_poll) {
            next_signal_poll = now + 5000000;
            if (s_want_connection && !s_deadline && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_status.rssi = ap.rssi;
                xSemaphoreGive(s_lock);
            }
        }
        if (s_want_connection && ((s_deadline && now >= s_deadline) || (s_retry_at && now >= s_retry_at))) {
            if (++s_retries > 6) {
                s_want_connection = false; s_deadline = 0; s_retry_at = 0;
                esp_wifi_disconnect(); failed(ESP_ERR_TIMEOUT);
            } else begin_join();
        }
        /* A saved AP may come online after boot or after reconnect attempts end.
         * Scan only while idle; a manual join or live connection takes priority. */
        if (now >= next_auto_scan && !s_want_connection) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bool should_scan = s_status.enabled && !s_status.connected &&
                !s_status.joining && !s_status.scanning;
            xSemaphoreGive(s_lock);
            next_auto_scan = esp_timer_get_time() + auto_scan_interval_us;
            if (should_scan) scan();
        }
    }
}

esp_err_t hisu_network_start(void)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;
    bool wifi_initialized = false;
    s_lock = xSemaphoreCreateMutex();
    s_commands = xQueueCreate(4, sizeof(command_t));
    s_events = xEventGroupCreate();
    esp_err_t err = ESP_ERR_NO_MEM;
    if (!s_lock || !s_commands || !s_events) goto fail;
    err = esp_netif_init();
    if (err != ESP_OK) goto fail;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;
    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) { err = ESP_ERR_NO_MEM; goto fail; }
    s_setup_netif = esp_netif_create_default_wifi_ap();
    if (!s_setup_netif) { err = ESP_ERR_NO_MEM; goto fail; }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    wifi_initialized = err == ESP_OK;
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) {
        /* Keep the dormant AP profile password-protected even during mode changes. */
        wifi_config_t closed = {0};
        memcpy(closed.ap.ssid, "Hisu-Setup-Closed", 17);
        uint8_t random[16]; esp_fill_random(random, sizeof(random));
        for (unsigned i = 0; i < sizeof(random); ++i)
            snprintf((char *)closed.ap.password + i * 2, 3, "%02x", random[i]);
        closed.ap.authmode = WIFI_AUTH_WPA2_PSK;
        closed.ap.ssid_hidden = 1; closed.ap.max_connection = 1;
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &closed);
        if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
        memset(&closed, 0, sizeof(closed)); memset(random, 0, sizeof(random));
    }
    if (err == ESP_OK) err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, &s_wifi_handler);
    if (err == ESP_OK) err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, &s_ip_handler);
    if (err != ESP_OK) goto fail;
    hisu_settings_get_wifi(s_ssid, s_password);
    s_status.enabled = hisu_settings_get(HISU_SETTING_WIFI_ENABLED) == 1;
    s_status.ready = true;
    s_status.scanning = s_status.enabled;
    memcpy(s_status.ssid, s_ssid, sizeof(s_ssid));
    if (xTaskCreate(worker, "hisu_network", 8192, NULL, 3, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM; goto fail;
    }
    return ESP_OK;
fail:
    if (s_wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
    if (s_ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
    s_wifi_handler = NULL; s_ip_handler = NULL;
    if (wifi_initialized) esp_wifi_deinit();
    if (s_netif) esp_netif_destroy_default_wifi(s_netif);
    if (s_setup_netif) esp_netif_destroy_default_wifi(s_setup_netif);
    s_setup_netif = NULL;
    s_netif = NULL;
    if (s_commands) vQueueDelete(s_commands);
    if (s_events) vEventGroupDelete(s_events);
    if (s_lock) vSemaphoreDelete(s_lock);
    s_commands = NULL; s_events = NULL; s_lock = NULL;
    s_status = (hisu_network_status_t){0};
    memset(s_ssid, 0, sizeof(s_ssid)); memset(s_password, 0, sizeof(s_password));
    return err;
}

void hisu_network_get(hisu_network_status_t *out)
{
    if (!out) return;
    *out = (hisu_network_status_t){0};
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY); *out = s_status; xSemaphoreGive(s_lock);
}

static esp_err_t send_command(command_t *command)
{
    if (!s_lock || !s_status.ready) return ESP_ERR_INVALID_STATE;
    return xQueueSend(s_commands, command, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t request_scan(bool enable)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_status.ready || (!s_status.enabled && !enable) || s_status.scanning) {
        xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE;
    }
    s_status.scanning = true; ++s_status.revision;
    xSemaphoreGive(s_lock);
    command_t command = {.type = CMD_SCAN, .enabled = enable};
    esp_err_t err = send_command(&command);
    if (err != ESP_OK) failed(err);
    return err;
}
esp_err_t hisu_network_scan(void) { return request_scan(false); }
esp_err_t hisu_network_provision_scan(void) { return request_scan(true); }

static esp_err_t submit_join(const char *ssid, const char *password, bool provision)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || !hisu_network_valid_password(password)) return ESP_ERR_INVALID_ARG;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_status.ready || (!s_status.enabled && !provision) || s_status.scanning || (s_status.joining && !provision)) {
        xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE;
    }
    s_status.joining = true; ++s_status.revision;
    xSemaphoreGive(s_lock);
    command_t command = {.type = CMD_JOIN, .enabled = provision};
    memcpy(command.ssid, ssid, strlen(ssid)); memcpy(command.password, password, strlen(password));
    esp_err_t err = send_command(&command);
    memset(&command, 0, sizeof(command));
    if (err != ESP_OK) failed(err);
    return err;
}

esp_err_t hisu_network_join(const char *ssid, const char *password) { return submit_join(ssid, password, false); }
esp_err_t hisu_network_provision(const char *ssid, const char *password) { return submit_join(ssid, password, true); }

esp_err_t hisu_network_enable(bool enabled)
{
    command_t command = {.type = CMD_ENABLE, .enabled = enabled};
    return send_command(&command);
}

esp_err_t hisu_network_setup(bool enabled)
{
    command_t command = {.type = CMD_SETUP, .enabled = enabled};
    return send_command(&command);
}
