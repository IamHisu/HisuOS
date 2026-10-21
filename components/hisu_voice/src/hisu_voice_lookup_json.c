/* Compile the managed parser in a private namespace, without modifying it. */
#include "hisu_voice_lookup_json.h"
#include <stdlib.h>
#include "esp_heap_caps.h"

static void *lookup_json_allocate(size_t bytes)
{
    return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
static void *lookup_json_resize(void *pointer,size_t bytes)
{
    return heap_caps_realloc(pointer,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}

/* cJSON's default hooks belong to this instance only, fixed to external RAM. */
#define malloc lookup_json_allocate
#define realloc lookup_json_resize
#include "cJSON.c"
#undef malloc
#undef realloc
