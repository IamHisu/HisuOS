#include "hisu_media_model.h"
#include <string.h>

bool hisu_media_session_should_close(int64_t now, int64_t web_deadline,
    int64_t leave_deadline, int64_t disconnected_at)
{
    return (web_deadline && now >= web_deadline) ||
        (leave_deadline && now >= leave_deadline) ||
        (disconnected_at && now - disconnected_at >= 10000000);
}

bool hisu_media_ipv4_mapped(const uint8_t address[16], uint32_t *ipv4)
{
    static const uint8_t prefix[12] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};
    if (!address || !ipv4 || memcmp(address,prefix,sizeof(prefix))) return false;
    /* Preserve network byte order, matching esp_netif's IPv4 addresses. */
    memcpy(ipv4,address + sizeof(prefix),sizeof(*ipv4));
    return true;
}

bool hisu_media_http_host_valid(const char *host, const char *ip)
{
    if (!host || !ip || !ip[0]) return false;
    size_t length = strlen(ip);
    return !strcmp(host,ip) || (!strncmp(host,ip,length) && !strcmp(host + length,":80"));
}

bool hisu_media_http_origin_valid(const char *origin, const char *ip)
{
    return origin && ip && ip[0] && !strncmp(origin,"http://",7) && !strcmp(origin + 7,ip);
}

_Static_assert(sizeof(hisu_media_record_t) == 64, "Storage record size");

uint32_t hisu_media_crc_update(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *p = data;
    crc = ~crc;
    while (size--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

uint32_t hisu_media_crc(const void *data, size_t size)
{
    return hisu_media_crc_update(0,data,size);
}

bool hisu_media_record_header_valid(const hisu_media_record_t *r)
{
    if (!r || r->magic != HISU_MEDIA_MAGIC || r->version != 1 || !r->sequence ||
        r->width != 240 || r->height != 240 || r->flags > 1) return false;
    if (hisu_media_crc(r, offsetof(hisu_media_record_t, header_crc)) != r->header_crc) return false;
    for (unsigned i = 0; i < 7; ++i) if (r->reserved[i]) return false;
    return r->flags ? r->length == HISU_MEDIA_PIXEL_BYTES : !r->length && !r->crc;
}

bool hisu_media_record_valid(const hisu_media_record_t *r, const void *pixels)
{
    return hisu_media_record_header_valid(r) &&
           (!r->flags || (pixels && hisu_media_crc(pixels, r->length) == r->crc));
}

bool hisu_media_secret_equal(const char *a, const char *b, size_t length)
{
    volatile unsigned difference = 0;
    for (size_t i = 0; i < length; ++i) difference |= (uint8_t)a[i] ^ (uint8_t)b[i];
    return difference == 0;
}
