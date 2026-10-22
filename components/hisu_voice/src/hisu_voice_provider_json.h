#pragma once
#include "cJSON.h"

/* Provider trees stay in PSRAM. Read them normally, but delete only here;
 * never attach them to Live/tool response trees using the default allocator. */
cJSON *voice_provider_json_parse(const char *text, size_t bytes, const char **end);
void voice_provider_json_delete(cJSON *tree);
