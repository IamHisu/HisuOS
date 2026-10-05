idf_component_register(
    SRCS "src/friend_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_wifi
    PRIV_REQUIRES hisu_settings esp_netif esp_event nvs_flash
)
