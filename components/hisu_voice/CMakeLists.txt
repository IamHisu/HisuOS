idf_component_register(SRCS "src/hisu_voice.c" "src/hisu_voice_session.c" "src/hisu_voice_sse.c"
    "src/hisu_voice_protocol.c" "src/hisu_voice_live.c" "src/hisu_voice_live_message.c" "src/hisu_voice_live_turn.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_i2s esp_driver_spi esp_timer esp-sr hisu_ui hisu_network
                  hisu_settings esp_websocket_client mbedtls json lwip)
target_include_directories(${COMPONENT_LIB} PRIVATE "${PROJECT_DIR}/boards/zhengchen_154")
