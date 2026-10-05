idf_component_register(SRCS "src/hisu_ui.c" "src/hisu_ui_model.c" "fonts/font_vietnamese_16.c"
    INCLUDE_DIRS "include" REQUIRES esp_common
    PRIV_REQUIRES lvgl hisu_board hisu_settings esp_timer freertos)
