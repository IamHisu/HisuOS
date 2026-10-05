#include "hisu_clock.h"
#include "hisu_settings.h"
#include "esp_netif_sntp.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
static bool s_sntp_ready;
bool hisu_clock_vietnam_date(int *day, int *month, int *year)
{
    time_t now = time(NULL);
    if (now < 1704067200LL || now >= 4102444800LL) return false;
    now += 7*3600;
    struct tm date;
    if (!gmtime_r(&now, &date) || date.tm_year+1900 > 2099) return false;
    *day = date.tm_mday; *month = date.tm_mon+1; *year = date.tm_year+1900;
    return true;
}
esp_err_t hisu_clock_start(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.start = false;
    config.wait_for_sync = false;
    esp_err_t err = esp_netif_sntp_init(&config);
    s_sntp_ready = err == ESP_OK;
    return err;
}
void hisu_clock_network_connected(void)
{
    if (s_sntp_ready) esp_netif_sntp_start();
}
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
