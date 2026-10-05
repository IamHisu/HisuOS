idf_component_register(
    SRCS "src/friend_ai.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_http_client esp_netif json mbedtls nvs_flash
)
