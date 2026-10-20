# sdkconfig.defaults does not override values in an existing sdkconfig.
# Reject old SDK defaults that starve Wi-Fi/TCP while PSRAM is still free.
if(NOT CONFIG_SPIRAM_USE_MALLOC OR
   NOT CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP OR
   CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL GREATER 2048 OR
   CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL LESS 65536 OR
   NOT CONFIG_ESP_WIFI_STATIC_TX_BUFFER OR
   CONFIG_LWIP_TCP_SND_BUF_DEFAULT LESS 16384 OR
   NOT CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC OR CONFIG_MBEDTLS_HARDWARE_AES)
    message(FATAL_ERROR
        "sdkconfig has an obsolete network memory profile. Set "
        "SPIRAM_USE_MALLOC=y, SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y, "
        "SPIRAM_MALLOC_ALWAYSINTERNAL=2048, SPIRAM_MALLOC_RESERVE_INTERNAL=65536, "
        "ESP_WIFI_STATIC_TX_BUFFER=y, LWIP_TCP_SND_BUF_DEFAULT=16384, "
        "MBEDTLS_EXTERNAL_MEM_ALLOC=y and MBEDTLS_HARDWARE_AES=n. "
        "See sdkconfig.defaults for Wi-Fi buffer counts. "
        "Changing sdkconfig.defaults alone does not update an existing sdkconfig.")
endif()
