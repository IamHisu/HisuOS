idf_component_register(SRCS "src/hisu_media.c" "src/hisu_media_model.c" "src/hisu_setup_api.c"
    "src/hisu_setup_portal.c" "src/hisu_setup_dns.c"
    EMBED_FILES "web/index.html" "web/app.js" "web/style.css"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_server esp_partition esp_netif esp_timer esp_hw_support esp_wifi
                  hisu_network hisu_settings hisu_clock json freertos lwip)
