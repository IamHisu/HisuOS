#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

typedef struct {
    uint32_t epoch, generation;
    char id[128];
    char *response;
} voice_lookup_result_t;

/* One bounded job. Only live_send submits/takes; HTTP never writes to WSS. */
bool voice_lookup_submit(uint32_t epoch,uint32_t generation,const char *id,const cJSON *args,
                         bool (*current)(uint32_t,uint32_t));
voice_lookup_result_t *voice_lookup_take(void);
bool voice_lookup_busy(void);
void voice_lookup_free(voice_lookup_result_t *result);
