idf_component_register(SRCS "src/hisu_board.c" "src/hisu_board_power.c" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES esp_driver_gpio esp_driver_spi
    esp_driver_ledc esp_lcd esp_timer esp_adc esp_system)
target_include_directories(${COMPONENT_LIB} PRIVATE "${PROJECT_DIR}/boards/zhengchen_154")
