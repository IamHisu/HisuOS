#include "hisu_clock.h"
#include "hisu_settings.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
static atomic_bool s_sntp_ready;
static atomic_bool s_sntp_started;
static void clock_synced(struct timeval *tv)
{
    ESP_LOGI("CLOCK","NTP synchronized: utc=%lld uptime_ms=%lld",
        (long long)tv->tv_sec,(long long)(esp_timer_get_time()/1000));
}
static bool date_at_offset(int *day, int *month, int *year, int offset_minutes)
{
    /* The system clock keeps ticking without Wi-Fi after NTP/phone sync. */
    time_t now = time(NULL);
    if (now < 1704067200LL || now >= 4102444800LL) return false;
    now += offset_minutes*60;
    struct tm date;
    if (!gmtime_r(&now, &date) || date.tm_year+1900 > 2099) return false;
    *day = date.tm_mday; *month = date.tm_mon+1; *year = date.tm_year+1900;
    return true;
}
bool hisu_clock_vietnam_date(int *day, int *month, int *year) { return date_at_offset(day,month,year,420); }
bool hisu_clock_local_date(int *day, int *month, int *year)
{
    return date_at_offset(day,month,year,hisu_settings_get(HISU_SETTING_TIMEZONE));
}
esp_err_t hisu_clock_start(void)
{
#if CONFIG_LWIP_SNTP_MAX_SERVERS >= 3
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST("time.google.com", "time.cloudflare.com", "pool.ntp.org"));
#else
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.google.com");
#endif
    config.start = false;
    config.wait_for_sync = false;
    config.sync_cb = clock_synced;
    esp_err_t err = esp_netif_sntp_init(&config);
    atomic_store(&s_sntp_ready,err == ESP_OK);
    return err;
}
void hisu_clock_network_connected(void)
{
    if (!atomic_load(&s_sntp_ready)) return;
    bool expected=false;
    /* GOT_IP and the worker/startup fallback can observe the same connection. */
    if (!atomic_compare_exchange_strong(&s_sntp_started,&expected,true)) return;
    esp_err_t err=esp_netif_sntp_start();
    if (err!=ESP_OK) atomic_store(&s_sntp_started,false);
    ESP_LOGI("CLOCK","NTP requested after IP: result=%s uptime_ms=%lld",
        esp_err_to_name(err),(long long)(esp_timer_get_time()/1000));
}
void hisu_clock_network_disconnected(void) { atomic_store(&s_sntp_started,false); }
esp_err_t hisu_clock_set(uint64_t utc_seconds, int offset_minutes)
{
    if (utc_seconds < 1704067200ULL || utc_seconds >= 4102444800ULL ||
        offset_minutes < -720 || offset_minutes > 840) return ESP_ERR_INVALID_ARG;
    esp_err_t err = hisu_settings_set(HISU_SETTING_TIMEZONE, offset_minutes);
    if (err != ESP_OK) return err;
    struct timeval value = {.tv_sec = (time_t)utc_seconds};
    return settimeofday(&value, NULL) == 0 ? ESP_OK : ESP_FAIL;
}
bool hisu_clock_read(char text[6])
{
    time_t now = time(NULL);
    if (now < 1704067200LL || now >= 4102444800LL) { snprintf(text, 6, "--:--"); return false; }
    now += hisu_settings_get(HISU_SETTING_TIMEZONE) * 60;
    struct tm local;
    if (!gmtime_r(&now, &local)) { snprintf(text, 6, "--:--"); return false; }
    strftime(text, 6, "%H:%M", &local);
    return true;
}
