#include "hisu_alert.h"
#include "hisu_alert_model.h"
#include "hisu_storage_layout.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define ALERT_OFFSET (HISU_MEMORY_OFFSET + 2U * HISU_MEMORY_SLOT_BYTES)
#define ALERT_SLOT_BYTES 0x60000U
#define ALERT_DATA_OFFSET 4096U
typedef struct {
    uint32_t magic, version, index, voice_crc, phrase_crc, bytes, pcm_crc, header_crc, reserved[8];
} alert_record_t;
_Static_assert(sizeof(alert_record_t) == 64, "Fixed alert header");
_Static_assert(HISU_ALERT_PCM_MAX + ALERT_DATA_OFFSET <= ALERT_SLOT_BYTES, "PCM fits slot");
static const char *const phrases[HISU_ALERT_COUNT] = {
#define HISU_ALERT_PHRASE(text) text,
#include "hisu_alert_phrases.inc"
#undef HISU_ALERT_PHRASE
};
static const esp_partition_t *s_partition;
static SemaphoreHandle_t s_lock;
static alert_record_t s_records[HISU_ALERT_COUNT];
static hisu_alert_status_t s_status;
static hisu_alert_policy_t s_policy = {.last = {-1, -1}};
static uint32_t crc(const void *p, size_t n) { return ~hisu_alert_crc_update(UINT32_MAX, p, n); }
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
static bool header_valid(const alert_record_t *record, unsigned index)
{
    alert_record_t header = *record; header.header_crc = 0;
    return record->magic == 0x31505348U && record->version == 1 && record->index == index &&
        record->voice_crc == crc("Kore",4) && record->phrase_crc == crc(phrases[index],strlen(phrases[index])) &&
        record->bytes >= 24000 && record->bytes <= HISU_ALERT_PCM_MAX && !(record->bytes & 1U) &&
        record->header_crc == crc(&header,sizeof(header));
}
static esp_err_t validate_pcm(unsigned index, const alert_record_t *record)
{
    uint8_t chunk[512];
    uint32_t sum = UINT32_MAX;
    for (size_t offset = 0; offset < record->bytes;) {
        size_t n = record->bytes - offset; if (n > sizeof(chunk)) n = sizeof(chunk);
        esp_err_t err = esp_partition_read(s_partition, ALERT_OFFSET + index * ALERT_SLOT_BYTES + ALERT_DATA_OFFSET + offset, chunk,n);
        if (err != ESP_OK) return err;
        sum = hisu_alert_crc_update(sum,chunk,n); offset += n;
    }
    return ~sum == record->pcm_crc ? ESP_OK : ESP_FAIL;
}
esp_err_t hisu_alert_init(void)
{
    if (s_lock) return ESP_ERR_INVALID_STATE;
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"storage");
    if (!s_partition || s_partition->size < ALERT_OFFSET + HISU_ALERT_COUNT * ALERT_SLOT_BYTES) return ESP_ERR_NOT_FOUND;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    for (unsigned i = 0; i < HISU_ALERT_COUNT; ++i) {
        alert_record_t header;
        if (esp_partition_read(s_partition,ALERT_OFFSET + i * ALERT_SLOT_BYTES,&header,sizeof(header)) == ESP_OK &&
            header_valid(&header,i) && validate_pcm(i,&header) == ESP_OK) {
            s_records[i] = header; ++s_status.ready;
        }
    }
    return ESP_OK;
}
const char *hisu_alert_phrase(unsigned index) { return index < HISU_ALERT_COUNT ? phrases[index] : NULL; }
void hisu_alert_get(hisu_alert_status_t *out)
{
    *out = (hisu_alert_status_t){0};
    if (!s_lock) { out->error = ESP_ERR_INVALID_STATE; return; }
    lock(); *out = s_status; unlock();
}
esp_err_t hisu_alert_request(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    lock();
    if (s_status.busy || s_status.requested) { unlock(); return ESP_ERR_INVALID_STATE; }
    s_status.error = ESP_OK; s_status.http_status = 0;
    s_status.requested = s_status.ready < HISU_ALERT_COUNT;
    unlock(); return ESP_OK;
}
bool hisu_alert_prepare_take(unsigned *index)
{
    if (!s_lock) return false;
    lock();
    if (!s_status.requested || s_status.busy) { unlock(); return false; }
    for (unsigned i = 0; i < HISU_ALERT_COUNT; ++i) if (!s_records[i].bytes) {
        s_status.busy = true; s_status.current = i; *index = i; unlock(); return true;
    }
    s_status.requested = false; unlock(); return false;
}
void hisu_alert_prepare_finish(esp_err_t error, int status)
{
    if (!s_lock) return;
    lock(); s_status.busy = false; s_status.error = error; s_status.http_status = status;
    if (error != ESP_OK || s_status.ready == HISU_ALERT_COUNT) s_status.requested = false;
    unlock();
}
esp_err_t hisu_alert_store(unsigned index, const int16_t *pcm, size_t bytes)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (index >= HISU_ALERT_COUNT || !pcm || bytes < 24000 || bytes > HISU_ALERT_PCM_MAX || (bytes & 1U)) return ESP_ERR_INVALID_ARG;
    lock(); bool allowed = s_status.busy && s_status.current == index && !s_records[index].bytes; unlock();
    if (!allowed) return ESP_ERR_INVALID_STATE;
    alert_record_t header = {.magic=0x31505348U,.version=1,.index=index,.voice_crc=crc("Kore",4),
        .phrase_crc=crc(phrases[index],strlen(phrases[index])),.bytes=(uint32_t)bytes,.pcm_crc=crc(pcm,bytes)};
    header.header_crc = crc(&header,sizeof(header));
    size_t base = ALERT_OFFSET + index * ALERT_SLOT_BYTES;
    esp_err_t err = esp_partition_erase_range(s_partition,base,ALERT_SLOT_BYTES);
    if (err == ESP_OK) err = esp_partition_write(s_partition,base + ALERT_DATA_OFFSET,pcm,bytes);
    if (err == ESP_OK) err = validate_pcm(index,&header);
    if (err == ESP_OK) err = esp_partition_write(s_partition,base,&header,sizeof(header));
    if (err == ESP_OK) {
        alert_record_t readback;
        err = esp_partition_read(s_partition,base,&readback,sizeof(readback));
        if (err == ESP_OK && !header_valid(&readback,index)) err = ESP_FAIL;
    }
    if (err == ESP_OK) { lock(); s_records[index] = header; ++s_status.ready; unlock(); }
    return err;
}
size_t hisu_alert_size(unsigned index)
{
    if (!s_lock || index >= HISU_ALERT_COUNT) return 0;
    lock(); size_t n = s_records[index].bytes; unlock(); return n;
}
esp_err_t hisu_alert_read(unsigned index, size_t offset, void *pcm, size_t bytes)
{
    size_t size = hisu_alert_size(index);
    if (!size || !pcm || offset > size || bytes > size - offset || (offset & 1U) || (bytes & 1U)) return ESP_ERR_INVALID_ARG;
    return esp_partition_read(s_partition,ALERT_OFFSET + index * ALERT_SLOT_BYTES + ALERT_DATA_OFFSET + offset,pcm,bytes);
}
int hisu_alert_poll(bool valid, bool charging, int percent, int64_t now)
{
    if (!s_lock) return -1;
    lock();
    int level = hisu_alert_level(&s_policy,valid,charging,percent,now), result = -1;
    if (level) {
        int group = level - 1, pick = (int)(((uint64_t)now >> 16) & 1U);
        if (pick == s_policy.last[group]) pick ^= 1;
        unsigned first = (unsigned)group * 2;
        if (s_records[first + pick].bytes) result = (int)first + pick;
        else if (s_records[first + (pick ^ 1)].bytes) result = (int)first + (pick ^ 1);
    }
    unlock(); return result;
}
void hisu_alert_played(unsigned index)
{
    if (!s_lock || index >= HISU_ALERT_REMINDER) return;
    lock();
    unsigned group = index / 2;
    s_policy.fired |= group ? 3U : 1U;
    s_policy.last[group] = (int)(index % 2);
    unlock();
}
