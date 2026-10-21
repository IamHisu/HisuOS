#pragma once
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

#define HISU_NEWS_BODY_LIMIT 262144U
/* Bounded Google News RSS, no article fetching or interpretation of HTML. */
cJSON *lookup_news(const char *xml,size_t bytes,int64_t now);
