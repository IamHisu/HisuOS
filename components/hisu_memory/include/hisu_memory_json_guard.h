#pragma once
#include <stdbool.h>
/* Bound recursive parser stack before cJSON sees model-generated JSON. */
static inline bool hisu_memory_json_shallow(const char *text,unsigned maximum)
{
    if (!text) return false;
    unsigned depth=0; bool quoted=false,escape=false;
    for (const unsigned char *p=(const unsigned char *)text;*p;++p) {
        if (quoted) {
            if (*p<32) return false;
            if (escape) escape=false;
            else if (*p=='\\') escape=true;
            else if (*p=='"') quoted=false;
        } else if (*p=='"') quoted=true;
        else if (*p=='{' || *p=='[') { if (++depth>maximum) return false; }
        else if (*p=='}' || *p==']') { if (!depth) return false; --depth; }
    }
    return !quoted && !depth;
}
