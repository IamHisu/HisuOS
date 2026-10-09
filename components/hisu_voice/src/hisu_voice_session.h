#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { VOICE_IDLE, VOICE_WAITING, VOICE_CAPTURING, VOICE_PROCESSING, VOICE_SPEAKING } voice_state_t;
typedef struct {
    voice_state_t state;
    uint32_t generation;
    uint32_t session_id;
    int64_t ready_ms;
    int64_t deadline_ms;
} voice_session_t;

void voice_session_open(voice_session_t *session, int64_t now_ms);
void voice_session_close(voice_session_t *session);
bool voice_session_current(const voice_session_t *session, uint32_t generation);
bool voice_session_capture(voice_session_t *session, int64_t now_ms);
bool voice_session_finish(voice_session_t *session, uint32_t generation, int64_t now_ms);
bool voice_session_expire(voice_session_t *session, int64_t now_ms);
bool voice_session_end_phrase(const char *text);
