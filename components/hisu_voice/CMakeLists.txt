idf_component_register(SRCS "src/hisu_voice.c" "src/hisu_voice_session.c" "src/hisu_voice_sse.c"
    "src/hisu_voice_protocol.c" "src/hisu_voice_live.c" "src/hisu_voice_live_message.c" "src/hisu_voice_live_turn.c"
    "src/hisu_voice_music.c" "src/hisu_voice_music_protocol.c" "src/hisu_voice_alert_tts.c" "src/hisu_voice_memory_summary.c"
    "src/hisu_voice_lookup.c" "src/hisu_voice_lookup_data.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_i2s esp_driver_spi esp_timer esp-sr hisu_ui hisu_network
                  hisu_settings hisu_media hisu_memory hisu_power hisu_alert hisu_schedule hisu_clock esp_app_format esp_system esp_rom esp_websocket_client esp_audio_codec esp_http_client tcp_transport mbedtls json lwip)
target_include_directories(${COMPONENT_LIB} PRIVATE "${PROJECT_DIR}/boards/zhengchen_154")
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage -Werror=frame-larger-than=2048)
