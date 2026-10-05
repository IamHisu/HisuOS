idf_component_register(
    SRCS "src/board_display.c" "src/board_buttons.c" "src/board_audio.c"
         "src/board_power.c" "src/board_speaker.c"
    INCLUDE_DIRS "include" "${HISU_BOARD_DIR}"
    REQUIRES esp_common lvgl
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_i2s esp_driver_ledc
                  esp_adc esp_lcd esp_timer button
)
