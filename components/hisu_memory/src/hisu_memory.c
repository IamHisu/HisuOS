#include "hisu_memory.h"
#include "hisu_memory_model.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static SemaphoreHandle_t s_lock;
static const esp_partition_t *s_partition;
static hisu_memory_data_t *s_data, *s_snapshot;
static char *s_recent;
static bool s_recent_user, s_dirty, s_flushing, s_purge;
static uint32_t s_revision, s_sequence;
static int s_slot = -1;
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
static bool erased(const void *data, size_t bytes)
{
    const uint8_t *p = data;
    while (bytes--) if (*p++ != 0xff) return false;
    return true;
}

esp_err_t hisu_memory_init(void)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;
    s_slot = -1; s_sequence = s_revision = 0;
    s_dirty = s_flushing = s_purge = false;
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "storage");
    if (!s_partition || s_partition->size < HISU_MEMORY_OFFSET + 2U * HISU_MEMORY_SLOT_BYTES)
        return ESP_ERR_NOT_FOUND;
    s_data = heap_caps_calloc(1, sizeof(*s_data), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_snapshot = heap_caps_calloc(1, sizeof(*s_snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_recent = heap_caps_calloc(1, HISU_MEMORY_RECENT_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_data || !s_snapshot || !s_recent) goto no_memory;
    s_data->next_id = 1; s_data->enabled = 1;
    bool found = false, occupied[2] = {false, false};
    for (int slot = 0; slot < 2; ++slot) {
        hisu_memory_record_t header;
        size_t offset = HISU_MEMORY_OFFSET + slot * HISU_MEMORY_SLOT_BYTES;
        esp_err_t err = esp_partition_read(s_partition, offset, &header, sizeof(header));
        if (err == ESP_OK) err = esp_partition_read(s_partition, offset + sizeof(header), s_snapshot, sizeof(*s_snapshot));
        if (err != ESP_OK) {
            free(s_data); free(s_snapshot); free(s_recent);
            s_data = s_snapshot = NULL; s_recent = NULL;
            return err;
        }
        occupied[slot] = !erased(&header, sizeof(header)) || !erased(s_snapshot, sizeof(*s_snapshot));
        if (!hisu_memory_record_valid(&header, s_snapshot)) continue;
        if (!found || (int32_t)(header.sequence - s_sequence) > 0) {
            *s_data = *s_snapshot; s_sequence = header.sequence; s_slot = slot;
        }
        found = true;
    }
    memset(s_snapshot, 0, sizeof(*s_snapshot));
    s_purge = found && occupied[1 - s_slot];
    s_lock = xSemaphoreCreateMutex();
    if (s_lock) return ESP_OK;
no_memory:
    free(s_data); free(s_snapshot); free(s_recent);
    s_data = s_snapshot = NULL; s_recent = NULL;
    return ESP_ERR_NO_MEM;
}

esp_err_t hisu_memory_remember(const char *fact, uint32_t replace_id, uint32_t *id)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (!id || !hisu_memory_fact_valid(fact)) return ESP_ERR_INVALID_ARG;
    lock();
    if (!s_data->enabled) { unlock(); return ESP_ERR_INVALID_STATE; }
    if (replace_id) {
        for (uint32_t i = 0; i < s_data->count; ++i) if (s_data->facts[i].id == replace_id) {
            *id = replace_id;
            if (strcmp(s_data->facts[i].text, fact)) {
                memset(s_data->facts[i].text, 0, sizeof(s_data->facts[i].text));
                strcpy(s_data->facts[i].text, fact); s_dirty = true; ++s_revision;
            }
            unlock(); return ESP_OK;
        }
        unlock(); return ESP_ERR_NOT_FOUND;
    }
    for (uint32_t i = 0; i < s_data->count; ++i)
        if (!strcmp(s_data->facts[i].text, fact)) { *id = s_data->facts[i].id; unlock(); return ESP_OK; }
    if (s_data->count == HISU_MEMORY_COUNT) { unlock(); return HISU_MEMORY_ERR_FULL; }
    if (s_data->next_id >= INT32_MAX) {
        unlock(); return ESP_ERR_NO_MEM;
    }
    hisu_memory_fact_t *entry = &s_data->facts[s_data->count++];
    entry->id = s_data->next_id++;
    strcpy(entry->text, fact); *id = entry->id;
    s_dirty = true; ++s_revision;
    unlock(); return ESP_OK;
}

esp_err_t hisu_memory_evict_and_remember(const char *fact, uint32_t evict_id,
                                      const char *expected_fact, uint32_t *id)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (!id || !evict_id || !hisu_memory_fact_valid(fact) || !hisu_memory_fact_valid(expected_fact))
        return ESP_ERR_INVALID_ARG;
    lock();
    if (!s_data->enabled || s_data->count != HISU_MEMORY_COUNT) { unlock(); return ESP_ERR_INVALID_STATE; }
    if (s_data->next_id >= INT32_MAX) { unlock(); return ESP_ERR_NO_MEM; }
    uint32_t index = s_data->count;
    for (uint32_t i = 0; i < s_data->count; ++i) {
        if (!strcmp(s_data->facts[i].text, fact)) { unlock(); return ESP_ERR_INVALID_ARG; }
        if (s_data->facts[i].id == evict_id) index = i;
    }
    if (index == s_data->count) { unlock(); return ESP_ERR_NOT_FOUND; }
    if (strcmp(s_data->facts[index].text, expected_fact)) { unlock(); return ESP_ERR_INVALID_STATE; }
    /* One cache transaction: never delete first and risk losing the old fact
     * when the subsequent add fails. All validation happens before mutation. */
    memmove(&s_data->facts[index], &s_data->facts[index + 1],
            (s_data->count - index - 1) * sizeof(s_data->facts[0]));
    hisu_memory_fact_t *entry = &s_data->facts[s_data->count - 1];
    memset(entry, 0, sizeof(*entry));
    entry->id = s_data->next_id++;
    strcpy(entry->text, fact); *id = entry->id;
    s_dirty = true; ++s_revision;
    unlock(); return ESP_OK;
}

