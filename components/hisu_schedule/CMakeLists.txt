idf_component_register(SRCS "src/hisu_schedule.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES hisu_memory esp_partition heap freertos json)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=frame-larger-than=2048)
