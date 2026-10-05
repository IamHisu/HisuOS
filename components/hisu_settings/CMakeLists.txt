idf_component_register(SRCS "src/hisu_settings.c" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES nvs_flash freertos)
