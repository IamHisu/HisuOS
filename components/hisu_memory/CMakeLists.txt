idf_component_register(SRCS "src/hisu_memory.c" "src/hisu_memory_model.c"
    INCLUDE_DIRS "include" PRIV_REQUIRES esp_partition heap freertos json)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=frame-larger-than=2048)

set(private_memory "${PROJECT_DIR}/.private/hisu_private_memory.json")
if(EXISTS "${private_memory}")
    target_add_binary_data(${COMPONENT_LIB} "${private_memory}" TEXT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HISU_MEMORY_PRIVATE_SEED=1)
    message(STATUS "Private memory profile enabled: this firmware contains personal data")
endif()
