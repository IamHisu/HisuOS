#include "hisu_alert_model.h"

uint32_t hisu_alert_crc_update(uint32_t crc, const void *data, size_t bytes)
{
    const uint8_t *p = data;
    while (bytes--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc;
}

int hisu_alert_level(hisu_alert_policy_t *s, bool valid, bool charging, int percent, int64_t now)
{
    if (!valid || percent < 0 || percent > 100) { s->pending_level = 0; s->low_since = 0; return 0; }
    if (charging) {
        if (!s->charge_since) s->charge_since = now;
        if (now - s->charge_since >= 10000000) s->fired = 0;
        s->pending_level = 0; s->low_since = 0; return 0;
    }
    s->charge_since = 0;
    /* Never open the amplifier on a critically depleted supply. */
    int level = percent > 2 && percent <= 5 ? 2 : percent > 5 && percent <= 15 ? 1 : 0;
    if (!level || (s->fired & (1U << (level - 1)))) { s->pending_level = 0; s->low_since = 0; return 0; }
    if (s->pending_level != level) { s->pending_level = level; s->low_since = now; return 0; }
    return now - s->low_since >= 4000000 ? level : 0;
}
