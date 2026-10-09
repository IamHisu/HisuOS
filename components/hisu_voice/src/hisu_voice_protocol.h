#pragma once
#include "cJSON.h"
#include <stdbool.h>

typedef struct { char *text; bool complete; } voice_reply_t;
bool voice_reply_append(voice_reply_t *reply, const cJSON *event);
typedef enum { VOICE_AUDIO_IGNORE, VOICE_AUDIO_DATA, VOICE_AUDIO_DONE, VOICE_AUDIO_ERROR } voice_audio_event_t;
voice_audio_event_t voice_audio_event(const cJSON *event, const char **encoded);
