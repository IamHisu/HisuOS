#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint8_t header[10];
    size_t used;
    uint32_t skip, discarded;
    unsigned tags;
    bool audio;
} music_id3_t;

/* Strip leading ID3 tags in place, retaining at most ten bytes across reads.
 * Capacity includes room to restore a split, non-ID3 audio prefix. */
static bool music_id3_feed(music_id3_t *s,uint8_t *data,size_t *bytes,size_t capacity)
{
    size_t offset=0, length=*bytes;
    if (length>capacity) return false;
    if (s->audio) return true;
    while (offset<length) {
        if (s->skip) {
            size_t n=length-offset;
            if (n>s->skip) n=s->skip;
            offset+=n; s->skip-=(uint32_t)n; s->discarded+=(uint32_t)n;
            continue;
        }
        size_t n=10-s->used;
        if (n>length-offset) n=length-offset;
        memcpy(s->header+s->used,data+offset,n); s->used+=n; offset+=n;
        if (s->used<10) break;
        if (memcmp(s->header,"ID3",3)) {
            size_t rest=length-offset;
            if (capacity<10 || rest>capacity-10) return false;
            memmove(data+10,data+offset,rest); memcpy(data,s->header,10);
            *bytes=rest+10; s->audio=true; return true;
        }
        unsigned version=s->header[3];
        unsigned reserved=version==2 ? 0x3f : version==3 ? 0x1f : 0x0f;
        if (version<2 || version>4 || s->header[4]==0xff || (s->header[5]&reserved) ||
            ((s->header[6]|s->header[7]|s->header[8]|s->header[9])&0x80) || ++s->tags>4) return false;
        uint32_t size=((uint32_t)s->header[6]<<21)|((uint32_t)s->header[7]<<14)|
            ((uint32_t)s->header[8]<<7)|s->header[9];
        if (version==4 && (s->header[5]&0x10)) size+=10;
        if (size>2U*1024*1024-10 || s->discarded>2U*1024*1024-10-size) return false;
        s->skip=size; s->discarded+=10; s->used=0;
    }
    *bytes=0;
    return true;
}
