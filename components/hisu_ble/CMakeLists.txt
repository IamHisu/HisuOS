idf_component_register(SRCS "src/hisu_ble.c" INCLUDE_DIRS "include" REQUIRES esp_common
    PRIV_REQUIRES bt hisu_network hisu_clock esp_timer esp_hw_support freertos)
