#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef bool (*voice_sse_event_fn)(const char *data, size_t length, void *context);
typedef struct {
    char *line;
    char *data;
    size_t capacity;
    size_t line_length;
    size_t data_length;
    bool has_data;
    bool failed;
    voice_sse_event_fn event;
    void *context;
} voice_sse_t;

void voice_sse_init(voice_sse_t *s, char *line, char *data, size_t capacity,
                    voice_sse_event_fn event, void *context);
bool voice_sse_feed(voice_sse_t *s, const char *bytes, size_t length);
bool voice_sse_complete(const voice_sse_t *s);
