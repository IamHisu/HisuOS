#include "hisu_app.h"
#include "hisu_settings.h"
#include "hisu_ui.h"
#include "hisu_network.h"
#include "hisu_power.h"
#include "esp_log.h"

void hisu_app_start(void)
{
    esp_err_t err = hisu_settings_init();
    if (err == ESP_OK) {
        esp_err_t service_err = hisu_network_start();
        if (service_err != ESP_OK) ESP_LOGE("APP", "Network startup: %s", esp_err_to_name(service_err));
        service_err = hisu_power_start();
        if (service_err != ESP_OK) ESP_LOGE("APP", "Power startup: %s", esp_err_to_name(service_err));
    }
    if (err == ESP_OK) err = hisu_ui_start();
    if (err != ESP_OK) {
        ESP_LOGE("APP", "UI startup failed: %s", esp_err_to_name(err));
    }
}
