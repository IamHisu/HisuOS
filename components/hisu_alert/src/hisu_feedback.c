#include "hisu_feedback.h"
#include "hisu_alert_model.h"
#include "hisu_storage_layout.h"
#include "esp_partition.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct { uint32_t magic, version, index, voice_crc, phrase_crc, bytes, pcm_crc, header_crc, reserved[8]; } feedback_record_t;
_Static_assert(sizeof(feedback_record_t)==64,"Fixed feedback header");
_Static_assert(HISU_FEEDBACK_OFFSET==HISU_MEMORY_OFFSET + 2U*HISU_MEMORY_SLOT_BYTES + 5U*0x60000U + 2U*8192U,"Feedback follows schedule A/B");
_Static_assert(HISU_FEEDBACK_OFFSET + HISU_FEEDBACK_COUNT * HISU_FEEDBACK_SLOT_BYTES <= 0x4e0000U,"Feedback fits storage");
static const char *const feedback_phrases[] = {
#define HISU_FEEDBACK_PHRASE(text) text,
#include "hisu_feedback_phrases.inc"
#undef HISU_FEEDBACK_PHRASE
};
static const esp_partition_t *feedback_partition;
static size_t feedback_bytes[HISU_FEEDBACK_COUNT];
static uint32_t feedback_crc(const void *p,size_t n) { return ~hisu_alert_crc_update(UINT32_MAX,p,n); }
void hisu_feedback_init(void)
{
    memset(feedback_bytes,0,sizeof(feedback_bytes));
    feedback_partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"storage");
    if (!feedback_partition || feedback_partition->size < HISU_FEEDBACK_OFFSET + HISU_FEEDBACK_COUNT * HISU_FEEDBACK_SLOT_BYTES) return;
    for (unsigned i=0;i<HISU_FEEDBACK_COUNT;++i) {
        feedback_record_t record,header;
        size_t base=HISU_FEEDBACK_OFFSET+i*HISU_FEEDBACK_SLOT_BYTES;
        if (esp_partition_read(feedback_partition,base,&record,sizeof(record))!=ESP_OK) continue;
        header=record; header.header_crc=0;
        if (record.magic!=0x31434648U || record.version!=1 || record.index!=i ||
            record.voice_crc!=feedback_crc("Leda:vi-southern-v1",sizeof("Leda:vi-southern-v1")-1) ||
            record.phrase_crc!=feedback_crc(feedback_phrases[i],strlen(feedback_phrases[i])) ||
            record.bytes<24000 || record.bytes>HISU_FEEDBACK_SLOT_BYTES-4096 || (record.bytes&1U) ||
            record.header_crc!=feedback_crc(&header,sizeof(header))) continue;
        uint8_t chunk[512]; uint32_t sum=UINT32_MAX; bool valid=true;
        for (size_t offset=0;offset<record.bytes;) {
            size_t n=record.bytes-offset; if (n>sizeof(chunk)) n=sizeof(chunk);
            if (esp_partition_read(feedback_partition,base+4096+offset,chunk,n)!=ESP_OK) { valid=false; break; }
            sum=hisu_alert_crc_update(sum,chunk,n); offset+=n;
        }
        if (valid && ~sum==record.pcm_crc) feedback_bytes[i]=record.bytes;
    }
}
size_t hisu_feedback_size(unsigned index) { return index<HISU_FEEDBACK_COUNT ? feedback_bytes[index] : 0; }
esp_err_t hisu_feedback_read(unsigned index,size_t offset,void *pcm,size_t bytes)
{
    size_t size=hisu_feedback_size(index);
    if (!size || !pcm || offset>size || bytes>size-offset || ((offset|bytes)&1U)) return ESP_ERR_INVALID_ARG;
    return esp_partition_read(feedback_partition,HISU_FEEDBACK_OFFSET+index*HISU_FEEDBACK_SLOT_BYTES+4096+offset,pcm,bytes);
}
