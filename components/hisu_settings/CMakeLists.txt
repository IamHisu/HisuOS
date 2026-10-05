idf_component_register(
    SRCS "src/friend_wifi_store.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES nvs_flash
)
