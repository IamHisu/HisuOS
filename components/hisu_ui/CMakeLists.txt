idf_component_register(
    SRCS "src/friend_ui.c" "src/friend_media.c" "src/friend_gif.c"
         "src/friend_subtitle.c" "storage/friend_media_store.c"
         "fonts/font_vietnamese_16.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common lvgl
    PRIV_REQUIRES esp_partition nvs_flash
)
