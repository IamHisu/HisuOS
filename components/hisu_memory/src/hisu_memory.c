#include "hisu_memory.h"
#include "hisu_memory_model.h"
#include "hisu_memory_json_guard.h"
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
static char *s_summary_source;
static uint32_t s_summary_ticket;
static bool s_summary_requested;
static bool s_summary_blocked,s_summary_new_turn;
static bool s_recent_user, s_dirty, s_flushing, s_purge;
static uint32_t s_revision, s_sequence;
static int s_slot = -1;
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
static void summary_clear_locked(void)
{
    memset(s_summary_source,0,HISU_MEMORY_RECENT_BYTES+1);
    s_summary_requested=false; s_summary_blocked=true; s_summary_new_turn=true; ++s_summary_ticket;
}
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
    s_summary_ticket=1; s_summary_requested=false;
    s_summary_blocked=false; s_summary_new_turn=true;
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "storage");
    if (!s_partition || s_partition->size < HISU_MEMORY_OFFSET + 2U * HISU_MEMORY_SLOT_BYTES)
        return ESP_ERR_NOT_FOUND;
    s_data = heap_caps_calloc(1, sizeof(*s_data), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_snapshot = heap_caps_calloc(1, sizeof(*s_snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_recent = heap_caps_calloc(1, HISU_MEMORY_RECENT_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_summary_source=heap_caps_calloc(1,HISU_MEMORY_RECENT_BYTES+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!s_data || !s_snapshot || !s_recent || !s_summary_source) goto no_memory;
    s_data->next_id = 1; s_data->enabled = 1;
    bool found = false, occupied[2] = {false, false};
    for (int slot = 0; slot < 2; ++slot) {
        hisu_memory_record_t header;
        size_t offset = HISU_MEMORY_OFFSET + slot * HISU_MEMORY_SLOT_BYTES;
        esp_err_t err = esp_partition_read(s_partition, offset, &header, sizeof(header));
        if (err == ESP_OK) err = esp_partition_read(s_partition, offset + sizeof(header), s_snapshot, sizeof(*s_snapshot));
        if (err != ESP_OK) {
            free(s_data); free(s_snapshot); free(s_recent); free(s_summary_source);
            s_data = s_snapshot = NULL; s_recent = s_summary_source = NULL;
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
    free(s_data); free(s_snapshot); free(s_recent); free(s_summary_source);
    s_data = s_snapshot = NULL; s_recent = s_summary_source = NULL;
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
                strcpy(s_data->facts[i].text, fact); s_dirty = true; ++s_revision; ++s_summary_ticket;
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
    s_dirty = true; ++s_revision; ++s_summary_ticket;
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
    s_dirty = true; ++s_revision; summary_clear_locked();
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
        summary_clear_locked(); s_dirty = true; ++s_revision; unlock(); return ESP_OK;
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
    summary_clear_locked();
    s_dirty = true; ++s_revision; unlock(); return ESP_OK;
}

esp_err_t hisu_memory_set_enabled(bool enabled)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    lock();
    if (s_data->enabled != (uint32_t)enabled) {
        s_data->enabled = enabled; s_dirty = true; ++s_revision; summary_clear_locked();
    }
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
    if (user && s_data->enabled && !s_summary_blocked) {
        size_t n=strlen(s_summary_source);
        bool separator=n && s_summary_new_turn;
        size_t added=bytes+(separator ? 1U : 0U);
        if (n+added>HISU_MEMORY_RECENT_BYTES) {
            size_t discard=n+added-HISU_MEMORY_RECENT_BYTES;
            while (discard<n && ((unsigned char)s_summary_source[discard]&0xc0)==0x80) ++discard;
            memmove(s_summary_source,s_summary_source+discard,n-discard+1); n-=discard;
        }
        if (separator) s_summary_source[n++]='\n';
        memcpy(s_summary_source+n,text,bytes+1); s_summary_new_turn=false; ++s_summary_ticket;
    }
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
    lock(); memset(s_recent, 0, HISU_MEMORY_RECENT_BYTES + 1); summary_clear_locked(); unlock();
}

void hisu_memory_summary_request(void)
{
    if (!s_lock) return;
    lock(); if (s_data->enabled && s_summary_source[0]) s_summary_requested=true; unlock();
}
void hisu_memory_summary_begin_turn(void)
{
    if (!s_lock) return;
    lock(); s_summary_blocked=false; s_summary_new_turn=true; ++s_summary_ticket; unlock();
}
bool hisu_memory_summary_current(uint32_t ticket)
{
    if (!s_lock) return false;
    lock(); bool current=s_data->enabled && s_summary_requested && ticket==s_summary_ticket; unlock(); return current;
}
void hisu_memory_summary_done(uint32_t ticket)
{
    if (!s_lock) return;
    lock(); if (ticket==s_summary_ticket) summary_clear_locked(); unlock();
}
char *hisu_memory_summary_snapshot(uint32_t *ticket)
{
    if (!s_lock || !ticket) return NULL;
    lock(); bool ready=s_data->enabled && s_summary_requested && s_summary_source[0]; unlock();
    if (!ready) return NULL;
    const size_t capacity=HISU_MEMORY_COUNT*(HISU_MEMORY_FACT_BYTES*2+64)+HISU_MEMORY_RECENT_BYTES*6+512;
    char *out=heap_caps_calloc(1,capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!out) return NULL;
    lock();
    size_t used=(size_t)snprintf(out,capacity,"{\"known_facts\":");
    bool ok=s_data->enabled && s_summary_requested && s_summary_source[0] && write_facts(out,capacity,&used,false);
    static const char field[]=",\"user_statements\":";
    if (ok && used+sizeof(field)<capacity) { memcpy(out+used,field,sizeof(field)); used+=sizeof(field)-1; }
    else ok=false;
    cJSON *source=ok ? cJSON_CreateString(s_summary_source) : NULL;
    ok=source && cJSON_PrintPreallocated(source,out+used,(int)(capacity-used),false);
    cJSON_Delete(source);
    if (ok) { used+=strlen(out+used); ok=used+2<capacity; }
    if (ok) { out[used++]='}'; out[used]=0; *ticket=s_summary_ticket; }
    unlock();
    if (!ok) { free(out); return NULL; }
    return out;
}
esp_err_t hisu_memory_summary_apply(uint32_t ticket,const char *proposals,unsigned *stored)
{
    if (stored) *stored=0;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (!proposals || strlen(proposals)>4096 || strstr(proposals,"\\u0000") || !hisu_memory_json_shallow(proposals,6)) return ESP_ERR_INVALID_ARG;
    cJSON *root=cJSON_ParseWithOpts(proposals,NULL,true);
    const cJSON *facts=cJSON_GetObjectItemCaseSensitive(root,"facts");
    bool valid=cJSON_IsObject(root) && cJSON_GetArraySize(root)==1 && cJSON_IsArray(facts) && cJSON_GetArraySize(facts)<=3;
    hisu_memory_data_t *stage=valid ? heap_caps_calloc(1,sizeof(*stage),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) : NULL;
    if (!valid || !stage) { cJSON_Delete(root); return valid ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_ARG; }
    lock();
    if (!s_data->enabled || !s_summary_requested || ticket!=s_summary_ticket) {
        unlock(); free(stage); cJSON_Delete(root); return ESP_ERR_INVALID_STATE;
    }
    *stage=*s_data; unsigned changed=0;
    const cJSON *item;
    cJSON_ArrayForEach(item,facts) {
        const cJSON *fact=cJSON_GetObjectItemCaseSensitive(item,"fact"),*evidence=cJSON_GetObjectItemCaseSensitive(item,"evidence");
        const cJSON *replace=cJSON_GetObjectItemCaseSensitive(item,"replace_id"),*expected=cJSON_GetObjectItemCaseSensitive(item,"expected_fact");
        valid=cJSON_IsObject(item) && cJSON_GetArraySize(item)==4 && cJSON_IsString(fact) && hisu_memory_fact_valid(fact->valuestring) &&
            cJSON_IsString(evidence) && hisu_memory_fact_valid(evidence->valuestring) && strlen(evidence->valuestring)>=8 &&
            strstr(s_summary_source,evidence->valuestring) && cJSON_IsNumber(replace) && replace->valuedouble>=0 &&
            replace->valuedouble<INT32_MAX && replace->valuedouble==replace->valueint && cJSON_IsString(expected);
        if (!valid) break;
        uint32_t index=stage->count;
        for (uint32_t i=0;i<stage->count;++i) if (!strcmp(stage->facts[i].text,fact->valuestring)) { index=i; break; }
        if (index<stage->count) continue;
        if (replace->valueint) {
            for (uint32_t i=0;i<stage->count;++i) if (stage->facts[i].id==(uint32_t)replace->valueint) { index=i; break; }
            if (index==stage->count || strcmp(stage->facts[index].text,expected->valuestring)) { valid=false; break; }
        } else {
            if (expected->valuestring[0]) { valid=false; break; }
            if (stage->count==HISU_MEMORY_COUNT) continue;
            if (stage->next_id>=INT32_MAX) { valid=false; break; }
            stage->facts[index].id=stage->next_id++; ++stage->count;
        }
        memset(stage->facts[index].text,0,sizeof(stage->facts[index].text));
        strcpy(stage->facts[index].text,fact->valuestring); ++changed;
    }
    if (valid) {
        if (changed) { *s_data=*stage; s_dirty=true; ++s_revision; }
        summary_clear_locked(); if (stored) *stored=changed;
    }
    unlock(); free(stage); cJSON_Delete(root);
    return valid ? ESP_OK : ESP_ERR_INVALID_ARG;
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
