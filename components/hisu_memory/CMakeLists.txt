idf_component_register(SRCS "src/hisu_memory.c" "src/hisu_memory_model.c"
    INCLUDE_DIRS "include" PRIV_REQUIRES esp_partition heap freertos json)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=frame-larger-than=2048)
