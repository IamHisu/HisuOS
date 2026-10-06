idf_component_register(SRCS "src/hisu_ui.c" "src/hisu_ui_model.c" "fonts/font_vi_semibold_16.c"
    EMBED_FILES "assets/idle_cat_dark.gif" "assets/idle_cat_light.gif" "assets/boot_windows_xp.gif"
    INCLUDE_DIRS "include" REQUIRES esp_common
    PRIV_REQUIRES lvgl hisu_board hisu_settings hisu_network hisu_power hisu_clock hisu_media esp_timer freertos)
