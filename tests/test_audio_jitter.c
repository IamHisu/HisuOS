#include <assert.h>
#include <stdio.h>
#include "hisu_audio_jitter.h"

static void fifo_tests(void)
{
    uint8_t storage[9600];
    hisu_audio_jitter_t buffer;
    assert(!hisu_jitter_init(&buffer,NULL,sizeof(storage)));
    assert(!hisu_jitter_init(&buffer,storage,100));
    assert(hisu_jitter_init(&buffer,storage,sizeof(storage)));
    uint16_t source[240], output[240];
    uint32_t produced=0, consumed=0, random_state=17;
    for (unsigned step=0; step<20000; ++step) {
        random_state=random_state*1664525U+1013904223U;
        size_t count=(random_state>>16)%240+1;
        if (random_state&1U) {
            for (size_t sample=0; sample<count; ++sample) source[sample]=(uint16_t)(produced+sample);
            size_t before=buffer.used;
            bool accepted=hisu_jitter_write(&buffer,source,count*2);
            if (accepted) produced+=(uint32_t)count;
            else assert(buffer.used==before);
        } else {
            size_t bytes=hisu_jitter_read(&buffer,output,count*2);
            for (size_t sample=0; sample<bytes/2; ++sample) assert(output[sample]==(uint16_t)(consumed+sample));
            consumed+=(uint32_t)(bytes/2);
        }
        assert(buffer.used==(produced-consumed)*2);
        assert(buffer.used<=buffer.capacity && !(buffer.used%2));
    }
    size_t before=buffer.used;
    assert(!hisu_jitter_write(&buffer,source,3) && buffer.used==before);
    assert(hisu_jitter_read(&buffer,NULL,buffer.used)==before);
    assert(buffer.used==0 && buffer.discarded_bytes==before);
    assert(hisu_jitter_write(&buffer,source,sizeof(source)));
    hisu_jitter_unwrite(&buffer,sizeof(source));
    assert(buffer.used==0);
    assert(hisu_jitter_write(&buffer,source,sizeof(source)));
    assert(hisu_jitter_read(&buffer,output,sizeof(output))==sizeof(output));
    assert(!memcmp(source,output,sizeof(source)));
}

static void timing_tests(void)
{
    assert(!hisu_jitter_ready(179*48,false));
    assert(hisu_jitter_ready(180*48,false));
    assert(hisu_jitter_ready(2,true));
    uint8_t storage[9600];
    hisu_audio_jitter_t buffer;
    assert(hisu_jitter_init(&buffer,storage,sizeof(storage)));
    hisu_jitter_rx(&buffer,480,0);
    hisu_jitter_rx(&buffer,480,20000);
    hisu_jitter_rx(&buffer,480,220000);
    assert(buffer.rx_bytes==1440 && buffer.rx_gaps==1 && buffer.max_rx_gap_us==200000);
    buffer.received=false;
    hisu_jitter_rx(&buffer,480,20000000);
    assert(buffer.rx_gaps==1);
}

int main(void)
{
    fifo_tests(); timing_tests();
    puts("Audio jitter tests passed: 20000 FIFO operations, wrap, overflow, rollback, discard, prebuffer and RX gaps.");
    return 0;
}
