idf_component_register(SRCS "src/hisu_network.c" "src/hisu_wifi_input.c" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES esp_wifi esp_event esp_netif hisu_settings esp_timer freertos)
