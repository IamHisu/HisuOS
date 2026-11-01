#pragma once
#include "hisu_voice_lookup_data.h"

#define HISU_WEB_BODY_LIMIT 65536U
/* Decode only completed, searched answers with citations and search suggestions. */
cJSON *lookup_web_result(const cJSON *root, int64_t now);
char *lookup_web_request(const lookup_query_t *query, int64_t now);
