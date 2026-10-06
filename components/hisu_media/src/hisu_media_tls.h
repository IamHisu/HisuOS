#pragma once
#include "esp_err.h"
typedef struct { char certificate[2048], key[1024], fingerprint[65]; } hisu_media_tls_t;
/* Ephemeral per-boot certificate. Never embedded or shared across devices. */
esp_err_t hisu_media_tls_create(hisu_media_tls_t *out);