esp_err_t hisu_memory_forget(uint32_t id)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    lock();
    for (uint32_t i = 0; i < s_data->count; ++i) if (s_data->facts[i].id == id) {
        memmove(&s_data->facts[i], &s_data->facts[i + 1], (s_data->count - i - 1) * sizeof(s_data->facts[0]));
        memset(&s_data->facts[--s_data->count], 0, sizeof(s_data->facts[0]));
        memset(s_recent, 0, HISU_MEMORY_RECENT_BYTES + 1);
        s_dirty = true; ++s_revision; unlock(); return ESP_OK;
    }
    unlock(); return ESP_ERR_NOT_FOUND;
}

esp_err_t hisu_memory_clear(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    lock();
    uint32_t next = s_data->next_id, enabled = s_data->enabled;
    memset(s_data, 0, sizeof(*s_data));
    s_data->next_id = next; s_data->enabled = enabled;
    memset(s_recent, 0, HISU_MEMORY_RECENT_BYTES + 1);
    s_dirty = true; ++s_revision; unlock(); return ESP_OK;
}

esp_err_t hisu_memory_set_enabled(bool enabled)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    lock();
    if (s_data->enabled != (uint32_t)enabled) { s_data->enabled = enabled; s_dirty = true; ++s_revision; }
    unlock(); return ESP_OK;
}

static bool write_facts(char *out, size_t capacity, size_t *used, bool limited)
{
    size_t budget = limited ? 2048 : SIZE_MAX;
    bool first = true;
    if (*used + 3 >= capacity) return false;
    out[(*used)++] = '['; out[*used] = 0;
    for (uint32_t i = 0; i < s_data->count; ++i) {
        const hisu_memory_fact_t *entry = &s_data->facts[limited ? s_data->count - 1 - i : i];
        size_t bytes = strlen(entry->text) + 32;
        if (bytes > budget) continue;
        budget -= bytes;
        if (!first) { out[(*used)++] = ','; out[*used] = 0; }
        cJSON *item = cJSON_CreateObject();
        if (!item || !cJSON_AddNumberToObject(item, "id", entry->id) ||
            !cJSON_AddStringToObject(item, "fact", entry->text) ||
            !cJSON_PrintPreallocated(item, out + *used, (int)(capacity - *used), false)) {
            cJSON_Delete(item); return false;
        }
        cJSON_Delete(item); *used += strlen(out + *used); first = false;
    }
    if (*used + 2 >= capacity) return false;
    out[(*used)++] = ']'; out[*used] = 0; return true;
}

char *hisu_memory_json(void)
{
    if (!s_lock) return NULL;
    /* Serialize one fact at a time: the complete cJSON tree costs too much
     * internal heap while TLS and the speaker are active. Payload is PSRAM. */
    const size_t capacity = HISU_MEMORY_COUNT * (HISU_MEMORY_FACT_BYTES * 2 + 64) + 512;
    char *json = heap_caps_calloc(1, capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json) return NULL;
    lock();
    size_t used = (size_t)snprintf(json, capacity, "{\"enabled\":%s,\"pending\":%s,\"capacity\":%u,\"facts\":",
        s_data->enabled ? "true" : "false", s_dirty || s_purge || s_flushing ? "true" : "false", HISU_MEMORY_COUNT);
    bool ok = used < capacity && write_facts(json, capacity, &used, false) && used + 2 < capacity;
    if (ok) { json[used++] = '}'; json[used] = 0; }
    unlock();
    if (!ok) { free(json); return NULL; }
    return json;
}

