#include "hisu_setup_dns.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t q[] = {0x12,0x34,1,0,0,1,0,0,0,0,0,0,3,'a','p','p',0,0,1,0,1};
    const uint8_t ip[] = {192,168,4,1};
    uint8_t reply[528];
    size_t n = hisu_setup_dns_reply(q,sizeof(q),ip,reply,sizeof(reply));
    assert(n == sizeof(q) + 16 && reply[0] == 0x12 && reply[1] == 0x34);
    assert(reply[2] == 0x81 && reply[7] == 1);
    assert(!memcmp(reply + n - 4, ip, 4));
    for (size_t len = 0; len < sizeof(q); ++len)
        assert(!hisu_setup_dns_reply(q,len,ip,reply,sizeof(reply)));
    assert(!hisu_setup_dns_reply(q,sizeof(q),ip,reply,sizeof(q)));
    q[18] = 28; /* AAAA: return a bounded empty response, not a bogus IPv6 address. */
    assert(hisu_setup_dns_reply(q,sizeof(q),ip,reply,sizeof(reply)) == sizeof(q));
    assert(reply[7] == 0);
    q[18] = 1;
    q[12] = 0xc0;
    assert(!hisu_setup_dns_reply(q,sizeof(q),ip,reply,sizeof(reply)));
    q[12] = 3; q[5] = 2;
    assert(!hisu_setup_dns_reply(q,sizeof(q),ip,reply,sizeof(reply)));
    q[5] = 1; q[2] = 0x81;
    assert(!hisu_setup_dns_reply(q,sizeof(q),ip,reply,sizeof(reply)));
    uint8_t fuzz[512];
    unsigned seed = 1;
    for (unsigned trial = 0; trial < 10000; ++trial) {
        for (size_t i = 0; i < sizeof(fuzz); ++i) {
            seed = seed * 1664525u + 1013904223u; fuzz[i] = (uint8_t)(seed >> 24);
        }
        assert(hisu_setup_dns_reply(fuzz,trial % 513,ip,reply,sizeof(reply)) <= sizeof(reply));
    }
    puts("Setup DNS: valid A/AAAA, truncation, compression, count, capacity and 10000 fuzz packets passed.");
    return 0;
}
