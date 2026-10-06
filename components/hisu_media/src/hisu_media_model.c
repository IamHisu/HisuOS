#include "hisu_media_model.h"
#include <string.h>
static unsigned gif_word(const uint8_t *p) { return p[0] | (unsigned)p[1]<<8; }
static bool gif_blocks(const uint8_t *data, size_t size, size_t *position)
{
    while (*position < size) {
        unsigned length = data[(*position)++];
        if (!length) return true;
        if (length > size-*position) return false;
        *position += length;
    }
    return false;
}
bool hisu_media_gif_valid(const uint8_t *data, size_t size)
{
    if (!data || size < 14 || size > HISU_MEDIA_GIF_STORAGE_MAX_BYTES ||
        (memcmp(data,"GIF89a",6) && memcmp(data,"GIF87a",6))) return false;
    unsigned canvas_width=gif_word(data+6),canvas_height=gif_word(data+8);
    if (!canvas_width || !canvas_height || canvas_width>240 || canvas_height>240) return false;
    size_t pos = 13;
    unsigned frames = 0, global_colors = data[10]&128 ? 2U<<(data[10]&7) : 0;
    if (global_colors && data[11] >= global_colors) return false;
    pos += global_colors*3;
    while (pos < size) {
        unsigned marker = data[pos++];
        if (marker == 0x3b) return frames > 0 && pos == size;
        if (marker == 0x21) {
            if (pos >= size) return false;
            unsigned label = data[pos++];
            if (label == 0xf9) {
                if (size-pos < 6 || data[pos] != 4 || data[pos+5] != 0 ||
                    ((data[pos+1]>>2)&7) > 3) return false;
                pos += 6;
            } else if (!gif_blocks(data,size,&pos)) return false;
        } else if (marker == 0x2c) {
            if (size-pos < 9 || ++frames > 256) return false;
            unsigned x=gif_word(data+pos),y=gif_word(data+pos+2);
            unsigned w=gif_word(data+pos+4),h=gif_word(data+pos+6),flags=data[pos+8];
            if (!w || !h || x+w > canvas_width || y+h > canvas_height) return false;
            pos += 9;
            unsigned colors = flags&128 ? 2U<<(flags&7) : 0;
            if (!colors && !global_colors) return false;
            pos += colors*3;
            if (pos >= size || data[pos] < 2 || data[pos] > 8) return false;
            ++pos;
            if (pos >= size || !data[pos] || !gif_blocks(data,size,&pos)) return false;
        } else return false;
    }
    return false;
}

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
        r->width != 240 || r->height != 240 || r->flags > 2) return false;
    if (hisu_media_crc(r, offsetof(hisu_media_record_t, header_crc)) != r->header_crc) return false;
    for (unsigned i = 0; i < 7; ++i) if (r->reserved[i]) return false;
    return r->flags == 2 ? r->length >= 14 && r->length <= HISU_MEDIA_GIF_STORAGE_MAX_BYTES :
           r->flags ? r->length == HISU_MEDIA_PIXEL_BYTES : !r->length && !r->crc;
}

bool hisu_media_record_valid(const hisu_media_record_t *r, const void *pixels)
{
    return hisu_media_record_header_valid(r) &&
           (!r->flags || (pixels && hisu_media_crc(pixels, r->length) == r->crc &&
            (r->flags != 2 || hisu_media_gif_valid(pixels,r->length))));
}

bool hisu_media_secret_equal(const char *a, const char *b, size_t length)
{
    volatile unsigned difference = 0;
    for (size_t i = 0; i < length; ++i) difference |= (uint8_t)a[i] ^ (uint8_t)b[i];
    return difference == 0;
}
