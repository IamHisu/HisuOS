#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HISU_MEDIA_PIXEL_BYTES 115200U
#define HISU_MEDIA_SLOT_BYTES 122880U
#define HISU_MEDIA_DATA_OFFSET 4096U
#define HISU_MEDIA_MAGIC 0x31424748U

typedef struct {
    uint32_t magic, version, sequence, width, height, length, crc, flags;
    uint32_t header_crc, reserved[7];
} hisu_media_record_t;

uint32_t hisu_media_crc(const void *data, size_t size);
uint32_t hisu_media_crc_update(uint32_t crc, const void *data, size_t size);
bool hisu_media_record_header_valid(const hisu_media_record_t *record);
bool hisu_media_record_valid(const hisu_media_record_t *record, const void *pixels);
bool hisu_media_secret_equal(const char *a, const char *b, size_t length);
bool hisu_media_http_host_valid(const char *host, const char *ip);
bool hisu_media_http_origin_valid(const char *origin, const char *ip);
bool hisu_media_ipv4_mapped(const uint8_t address[16], uint32_t *ipv4);
bool hisu_media_session_should_close(int64_t now, int64_t web_deadline,
    int64_t leave_deadline, int64_t disconnected_at);