char *hisu_memory_context(void)
{
    if (!s_lock) return NULL;
    const size_t capacity = 2048 * 2 + HISU_MEMORY_RECENT_BYTES * 6 + 512;
    char *json = heap_caps_calloc(1, capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json) return NULL;
    lock();
    size_t used = (size_t)snprintf(json, capacity, "{\"automatic_memory_enabled\":%s,\"known_user_facts\":",
        s_data->enabled ? "true" : "false");
    bool ok = used < capacity && write_facts(json, capacity, &used, true);
    static const char field[] = ",\"recent_conversation\":";
    if (ok && used + sizeof(field) < capacity) { strcpy(json + used, field); used += sizeof(field) - 1; }
    else ok = false;
    cJSON *recent = ok ? cJSON_CreateString(s_recent) : NULL;
    ok = recent && cJSON_PrintPreallocated(recent, json + used, (int)(capacity - used), false);
    cJSON_Delete(recent);
    if (ok) { used += strlen(json + used); ok = used + 2 < capacity; }
    if (ok) { json[used++] = '}'; json[used] = 0; }
    unlock();
    if (!ok) { free(json); return NULL; }
    return json;
}

void hisu_memory_recent(bool user, const char *text)
{
    if (!s_lock || !text || !text[0]) return;
    size_t bytes = strlen(text);
    if (bytes > HISU_MEMORY_RECENT_BYTES / 2) return;
    lock();
    const char *label = user ? "\nUser: " : "\nHi Su: ";
    bool prefix = !s_recent[0] || user != s_recent_user;
    size_t used = strlen(s_recent), added = bytes + (prefix ? strlen(label) : 0);
    if (used + added > HISU_MEMORY_RECENT_BYTES) {
        size_t discard = used + added - HISU_MEMORY_RECENT_BYTES;
        while (discard < used && ((unsigned char)s_recent[discard] & 0xc0) == 0x80) ++discard;
        memmove(s_recent, s_recent + discard, used - discard + 1);
        used -= discard;
    }
    if (prefix) { strcpy(s_recent + used, label); used += strlen(label); }
    strcpy(s_recent + used, text);
    s_recent_user = user;
    unlock();
}

void hisu_memory_clear_recent(void)
{
    if (!s_lock) return;
    lock(); memset(s_recent, 0, HISU_MEMORY_RECENT_BYTES + 1); unlock();
}

bool hisu_memory_pending(void)
{
    if (!s_lock) return false;
    lock(); bool pending = s_dirty || s_purge; unlock(); return pending;
}

esp_err_t hisu_memory_flush(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    lock();
    if (s_flushing) { unlock(); return ESP_ERR_INVALID_STATE; }
    bool dirty = s_dirty;
    if (!dirty && !s_purge) { unlock(); return ESP_OK; }
    s_flushing = true;
    uint32_t revision = s_revision, sequence = s_sequence + 1;
    int old = s_slot, target = old == 0 ? 1 : 0;
    if (dirty) *s_snapshot = *s_data;
    unlock();
    esp_err_t err = ESP_OK;
    if (dirty) {
        hisu_memory_record_t header;
        hisu_memory_record_make(&header, sequence, s_snapshot);
        size_t offset = HISU_MEMORY_OFFSET + target * HISU_MEMORY_SLOT_BYTES;
        err = esp_partition_erase_range(s_partition, offset, HISU_MEMORY_SLOT_BYTES);
        if (err == ESP_OK) err = esp_partition_write(s_partition, offset + sizeof(header), s_snapshot, sizeof(*s_snapshot));
        if (err == ESP_OK) err = esp_partition_write(s_partition, offset, &header, sizeof(header));
        if (err == ESP_OK) {
            /* Verify the durable copy before retiring the previous one. */
            hisu_memory_record_t read_header;
            err = esp_partition_read(s_partition, offset, &read_header, sizeof(read_header));
            if (err == ESP_OK) err = esp_partition_read(s_partition, offset + sizeof(header), s_snapshot, sizeof(*s_snapshot));
            if (err == ESP_OK && !hisu_memory_record_valid(&read_header, s_snapshot)) err = ESP_FAIL;
        }
        if (err == ESP_OK) {
            lock(); s_slot = target; s_sequence = sequence;
            if (revision == s_revision) s_dirty = false;
            s_purge = old >= 0; unlock();
        }
    }
    if (err == ESP_OK && old >= 0) {
        int retired = dirty ? old : 1 - old;
        err = esp_partition_erase_range(s_partition, HISU_MEMORY_OFFSET + retired * HISU_MEMORY_SLOT_BYTES, HISU_MEMORY_SLOT_BYTES);
        if (err == ESP_OK) { lock(); s_purge = false; unlock(); }
    }
    lock(); memset(s_snapshot, 0, sizeof(*s_snapshot)); s_flushing = false; unlock();
    return err;
}
