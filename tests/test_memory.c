#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include "memory_mock.h"
#include "../components/hisu_memory/src/hisu_memory.c"

static void reboot(void)
{
    free(s_data); free(s_snapshot); free(s_recent);
    s_data = s_snapshot = NULL; s_recent = NULL; s_lock = NULL;
    mock_flash_budget = -1;
    assert(hisu_memory_init() == ESP_OK);
}
static void reset(void)
{
    memset(mock_flash, 0xff, sizeof(mock_flash));
    reboot();
}
static bool flash_contains(const char *text)
{
    for (size_t i = HISU_MEMORY_OFFSET; i + strlen(text) <= sizeof(mock_flash); ++i)
        if (!memcmp(mock_flash + i, text, strlen(text))) return true;
    return false;
}
static void mutate_during_flash(void)
{
    uint32_t id;
    assert(hisu_memory_remember("New fact during save", 0, &id) == ESP_OK);
}
int main(void)
{
    assert(!hisu_memory_pending());
    assert(hisu_memory_remember("Hello", 0, NULL) == ESP_ERR_INVALID_STATE);
    assert(!hisu_memory_fact_valid(""));
    assert(!hisu_memory_fact_valid(" leading"));
    assert(!hisu_memory_fact_valid("trailing "));
    assert(!hisu_memory_fact_valid("newline\ntext"));
    assert(!hisu_memory_fact_valid("\xc0\xaf"));
    assert(!hisu_memory_fact_valid("\xed\xa0\x80"));
    assert(!hisu_memory_fact_valid("\xf4\x90\x80\x80"));
    assert(hisu_memory_fact_valid("Thich nghe nhac \xe1\xba\xb9"));
    char limit[HISU_MEMORY_FACT_BYTES + 2];
    memset(limit, 'a', sizeof(limit)); limit[256] = 0;
    assert(hisu_memory_fact_valid(limit)); limit[256] = 'a'; limit[257] = 0;
    assert(!hisu_memory_fact_valid(limit));
    reset();
    uint32_t id, same;
    unsigned writes = mock_flash_operations;
    assert(hisu_memory_remember("Likes quiet music", 0, &id) == ESP_OK && id == 1);
    assert(hisu_memory_remember("Likes quiet music", 0, &same) == ESP_OK && same == id);
    assert(s_data->count == 1 && hisu_memory_pending());
    assert(mock_flash_operations == writes);
    hisu_memory_recent(true, "What did we discuss?");
    hisu_memory_recent(false, "A small robot.");
    char *context = hisu_memory_context();
    assert(context && strstr(context, "Likes quiet music") && strstr(context, "small robot")); free(context);
    assert(hisu_memory_flush() == ESP_OK);
    assert(!hisu_memory_pending());
    reboot();
    assert(s_data->count == 1 && !strcmp(s_data->facts[0].text, "Likes quiet music"));
    writes = mock_flash_operations;
    assert(!hisu_memory_pending() && hisu_memory_flush() == ESP_OK && mock_flash_operations == writes);
    assert(!s_recent[0]);
    assert(hisu_memory_set_enabled(false) == ESP_OK);
    assert(hisu_memory_remember("Must not save", 0, &same) == ESP_ERR_INVALID_STATE);
    assert(hisu_memory_flush() == ESP_OK); reboot(); assert(s_data->enabled == 0);
    assert(hisu_memory_set_enabled(true) == ESP_OK);
    assert(hisu_memory_remember("Likes soft music", id, &same) == ESP_OK && same == id);
    assert(s_data->count == 1);
    assert(hisu_memory_flush() == ESP_OK);
    assert(!flash_contains("Likes quiet music"));
    /* Cut power at each erase/payload/header/retirement operation. */
    for (int cut = 0; cut < 4; ++cut) {
        reset();
        assert(hisu_memory_remember("Old fact", 0, &id) == ESP_OK);
        assert(hisu_memory_flush() == ESP_OK);
        assert(hisu_memory_remember("New fact", id, &same) == ESP_OK);
        mock_flash_budget = cut;
        assert(hisu_memory_flush() != ESP_OK);
        reboot();
        assert(s_data->count == 1);
        assert(!strcmp(s_data->facts[0].text, cut < 3 ? "Old fact" : "New fact"));
        assert(hisu_memory_flush() == ESP_OK);
    }
    reset();
    assert(hisu_memory_remember("Delete this fact", 0, &id) == ESP_OK);
    assert(hisu_memory_flush() == ESP_OK);
    assert(hisu_memory_forget(id) == ESP_OK && !s_recent[0]);
    assert(hisu_memory_flush() == ESP_OK && !flash_contains("Delete this fact"));
    reboot(); assert(s_data->count == 0);
    assert(hisu_memory_forget(id) == ESP_ERR_NOT_FOUND);
    assert(hisu_memory_remember("First fact", 0, &id) == ESP_OK);
    mock_during_flash = mutate_during_flash;
    assert(hisu_memory_flush() == ESP_OK && hisu_memory_pending());
    assert(hisu_memory_flush() == ESP_OK); reboot(); assert(s_data->count == 2);
    assert(hisu_memory_clear() == ESP_OK && hisu_memory_flush() == ESP_OK);
    assert(!flash_contains("First fact") && !flash_contains("New fact during save"));
    for (unsigned i = 0; i < HISU_MEMORY_COUNT; ++i) {
        char fact[32]; snprintf(fact, sizeof(fact), "Fact %u", i);
        assert(hisu_memory_remember(fact, 0, &id) == ESP_OK);
    }
    assert(hisu_memory_remember("Full", 0, &id) == HISU_MEMORY_ERR_FULL);
    uint32_t evicted = s_data->facts[7].id, revision = s_revision;
    unsigned before_eviction_writes = mock_flash_operations;
    assert(hisu_memory_evict_and_remember("New useful fact", evicted, "Wrong old text", &same) == ESP_ERR_INVALID_STATE);
    assert(hisu_memory_evict_and_remember("New useful fact", UINT32_MAX, "Fact 7", &same) == ESP_ERR_NOT_FOUND);
    assert(hisu_memory_evict_and_remember("Fact 1", evicted, "Fact 7", &same) == ESP_ERR_INVALID_ARG);
    assert(hisu_memory_evict_and_remember("bad\ntext", evicted, "Fact 7", &same) == ESP_ERR_INVALID_ARG);
    assert(hisu_memory_evict_and_remember("New useful fact", evicted, "Fact 7", NULL) == ESP_ERR_INVALID_ARG);
    assert(hisu_memory_set_enabled(false) == ESP_OK);
    uint32_t disabled_revision = s_revision;
    assert(hisu_memory_evict_and_remember("New useful fact", evicted, "Fact 7", &same) == ESP_ERR_INVALID_STATE);
    assert(s_revision == disabled_revision && !strcmp(s_data->facts[7].text,"Fact 7"));
    assert(hisu_memory_set_enabled(true) == ESP_OK);
    assert(s_revision == revision + 2);
    assert(hisu_memory_evict_and_remember("New useful fact", evicted, "Fact 7", &same) == ESP_OK);
    assert(s_data->count == HISU_MEMORY_COUNT && same != evicted && same == s_data->facts[23].id);
    assert(!strcmp(s_data->facts[7].text,"Fact 8") && !strcmp(s_data->facts[23].text,"New useful fact"));
    assert(hisu_memory_forget(evicted) == ESP_ERR_NOT_FOUND);
    assert(mock_flash_operations == before_eviction_writes);
    assert(hisu_memory_flush() == ESP_OK); reboot();
    assert(s_data->count == HISU_MEMORY_COUNT && !flash_contains("Fact 7"));
    assert(!strcmp(s_data->facts[23].text,"New useful fact"));
    for (unsigned i = 0; i < 1000; ++i) hisu_memory_recent(i % 2 == 0, "Xin chao \xe1\xba\xb9");
    assert(strlen(s_recent) <= HISU_MEMORY_RECENT_BYTES);
    char *json = hisu_memory_json(); cJSON *parsed = cJSON_Parse(json);
    assert(parsed && cJSON_GetArraySize(cJSON_GetObjectItem(parsed, "facts")) == HISU_MEMORY_COUNT);
    cJSON_Delete(parsed); free(json);
    /* Worst-case escaping stays valid and includes the newest relevant facts. */
    assert(hisu_memory_clear() == ESP_OK);
    char escaped[HISU_MEMORY_FACT_BYTES + 1];
    memset(escaped, '"', sizeof(escaped) - 1); escaped[HISU_MEMORY_FACT_BYTES] = 0;
    for (unsigned i = 0; i < HISU_MEMORY_COUNT; ++i) {
        escaped[0] = (char)('A' + i);
        assert(hisu_memory_remember(escaped, 0, &id) == ESP_OK);
    }
    json = hisu_memory_json(); parsed = cJSON_Parse(json);
    assert(parsed && cJSON_GetArraySize(cJSON_GetObjectItem(parsed,"facts")) == HISU_MEMORY_COUNT);
    cJSON_Delete(parsed); free(json);
    context = hisu_memory_context(); parsed = cJSON_Parse(context);
    cJSON *facts = cJSON_GetObjectItem(parsed,"known_user_facts");
    assert(parsed && cJSON_GetArraySize(facts) < HISU_MEMORY_COUNT);
    assert(cJSON_GetObjectItem(cJSON_GetArrayItem(facts,0),"id")->valuedouble == id);
    cJSON_Delete(parsed); free(context);
    /* At every interrupted eviction save, boot sees either complete old or new data. */
    for (int cut = 0; cut < 4; ++cut) {
        reset();
        assert(hisu_memory_evict_and_remember("Too early", 1, "Old 0", &id) == ESP_ERR_INVALID_STATE);
        for (unsigned i = 0; i < HISU_MEMORY_COUNT; ++i) {
            char fact[32]; snprintf(fact,sizeof(fact),"Old %u",i);
            assert(hisu_memory_remember(fact,0,&id) == ESP_OK);
        }
        assert(hisu_memory_flush() == ESP_OK);
        evicted = s_data->facts[0].id;
        assert(hisu_memory_evict_and_remember("Replacement",evicted,"Old 0",&id) == ESP_OK);
        mock_flash_budget = cut;
        assert(hisu_memory_flush() != ESP_OK);
        reboot();
        assert(s_data->count == HISU_MEMORY_COUNT);
        assert(!strcmp(s_data->facts[cut < 3 ? 0 : 23].text,cut < 3 ? "Old 0" : "Replacement"));
    }
    reset();
    assert(hisu_memory_remember("CRC fallback old", 0, &id) == ESP_OK && hisu_memory_flush() == ESP_OK);
    assert(hisu_memory_remember("CRC fallback new", id, &same) == ESP_OK);
    mock_flash_budget = 3;
    assert(hisu_memory_flush() != ESP_OK);
    mock_flash[HISU_MEMORY_OFFSET + s_slot * HISU_MEMORY_SLOT_BYTES + sizeof(hisu_memory_record_t)] ^= 1;
    reboot();
    assert(s_data->count == 1 && !strcmp(s_data->facts[0].text,"CRC fallback old"));
    free(s_data); free(s_snapshot); free(s_recent); s_lock = NULL;
    s_data = s_snapshot = NULL; s_recent = NULL;
    mock_alloc_budget = 1;
    assert(hisu_memory_init() == ESP_ERR_NO_MEM && !s_lock && !s_data && !s_snapshot && !s_recent);
    mock_alloc_budget = -1;
    puts("Memory tests passed: UTF-8, bounds, duplicate/update, full-store atomic eviction, stale-target rejection, disabled mode, reboot recall, torn writes, retirement, deferred/concurrent saves and deletion.");
    return 0;
}
