#include "hisu_media_tls.h"
#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/sha256.h"

static int random_bytes(void *context, unsigned char *out, size_t size)
{
    (void)context;
    esp_fill_random(out, size);
    return 0;
}

esp_err_t hisu_media_tls_create(hisu_media_tls_t *out)
{
    memset(out, 0, sizeof(*out));
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    unsigned char serial[16];
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
    esp_fill_random(serial,sizeof(serial));
    serial[0] = (serial[0] & 0x7f) | 1;
    int rc = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (!rc) rc = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), random_bytes, NULL);
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    if (!rc) rc = mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial));
    if (!rc) rc = mbedtls_x509write_crt_set_subject_name(&crt, "CN=HisuOS Setting,O=HisuOS");
    if (!rc) rc = mbedtls_x509write_crt_set_issuer_name(&crt, "CN=HisuOS Setting,O=HisuOS");
    if (!rc) rc = mbedtls_x509write_crt_set_validity(&crt, "20260101000000", "20491231235959");
    if (!rc) rc = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
    if (!rc) rc = mbedtls_x509write_crt_set_key_usage(&crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    if (!rc) rc = mbedtls_x509write_crt_pem(&crt, (unsigned char *)out->certificate,
                                          sizeof(out->certificate), random_bytes, NULL);
    if (!rc) rc = mbedtls_pk_write_key_pem(&key, (unsigned char *)out->key, sizeof(out->key));
    mbedtls_x509_crt parsed;
    mbedtls_x509_crt_init(&parsed);
    if (!rc) rc = mbedtls_x509_crt_parse(&parsed, (unsigned char *)out->certificate, strlen(out->certificate) + 1);
    unsigned char digest[32];
    if (!rc) rc = mbedtls_sha256(parsed.raw.p, parsed.raw.len, digest, 0);
    if (!rc) for (unsigned i = 0; i < 32; ++i) snprintf(out->fingerprint + i * 2, 3, "%02X", digest[i]);
    mbedtls_x509_crt_free(&parsed);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    if (rc) memset(out, 0, sizeof(*out));
    return rc ? ESP_FAIL : ESP_OK;
}
