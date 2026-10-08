#include "hisu_app.h"
#include "hisu_board.h"
#include "hisu_settings.h"
#include "hisu_ui.h"
#include "hisu_network.h"
#include "hisu_power.h"
#include "hisu_clock.h"
#include "hisu_media.h"
#include "hisu_voice.h"
#include "esp_log.h"

void hisu_app_start(void)
{
    esp_err_t err = hisu_board_prepare_boot();
    if (err == ESP_OK) err = hisu_settings_init();
    if (err == ESP_OK) {
        esp_err_t service_err = hisu_network_start();
        if (service_err != ESP_OK) ESP_LOGE("APP", "Network startup: %s", esp_err_to_name(service_err));
        service_err = hisu_clock_start();
        if (service_err != ESP_OK) ESP_LOGW("APP", "Clock startup: %s", esp_err_to_name(service_err));
        hisu_network_status_t network; hisu_network_get(&network);
        if (network.connected) hisu_clock_network_connected();
        service_err = hisu_power_start();
        if (service_err != ESP_OK) ESP_LOGE("APP", "Power startup: %s", esp_err_to_name(service_err));
        service_err = hisu_media_start();
        if (service_err != ESP_OK) ESP_LOGE("APP", "Media startup: %s", esp_err_to_name(service_err));
    }
    if (err == ESP_OK) err = hisu_ui_start();
    if (err == ESP_OK) {
        esp_err_t voice_err = hisu_voice_start();
        if (voice_err != ESP_OK) {
            ESP_LOGE("APP", "Microphone startup: %s", esp_err_to_name(voice_err));
            const char *message = voice_err == ESP_ERR_NOT_FOUND ? "Thiếu model Hey Hisu" :
                                  voice_err == ESP_ERR_INVALID_ARG ? "Hey Hisu không hợp lệ" :
                                  "Mic / nhận giọng nói lỗi";
            (void)hisu_ui_post_status(HISU_UI_ERROR, message);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE("APP", "UI startup failed: %s", esp_err_to_name(err));
    }
}
