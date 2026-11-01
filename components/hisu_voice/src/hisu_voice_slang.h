#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

/* Same live_send owner as build_setup; no extra permanent task. */
bool voice_slang_poll(bool (*idle)(void));
char *voice_slang_context(void);
/* Pure validation/extraction helpers for the open dictionary API. */
bool voice_slang_definition(const cJSON *root,char out[321]);
bool voice_slang_snapshot_valid(const cJSON *root,int64_t now);
