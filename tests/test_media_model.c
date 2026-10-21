#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hisu_media_model.h"

static uint8_t pixels[HISU_MEDIA_PIXEL_BYTES];
static void checksum(hisu_media_record_t *r)
{
    r->header_crc = hisu_media_crc(r, offsetof(hisu_media_record_t, header_crc));
}

int main(void)
{
    assert(!hisu_media_session_should_close(3600000000LL,0,0,0));
    assert(!hisu_media_session_should_close(89,90,0,0));
    assert(hisu_media_session_should_close(90,90,0,0));
    assert(!hisu_media_session_should_close(7,90,8,0));
    assert(hisu_media_session_should_close(8,90,8,0));
    assert(!hisu_media_session_should_close(10000000,0,0,1));
    assert(hisu_media_session_should_close(10000001,0,0,1));
    assert(!hisu_media_session_should_close(10000001,0,0,0));
    uint8_t mapped[16] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff,192,168,4,2};
    uint32_t ipv4 = 0;
    const uint8_t expected[4] = {192,168,4,2};
    assert(hisu_media_ipv4_mapped(mapped,&ipv4));
    assert(!memcmp(&ipv4,expected,sizeof(expected)));
    assert(!hisu_media_ipv4_mapped(NULL,&ipv4));
    assert(!hisu_media_ipv4_mapped(mapped,NULL));
    mapped[10] = 0;
    assert(!hisu_media_ipv4_mapped(mapped,&ipv4));
    mapped[10] = 0xff; mapped[0] = 0x20;
    assert(!hisu_media_ipv4_mapped(mapped,&ipv4));
    memset(mapped,0,sizeof(mapped)); mapped[15] = 1;
    assert(!hisu_media_ipv4_mapped(mapped,&ipv4));
    assert(hisu_media_http_host_valid("192.168.4.1", "192.168.4.1"));
    assert(hisu_media_http_host_valid("192.168.4.1:80", "192.168.4.1"));
    assert(!hisu_media_http_host_valid(NULL, "192.168.4.1"));
    assert(!hisu_media_http_host_valid("192", "192.168.4.1"));
    assert(!hisu_media_http_host_valid("192.168.4.1:8443", "192.168.4.1"));
    assert(!hisu_media_http_host_valid("192.168.4.1.evil", "192.168.4.1"));
    assert(hisu_media_http_origin_valid("http://192.168.4.1", "192.168.4.1"));
    assert(!hisu_media_http_origin_valid(NULL, "192.168.4.1"));
    assert(!hisu_media_http_origin_valid("null", "192.168.4.1"));
    assert(!hisu_media_http_origin_valid("https://192.168.4.1", "192.168.4.1"));
    assert(!hisu_media_http_origin_valid("http://192.168.4.1.evil", "192.168.4.1"));
    assert(!hisu_media_http_origin_valid("http://192.168.4.1/", "192.168.4.1"));
    assert(hisu_media_crc("123456789", 9) == 0xcbf43926U);
    assert(hisu_media_crc_update(hisu_media_crc("123",3),"456789",6)==0xcbf43926U);
    assert(hisu_media_secret_equal("01234567", "01234567", 8));
    assert(!hisu_media_secret_equal("01234567", "01234568", 8));
    assert(!hisu_media_secret_equal("01234567", "11234567", 8));
    hisu_media_record_t record = {.magic=HISU_MEDIA_MAGIC,.version=1,.sequence=1,
        .width=240,.height=240,.length=sizeof(pixels),.flags=1};
    record.crc = hisu_media_crc(pixels,sizeof(pixels)); checksum(&record);
    assert(hisu_media_record_valid(&record,pixels));
    assert(!hisu_media_record_valid(&record,NULL));
    pixels[0] = 1; assert(!hisu_media_record_valid(&record,pixels)); pixels[0] = 0;
    pixels[sizeof(pixels)-1] = 1; assert(!hisu_media_record_valid(&record,pixels)); pixels[sizeof(pixels)-1] = 0;
    hisu_media_record_t bad = record;
    bad.sequence++; assert(!hisu_media_record_header_valid(&bad));
    bad = record; bad.length--; checksum(&bad); assert(!hisu_media_record_header_valid(&bad));
    bad = record; bad.width=241; checksum(&bad); assert(!hisu_media_record_header_valid(&bad));
    bad = record; bad.version=2; checksum(&bad); assert(!hisu_media_record_header_valid(&bad));
    bad = record; bad.flags=3; checksum(&bad); assert(!hisu_media_record_header_valid(&bad));
    bad = record; bad.sequence=0; checksum(&bad); assert(!hisu_media_record_header_valid(&bad));
    bad = record; bad.reserved[0]=1; assert(!hisu_media_record_header_valid(&bad));
    bad = record; bad.magic=UINT32_MAX; checksum(&bad); assert(!hisu_media_record_header_valid(&bad));
    /* Read compatibility only: old firmware could persist an empty record. */
    record.flags=0; record.length=0; record.crc=0; checksum(&record);
    assert(hisu_media_record_valid(&record,NULL));
    record.crc=1; checksum(&record); assert(!hisu_media_record_header_valid(&record));
    assert(!hisu_media_record_header_valid(NULL));
    uint8_t gif[] = {'G','I','F','8','9','a',240,0,240,0,0x80,0,0,
        0,0,0,255,255,255,0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,0x3b};
    assert(hisu_media_gif_valid(gif,sizeof(gif)));
    for (size_t n=0;n<sizeof(gif);++n) assert(!hisu_media_gif_valid(gif,n));
    gif[6]=241; assert(!hisu_media_gif_valid(gif,sizeof(gif))); gif[6]=240;
    gif[6]=120; assert(hisu_media_gif_valid(gif,sizeof(gif))); gif[6]=240;
    gif[23]=241; assert(!hisu_media_gif_valid(gif,sizeof(gif))); gif[23]=0;
    record.flags=2; record.length=sizeof(gif); record.crc=hisu_media_crc(gif,sizeof(gif)); checksum(&record);
    assert(hisu_media_record_valid(&record,gif));
    gif[sizeof(gif)-1]=0; assert(!hisu_media_record_valid(&record,gif));

    puts("Media model tests passed (IPv4-mapped IPv6, Host/Origin, CRC, torn header, image bounds, tombstone, secret comparison).");
    return 0;
}
