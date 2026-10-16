idf_component_register(SRCS "src/hisu_alert.c" "src/hisu_alert_model.c"
    INCLUDE_DIRS "include" PRIV_REQUIRES esp_partition hisu_memory freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=frame-larger-than=2048)
