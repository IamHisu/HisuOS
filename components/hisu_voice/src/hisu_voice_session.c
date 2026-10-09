#include "hisu_voice_session.h"
#include <ctype.h>
#include <string.h>

void voice_session_open(voice_session_t *s, int64_t now_ms)
{
    bool active = s->state != VOICE_IDLE;
    if (!active) ++s->session_id;
    ++s->generation;
    s->state = VOICE_WAITING;
    s->ready_ms = now_ms + (active ? 200 : 400);
    s->deadline_ms = s->ready_ms + 15000;
}

void voice_session_close(voice_session_t *s)
{
    ++s->generation;
    s->state = VOICE_IDLE;
    s->deadline_ms = 0;
}

bool voice_session_current(const voice_session_t *s, uint32_t generation)
{
    return s->state != VOICE_IDLE && s->generation == generation;
}

bool voice_session_capture(voice_session_t *s, int64_t now_ms)
{
    if (s->state != VOICE_WAITING || now_ms < s->ready_ms || now_ms >= s->deadline_ms) return false;
    ++s->generation;
    s->state = VOICE_CAPTURING;
    return true;
}

bool voice_session_finish(voice_session_t *s, uint32_t generation, int64_t now_ms)
{
    if (!voice_session_current(s, generation)) return false;
    s->state = VOICE_WAITING;
    s->ready_ms = now_ms + 300;
    s->deadline_ms = s->ready_ms + 3000;
    return true;
}

bool voice_session_expire(voice_session_t *s, int64_t now_ms)
{
    if (s->state != VOICE_WAITING || now_ms < s->deadline_ms) return false;
    voice_session_close(s);
    return true;
}

bool voice_session_end_phrase(const char *text)
{
    char normalized[128];
    size_t length = strlen(text);
    if (length >= sizeof(normalized)) return false;
    while (length && (isspace((unsigned char)text[length - 1]) ||
           strchr(".!?,", text[length - 1]))) --length;
    while (length && isspace((unsigned char)*text)) { ++text; --length; }
    for (size_t i = 0; i < length; ++i) normalized[i] = (char)tolower((unsigned char)text[i]);
    normalized[length] = 0;
    return strcmp(normalized, "thôi nhé") == 0 || strcmp(normalized, "Thôi nhé") == 0 ||
           strcmp(normalized, "thoi nhe") == 0 || strcmp(normalized, "tạm biệt") == 0 ||
           strcmp(normalized, "Tạm biệt") == 0 || strcmp(normalized, "tam biet") == 0 ||
           strcmp(normalized, "kết thúc hội thoại") == 0 || strcmp(normalized, "Kết thúc hội thoại") == 0 ||
           strcmp(normalized, "ket thuc hoi thoai") == 0;
}
