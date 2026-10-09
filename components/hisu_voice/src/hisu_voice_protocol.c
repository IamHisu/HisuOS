#include "hisu_voice_protocol.h"
#include <string.h>

bool voice_reply_append(voice_reply_t *reply, const cJSON *event)
{
    const cJSON *candidate = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(event, "candidates"), 0);
    const cJSON *reason = cJSON_GetObjectItemCaseSensitive(candidate, "finishReason");
    if (cJSON_IsString(reason)) {
        if (strcmp(reason->valuestring, "STOP")) return false;
        reply->complete = true;
    }
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(candidate, "content");
    const cJSON *parts = cJSON_GetObjectItemCaseSensitive(content, "parts");
    const cJSON *part;
    cJSON_ArrayForEach(part, parts) {
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(part, "thought"))) continue;
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(part, "text");
        if (!cJSON_IsString(text)) continue;
        size_t used = strlen(reply->text), extra = strlen(text->valuestring);
        if (extra >= 256 - used) return false;
        memcpy(reply->text + used, text->valuestring, extra + 1);
    }
    return true;
}

voice_audio_event_t voice_audio_event(const cJSON *event, const char **encoded)
{
    *encoded = NULL;
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(event, "event_type");
    if (!cJSON_IsString(type)) return VOICE_AUDIO_ERROR;
    if (!strcmp(type->valuestring, "interaction.completed")) return VOICE_AUDIO_DONE;
    if (!strcmp(type->valuestring, "interaction.failed") || !strcmp(type->valuestring, "error") ||
        !strcmp(type->valuestring, "interaction.cancelled") || !strcmp(type->valuestring, "interaction.incomplete") ||
        !strcmp(type->valuestring, "interaction.requires_action")) return VOICE_AUDIO_ERROR;
    if (strcmp(type->valuestring, "step.delta")) return VOICE_AUDIO_IGNORE;
    const cJSON *delta = cJSON_GetObjectItemCaseSensitive(event, "delta");
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(delta, "type");
    if (!cJSON_IsString(kind)) return VOICE_AUDIO_ERROR;
    if (strcmp(kind->valuestring, "audio")) return VOICE_AUDIO_IGNORE;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(delta, "data");
    if (!cJSON_IsString(data)) return VOICE_AUDIO_ERROR;
    const cJSON *mime = cJSON_GetObjectItemCaseSensitive(delta, "mime_type");
    if (cJSON_IsString(mime) && strcmp(mime->valuestring, "audio/l16") &&
        strcmp(mime->valuestring, "audio/l16;rate=24000")) return VOICE_AUDIO_ERROR;
    *encoded = data->valuestring;
    return VOICE_AUDIO_DATA;
}
