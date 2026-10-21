#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool weather, news;
    char place[97], type[17];
    int day;
    int64_t location_id;
} lookup_query_t;
bool lookup_query_parse(const cJSON *args,lookup_query_t *query);
cJSON *lookup_error(const char *code);
cJSON *lookup_gold(const cJSON *root,const lookup_query_t *query,int64_t now);
/* Returns a selected, validated VN location, or a structured clarification. */
const cJSON *lookup_location(const cJSON *root,const lookup_query_t *query,cJSON **error);
cJSON *lookup_weather(const cJSON *root,const cJSON *location,const lookup_query_t *query,int64_t now);
