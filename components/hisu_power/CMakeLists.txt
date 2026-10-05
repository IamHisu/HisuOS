idf_component_register(SRCS "src/hisu_power.c" "src/hisu_power_model.c" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES hisu_board hisu_settings freertos esp_timer)
