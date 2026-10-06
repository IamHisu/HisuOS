#include "hisu_setup_dns.h"
#include <string.h>

size_t hisu_setup_dns_reply(const uint8_t *query, size_t length,
                          const uint8_t ip[4], uint8_t *out, size_t capacity)
{
    if (!query || !ip || !out || length < 17 || length > 512 ||
        (query[2] & 0xfe) || query[4] || query[5] != 1 ||
        query[6] || query[7] || query[8] || query[9] || query[10] || query[11]) return 0;
    size_t pos = 12, name_length = 0;
    while (pos < length && query[pos]) {
        unsigned label = query[pos++];
        if (label > 63 || label > length - pos) return 0;
        pos += label;
        name_length += label + 1;
        if (name_length > 253) return 0;
    }
    if (pos >= length || length - ++pos != 4 || query[pos + 2] || query[pos + 3] != 1) return 0;
    unsigned type = ((unsigned)query[pos] << 8) | query[pos + 1];
    if (type != 1 && type != 28) return 0;
    size_t result = length + (type == 1 ? 16 : 0);
    if (result > capacity) return 0;
    memcpy(out, query, length);
    out[2] = (uint8_t)(0x80 | (query[2] & 1));
    out[3] = 0; /* No recursion: this is a local captive responder. */
    out[6] = 0; out[7] = type == 1 ? 1 : 0;
    if (type == 1) {
        static const uint8_t answer[] = {0xc0,0x0c,0,1,0,1,0,0,0,30,0,4};
        memcpy(out + length, answer, sizeof(answer));
        memcpy(out + length + sizeof(answer), ip, 4);
    }
    return result;
}
