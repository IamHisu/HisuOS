#include "hisu_memory_model.h"
#include <string.h>

_Static_assert(sizeof(hisu_memory_record_t) == 64, "Fixed flash header");
_Static_assert(sizeof(hisu_memory_record_t) + sizeof(hisu_memory_data_t) <= HISU_MEMORY_SLOT_BYTES,
               "Memory record must fit a slot");

uint32_t hisu_memory_crc(const void *data, size_t bytes)
{
    uint32_t crc = UINT32_MAX;
    const uint8_t *p = data;
    while (bytes--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool hisu_memory_fact_valid(const char *text)
{
    if (!text || !text[0]) return false;
    size_t n = 0;
    while (n <= HISU_MEMORY_FACT_BYTES && text[n]) ++n;
    if (n > HISU_MEMORY_FACT_BYTES || text[0] == ' ' || text[n - 1] == ' ') return false;
    const uint8_t *p = (const uint8_t *)text, *end = p + n;
    while (p < end) {
        uint32_t cp = *p++;
        unsigned more = 0;
        uint32_t minimum = 0;
        if (cp >= 0xc2 && cp <= 0xdf) { more = 1; minimum = 0x80; cp &= 0x1f; }
        else if (cp >= 0xe0 && cp <= 0xef) { more = 2; minimum = 0x800; cp &= 0xf; }
        else if (cp >= 0xf0 && cp <= 0xf4) { more = 3; minimum = 0x10000; cp &= 7; }
        else if (cp >= 0x80) return false;
        if ((size_t)(end - p) < more) return false;
        while (more--) {
            if ((*p & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (*p++ & 0x3f);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
            cp < 32 || (cp >= 0x7f && cp <= 0x9f) ||
            (cp >= 0x202a && cp <= 0x202e) || (cp >= 0x2066 && cp <= 0x2069)) return false;
    }
    return true;
}

bool hisu_memory_data_valid(const hisu_memory_data_t *data)
{
    if (data->count > HISU_MEMORY_COUNT || !data->next_id || data->enabled > 1) return false;
    for (uint32_t i = 0; i < data->count; ++i) {
        const hisu_memory_fact_t *fact = &data->facts[i];
        if (!fact->id || fact->id >= data->next_id ||
            !memchr(fact->text, 0, sizeof(fact->text)) || !hisu_memory_fact_valid(fact->text)) return false;
        for (uint32_t j = 0; j < i; ++j) if (data->facts[j].id == fact->id) return false;
    }
    return true;
}

void hisu_memory_record_make(hisu_memory_record_t *record, uint32_t sequence, const hisu_memory_data_t *data)
{
    *record = (hisu_memory_record_t){.magic = 0x314d5348U, .version = 1, .sequence = sequence,
        .length = sizeof(*data), .crc = hisu_memory_crc(data, sizeof(*data))};
    record->header_crc = hisu_memory_crc(record, sizeof(*record));
}

bool hisu_memory_record_valid(const hisu_memory_record_t *record, const hisu_memory_data_t *data)
{
    hisu_memory_record_t header = *record;
    header.header_crc = 0;
    return record->magic == 0x314d5348U && record->version == 1 &&
        record->length == sizeof(*data) && hisu_memory_crc(&header, sizeof(header)) == record->header_crc &&
        hisu_memory_crc(data, sizeof(*data)) == record->crc && hisu_memory_data_valid(data);
}
