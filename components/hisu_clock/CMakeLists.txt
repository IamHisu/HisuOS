idf_component_register(SRCS "src/hisu_clock.c" "src/hisu_calendar.c" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES hisu_settings esp_netif)
