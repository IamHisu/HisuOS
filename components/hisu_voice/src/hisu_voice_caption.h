#pragma once
#include <stddef.h>
#include <string.h>

/* Keep a rolling UTF-8 window rather than freezing after the first 255 bytes. */
static inline void voice_caption_append(char *dst, size_t capacity, const char *text)
{
    if (!capacity) return;
    size_t used = strlen(dst);
    while (*text) {
        size_t n = 1;
        unsigned char c = (unsigned char)*text;
        if (c >= 0xf0) n = 4;
        else if (c >= 0xe0) n = 3;
        else if (c >= 0xc0) n = 2;
        size_t available = 1;
        while (available < n && text[available] &&
               ((unsigned char)text[available] & 0xc0) == 0x80) ++available;
        if (available != n) break;
        if (n >= capacity) { text += n; continue; }
        while (used + n >= capacity) {
            size_t remove = 1;
            while (remove < used && ((unsigned char)dst[remove] & 0xc0) == 0x80) ++remove;
            memmove(dst, dst + remove, used - remove + 1); used -= remove;
        }
        memcpy(dst + used, text, n); used += n; dst[used] = 0; text += n;
    }
}
