#include "hisu_voice_live_message.h"
int live_message_feed(live_message_t *s, char *buffer, size_t capacity,
                      const char *bytes, int length, unsigned opcode, bool fin,
                      int offset, int payload)
{
    if (length < 0 || offset < 0 || payload < 0 || offset > payload || length > payload - offset ||
        !capacity) return -1;
    if (offset == 0) {
        if (s->offset) return -1;
        if (opcode == 1 || opcode == 2) {
            if (s->active) return -1;
            s->used = 0; s->active = true;
        } else if (opcode != 0 || !s->active) return -1;
        s->payload = payload; s->opcode = opcode;
    } else if (!s->active || offset != s->offset || payload != s->payload || opcode != s->opcode) return -1;
    if (s->used >= capacity || (size_t)length >= capacity - s->used) return -1;
    for (int i = 0; i < length; ++i) buffer[s->used++] = bytes[i];
    s->offset = offset + length;
    if (s->offset == payload) {
        s->offset = 0;
        if (fin) { buffer[s->used] = 0; s->active = false; return 1; }
    }
    return 0;
}
