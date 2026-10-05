idf_component_register(
    SRCS "src/hisu_app.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES hisu_board hisu_ui hisu_ai hisu_wifi hisu_settings hisu_web
                  esp_timer nvs_flash lvgl
)
