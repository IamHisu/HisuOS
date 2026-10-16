#include "hisu_schedule.h"
#include "hisu_memory_model.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* After the five immutable Kore slots; never erase wallpaper or user memory. */
#define SCHEDULE_OFFSET 0x3e6000U
#define SLOT_BYTES 8192U
typedef struct { uint32_t next_id, count; hisu_schedule_event_t events[HISU_SCHEDULE_COUNT]; } data_t;
typedef struct { uint32_t magic, version, sequence, crc, header_crc, reserved[11]; } header_t;
_Static_assert(sizeof(header_t) + sizeof(data_t) <= SLOT_BYTES, "Schedule fits flash slot");
_Static_assert(SCHEDULE_OFFSET == HISU_MEMORY_OFFSET + 2U * HISU_MEMORY_SLOT_BYTES + 5U * 0x60000U,
               "Schedule follows the five audio slots");
static SemaphoreHandle_t s_lock;
static const esp_partition_t *s_partition;
static data_t *s_data, *s_snapshot;
static uint32_t s_sequence, s_revision;
static int s_slot = -1;
static bool s_dirty, s_flushing;
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
static bool valid_time(int64_t utc) { return utc >= 1704067200LL && utc < 4102444800LL; }
int64_t hisu_schedule_local_utc(const char *text, int offset_minutes)
{
    if (!text || strlen(text)!=19 || offset_minutes < -720 || offset_minutes > 840) return 0;
    for (unsigned i=0; i<19; ++i) {
        char separator=i==4 || i==7 ? '-' : i==10 ? 'T' : i==13 || i==16 ? ':' : 0;
        if (separator ? text[i]!=separator : (text[i]<'0' || text[i]>'9')) return 0;
    }
    int year=atoi(text), month=atoi(text+5), day=atoi(text+8), hour=atoi(text+11), minute=atoi(text+14), second=atoi(text+17);
    static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap=year%4==0 && (year%100!=0 || year%400==0);
    if (year<2024 || year>2099 || month<1 || month>12 || day<1 || day>days[month-1]+(month==2 && leap) ||
        hour>23 || minute>59 || second>59) return 0;
    int total=0;
    for (int y=1970; y<year; ++y) total+=365+(y%4==0 && (y%100!=0 || y%400==0));
    for (int m=1; m<month; ++m) total+=days[m-1]+(m==2 && leap);
    return ((int64_t)(total+day-1)*86400+hour*3600+minute*60+second)-(int64_t)offset_minutes*60;
}
static bool valid_text(const char *text)
{
    return text && strlen(text) <= HISU_SCHEDULE_TEXT && hisu_memory_fact_valid(text);
}
static unsigned prune_locked(int64_t now)
{
    if (!valid_time(now)) return 0;
    unsigned before=s_data->count, kept=0;
    for (unsigned i=0; i<before; ++i) {
        if (s_data->events[i].utc < now - 7LL*86400) continue;
        s_data->events[kept++]=s_data->events[i];
    }
    if (kept==before) return 0;
    memset(s_data->events+kept,0,(before-kept)*sizeof(s_data->events[0]));
    s_data->count=kept; s_dirty=true; ++s_revision;
    return before-kept;
}
unsigned hisu_schedule_prune(int64_t now)
{
    if (!s_lock) return 0;
    lock(); unsigned removed=prune_locked(now); unlock(); return removed;
}
static bool valid(const header_t *header, const data_t *data)
{
    header_t copy = *header; copy.header_crc = 0;
    if (header->magic != 0x31435348U || header->version != 1 ||
        hisu_memory_crc(&copy,sizeof(copy)) != header->header_crc ||
        hisu_memory_crc(data,sizeof(*data)) != header->crc ||
        !data->next_id || data->next_id > INT32_MAX || data->count > HISU_SCHEDULE_COUNT) return false;
    for (unsigned i = 0; i < data->count; ++i) {
        const hisu_schedule_event_t *event = &data->events[i];
        if (!event->id || event->id >= data->next_id || event->delivered > 1 || !valid_time(event->utc) ||
            !memchr(event->text,0,sizeof(event->text)) || !valid_text(event->text)) return false;
        for (unsigned j = 0; j < i; ++j) if (data->events[j].id == event->id) return false;
    }
    return true;
}
esp_err_t hisu_schedule_init(void)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"storage");
    if (!s_partition || s_partition->size < SCHEDULE_OFFSET + 2 * SLOT_BYTES) return ESP_ERR_NOT_FOUND;
    s_data = heap_caps_calloc(1,sizeof(*s_data),MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_snapshot = heap_caps_calloc(1,sizeof(*s_snapshot),MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_data || !s_snapshot) goto no_memory;
    s_slot = -1; s_sequence = s_revision = 0; s_dirty = s_flushing = false;
    s_data->next_id = 1;
    for (int slot = 0; slot < 2; ++slot) {
        header_t header;
        size_t base = SCHEDULE_OFFSET + slot * SLOT_BYTES;
        esp_err_t err = esp_partition_read(s_partition,base,&header,sizeof(header));
        if (err == ESP_OK) err = esp_partition_read(s_partition,base + sizeof(header),s_snapshot,sizeof(*s_snapshot));
        if (err != ESP_OK) { free(s_data); free(s_snapshot); s_data=s_snapshot=NULL; return err; }
        if (valid(&header,s_snapshot) && (s_slot < 0 || (int32_t)(header.sequence-s_sequence) > 0)) {
            *s_data = *s_snapshot; s_slot = slot; s_sequence = header.sequence;
        }
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock) return ESP_OK;
no_memory:
    free(s_data); free(s_snapshot); s_data=s_snapshot=NULL; return ESP_ERR_NO_MEM;
}
esp_err_t hisu_schedule_set(uint32_t id, int64_t utc, const char *text, int64_t now, uint32_t *result)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (!result || !valid_time(now) || !valid_time(utc) || utc <= now || !valid_text(text)) return ESP_ERR_INVALID_ARG;
    lock();
    if (!id) (void)prune_locked(now);
    unsigned index = s_data->count;
    if (id) {
        for (unsigned i=0; i<s_data->count; ++i) if (s_data->events[i].id==id) { index=i; break; }
        if (index==s_data->count) { unlock(); return ESP_ERR_NOT_FOUND; }
    } else {
        for (unsigned i=0; i<s_data->count; ++i)
            if (s_data->events[i].utc==utc && !strcmp(s_data->events[i].text,text)) {
                *result=s_data->events[i].id; unlock(); return ESP_OK;
            }
        if (s_data->count==HISU_SCHEDULE_COUNT || s_data->next_id>=INT32_MAX) { unlock(); return ESP_ERR_NO_MEM; }
        ++s_data->count; id=s_data->next_id++;
    }
    hisu_schedule_event_t *event = &s_data->events[index];
    memset(event,0,sizeof(*event)); event->id=id; event->utc=utc; strcpy(event->text,text);
    for (unsigned i=1; i<s_data->count; ++i) {
        hisu_schedule_event_t item=s_data->events[i]; unsigned j=i;
        while (j && s_data->events[j-1].utc>item.utc) { s_data->events[j]=s_data->events[j-1]; --j; }
        s_data->events[j]=item;
    }
    s_dirty=true; ++s_revision; *result=id; unlock(); return ESP_OK;
}
esp_err_t hisu_schedule_delete(uint32_t id)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    lock();
    for (unsigned i=0; i<s_data->count; ++i) if (s_data->events[i].id==id) {
        memmove(&s_data->events[i],&s_data->events[i+1],(s_data->count-i-1)*sizeof(s_data->events[0]));
        memset(&s_data->events[--s_data->count],0,sizeof(s_data->events[0]));
        s_dirty=true; ++s_revision; unlock(); return ESP_OK;
    }
    unlock(); return ESP_ERR_NOT_FOUND;
}
bool hisu_schedule_at(unsigned index, hisu_schedule_event_t *out)
{
    if (!s_lock || !out) return false;
    lock(); bool found=index<s_data->count; if (found) *out=s_data->events[index]; unlock(); return found;
}
bool hisu_schedule_due(int64_t now, hisu_schedule_event_t *out)
{
    if (!s_lock || !out || !valid_time(now)) return false;
    lock(); const hisu_schedule_event_t *pick=NULL;
    for (unsigned i=0; i<s_data->count; ++i) {
        const hisu_schedule_event_t *event=&s_data->events[i];
        if (!event->delivered && event->utc<=now && now-event->utc<=86400 && (!pick || event->utc<pick->utc)) pick=event;
    }
    if (pick) *out=*pick;
    unlock(); return pick!=NULL;
}
esp_err_t hisu_schedule_flush(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    lock();
    if (s_flushing) { unlock(); return ESP_ERR_INVALID_STATE; }
    if (!s_dirty) { unlock(); return ESP_OK; }
    s_flushing=true; *s_snapshot=*s_data;
    uint32_t revision=s_revision;
    int slot=s_slot==0 ? 1 : 0;
    header_t header={.magic=0x31435348U,.version=1,.sequence=s_sequence+1,.crc=hisu_memory_crc(s_snapshot,sizeof(*s_snapshot))};
    header.header_crc=hisu_memory_crc(&header,sizeof(header));
    unlock();
    size_t base=SCHEDULE_OFFSET+slot*SLOT_BYTES;
    esp_err_t err=esp_partition_erase_range(s_partition,base,SLOT_BYTES);
    if (err==ESP_OK) err=esp_partition_write(s_partition,base+sizeof(header),s_snapshot,sizeof(*s_snapshot));
    if (err==ESP_OK) err=esp_partition_write(s_partition,base,&header,sizeof(header));
    /* Read back payload and CRC before accepting the new snapshot. */
    header_t readback;
    if (err==ESP_OK) err=esp_partition_read(s_partition,base,&readback,sizeof(readback));
    if (err==ESP_OK) err=esp_partition_read(s_partition,base+sizeof(header),s_snapshot,sizeof(*s_snapshot));
    if (err==ESP_OK && !valid(&readback,s_snapshot)) err=ESP_FAIL;
    lock();
    if (err==ESP_OK) { s_slot=slot; s_sequence=header.sequence; if (revision==s_revision) s_dirty=false; }
    s_flushing=false; unlock(); return err;
}
esp_err_t hisu_schedule_claim(uint32_t id)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    lock(); unsigned index=s_data->count;
    for (unsigned i=0; i<s_data->count; ++i) if (s_data->events[i].id==id && !s_data->events[i].delivered) { index=i; break; }
    if (index==s_data->count) { unlock(); return ESP_ERR_NOT_FOUND; }
    s_data->events[index].delivered=1; s_dirty=true; ++s_revision; unlock();
    esp_err_t err=hisu_schedule_flush();
    if (err!=ESP_OK) {
        lock();
        for (unsigned i=0; i<s_data->count; ++i) if (s_data->events[i].id==id) s_data->events[i].delivered=0;
        s_dirty=true; ++s_revision; unlock();
    }
    return err;
}
uint32_t hisu_schedule_revision(void)
{
    if (!s_lock) return 0;
    lock(); uint32_t revision=s_revision; unlock(); return revision;
}
char *hisu_schedule_json(int64_t from, int64_t until)
{
    if (!s_lock || until<=from) return NULL;
    const size_t capacity=HISU_SCHEDULE_COUNT*(HISU_SCHEDULE_TEXT*6+128)+256;
    char *out=heap_caps_calloc(1,capacity,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) return NULL;
    lock();
    size_t used=(size_t)snprintf(out,capacity,"{\"pending_save\":%s,\"capacity\":32,\"events\":[",s_dirty ? "true" : "false");
    bool first=true, ok=true;
    for (unsigned i=0; ok && i<s_data->count; ++i) {
        const hisu_schedule_event_t *event=&s_data->events[i];
        if (event->utc<from || event->utc>=until) continue;
        cJSON *item=cJSON_CreateObject();
        if (!first) out[used++]=',';
        ok=item && cJSON_AddNumberToObject(item,"id",event->id) && cJSON_AddNumberToObject(item,"utc",(double)event->utc) &&
            cJSON_AddStringToObject(item,"text",event->text) && cJSON_AddBoolToObject(item,"delivered",event->delivered) &&
            cJSON_PrintPreallocated(item,out+used,(int)(capacity-used),false);
        cJSON_Delete(item); if (ok) { used+=strlen(out+used); first=false; }
    }
    if (ok) strcpy(out+used,"]}");
    unlock(); if (!ok) { free(out); return NULL; } return out;
}
