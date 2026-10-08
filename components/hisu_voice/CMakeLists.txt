idf_component_register(SRCS "src/hisu_voice.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_i2s esp_driver_spi esp_timer esp-sr hisu_ui hisu_network
                  hisu_settings esp_http_client mbedtls json)
target_include_directories(${COMPONENT_LIB} PRIVATE "${PROJECT_DIR}/boards/zhengchen_154")
