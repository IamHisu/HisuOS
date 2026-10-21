#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { char line[256]; size_t used; bool discard, cr, control; } voice_console_t;
enum { VOICE_CONSOLE_NONE, VOICE_CONSOLE_LINE, VOICE_CONSOLE_INVALID, VOICE_CONSOLE_CONTROL };
#define VOICE_CONSOLE_MIC_LEASE_MS 5000
static inline bool voice_console_mic_muted(int64_t until, int64_t now) { return until>now; }

static inline bool voice_console_valid(const char *text)
{
    if (!text || !text[0]) return false;
    size_t pos=0;
    while (text[pos]) {
        if (pos>=255) return false;
        unsigned char c=(unsigned char)text[pos++];
        if (c<0x80) { if (c<32 || c==127) return false; continue; }
        unsigned remaining, code, minimum;
        if (c>=0xc2 && c<=0xdf) { remaining=1; code=c&31; minimum=0x80; }
        else if (c>=0xe0 && c<=0xef) { remaining=2; code=c&15; minimum=0x800; }
        else if (c>=0xf0 && c<=0xf4) { remaining=3; code=c&7; minimum=0x10000; }
        else return false;
        while (remaining--) {
            if (pos>=255) return false;
            unsigned char next=(unsigned char)text[pos++];
            if ((next&0xc0)!=0x80) return false;
            code=(code<<6)|(next&63);
        }
        if (code<minimum || code>0x10ffff || (code>=0xd800 && code<=0xdfff)) return false;
    }
    return true;
}

/* Reject the entire oversized/invalid line, never submit a truncated command. */
static inline int voice_console_feed(voice_console_t *s, unsigned char c)
{
    bool cr=s->cr; s->cr=c=='\r';
    if (c=='\n' && cr) return VOICE_CONSOLE_NONE;
    if (c=='\r' || c=='\n') {
        s->line[s->used]=0;
        bool invalid=s->discard || (s->used && !voice_console_valid(s->line));
        bool present=s->used!=0, control=s->control; s->used=0; s->discard=s->control=false;
        return invalid ? VOICE_CONSOLE_INVALID : present ?
            (control ? VOICE_CONSOLE_CONTROL : VOICE_CONSOLE_LINE) : VOICE_CONSOLE_NONE;
    }
    if (s->discard) return VOICE_CONSOLE_NONE;
    if (c==0x1e && !s->used && !s->control) { s->control=true; return VOICE_CONSOLE_NONE; }
    if (c==8 || c==127) {
        if (s->used) {
            do { --s->used; } while (s->used && ((unsigned char)s->line[s->used]&0xc0)==0x80);
        }
    } else if (c<32 || s->used>=sizeof(s->line)-1) s->discard=true;
    else s->line[s->used++]=(char)c;
    return VOICE_CONSOLE_NONE;
}
