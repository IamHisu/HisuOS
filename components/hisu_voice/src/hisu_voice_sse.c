#include "hisu_voice_sse.h"
#include <string.h>

void voice_sse_init(voice_sse_t *s, char *line, char *data, size_t capacity,
                    voice_sse_event_fn event, void *context)
{
    *s = (voice_sse_t){ .line = line, .data = data, .capacity = capacity,
                        .event = event, .context = context };
}

static bool finish_line(voice_sse_t *s)
{
    size_t n = s->line_length;
    if (n && s->line[n - 1] == '\r') --n;
    s->line[n] = 0;
    if (!n) {
        if (s->has_data) {
            s->data[s->data_length] = 0;
            if (!s->event(s->data, s->data_length, s->context)) return false;
        }
        s->data_length = 0;
        s->has_data = false;
    } else if (n >= 5 && memcmp(s->line, "data:", 5) == 0) {
        size_t start = 5;
        if (s->line[start] == ' ') ++start;
        size_t extra = n - start + (s->has_data ? 1 : 0);
        if (extra >= s->capacity - s->data_length) return false;
        if (s->has_data) s->data[s->data_length++] = '\n';
        memcpy(s->data + s->data_length, s->line + start, n - start);
        s->data_length += n - start;
        s->has_data = true;
    }
    s->line_length = 0;
    return true;
}

bool voice_sse_feed(voice_sse_t *s, const char *bytes, size_t length)
{
    if (s->failed) return false;
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] == '\n') {
            if (!finish_line(s)) { s->failed = true; return false; }
        } else {
            if (s->line_length + 1 >= s->capacity) { s->failed = true; return false; }
            s->line[s->line_length++] = bytes[i];
        }
    }
    return true;
}

bool voice_sse_complete(const voice_sse_t *s)
{
    return !s->failed && !s->has_data && !s->line_length;
}
