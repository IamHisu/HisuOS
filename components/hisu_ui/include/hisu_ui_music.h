#pragma once
#include <stdint.h>
#include <stddef.h>

#define HISU_MUSIC_BARS 96
#define HISU_MUSIC_NOTES 6
typedef struct { int x, height; uint32_t color; } hisu_music_bar_t;
typedef struct { int x, y, size, kind; uint32_t color; } hisu_music_note_t;
typedef struct { int x, y; } hisu_music_point_t;
#define HISU_MUSIC_CLEF_CURVES 8
#define HISU_MUSIC_CLEF_STEPS 6
/* Original cubic strokes for a treble clef, in a 40x100 coordinate space. */
static const int8_t hisu_music_clef_curves[HISU_MUSIC_CLEF_CURVES][8]={
    {24,5,35,-4,37,11,25,25}, {25,25,15,37,3,44,3,57},
    {3,57,3,76,32,78,36,60}, {36,60,40,44,18,38,13,53},
    {13,53,9,62,21,67,26,59}, {24,5,16,24,24,58,28,79},
    {28,79,32,96,15,100,13,89}, {13,89,11,80,21,79,21,86}
};
static inline hisu_music_point_t hisu_music_clef_point(unsigned curve, unsigned step, int x, int y, int size)
{
    const int8_t *p=hisu_music_clef_curves[curve];
    int t=(int)step,u=HISU_MUSIC_CLEF_STEPS-t,n=HISU_MUSIC_CLEF_STEPS;
    int px=u*u*u*p[0]+3*u*u*t*p[2]+3*u*t*t*p[4]+t*t*t*p[6];
    int py=u*u*u*p[1]+3*u*u*t*p[3]+3*u*t*t*p[5]+t*t*t*p[7];
    return (hisu_music_point_t){x+px*size/(n*n*n*100),y+py*size/(n*n*n*100)};
}
static inline uint32_t hisu_music_color(unsigned x)
{
    static const uint32_t palette[]={0x00cfff,0x4657fa,0xd517c5,0xff2959,0xffa329,0x7aff31};
    if (x>239) x=239;
    unsigned position=x*5,part=position/239,weight=position%239;
    if (part==5) return palette[5];
    uint32_t a=palette[part],b=palette[part+1],color=0;
    for (unsigned shift=0;shift<=16;shift+=8)
        color|=((((a>>shift)&255)*(239-weight)+((b>>shift)&255)*weight)/239)<<shift;
    return color;
}
typedef struct {
    hisu_music_bar_t bars[HISU_MUSIC_BARS];
    hisu_music_note_t notes[HISU_MUSIC_NOTES];
    int clef_x, clef_y, clef_size;
    unsigned energy;
} hisu_music_frame_t;

/* Amplitude envelope, not a fabricated FFT or BPM estimate. Works for mono/stereo PCM. */
static inline unsigned hisu_music_pcm_level(const int16_t *pcm, size_t samples, unsigned volume)
{
    if (!pcm || !samples || !volume) return 0;
    uint64_t sum=0;
    for (size_t i=0;i<samples;++i) {
        int32_t value=pcm[i]; sum+=(unsigned)(value<0 ? -value : value);
    }
    if (volume>100) volume=100;
    unsigned level=(unsigned)(sum/samples)*volume/100/48;
    return level>255 ? 255 : level;
}

static inline void hisu_music_frame(uint32_t ms, unsigned energy, hisu_music_frame_t *frame)
{
    if (energy>255) energy=255;
    frame->energy=energy;
    for (int i=0;i<HISU_MUSIC_BARS;++i) {
        unsigned distance=(unsigned)(i>42 ? i-42 : 42-i);
        unsigned envelope=distance<32 ? 80-distance*2 : 12;
        unsigned pulse=(unsigned)((i*29+(int)(ms/70)*13)%47);
        unsigned spike=(i%7==0 || i%11==0) ? 25 : 0;
        int x=i*239/(HISU_MUSIC_BARS-1);
        frame->bars[i]=(hisu_music_bar_t){.x=x,
            .height=1+(int)(energy*envelope*(pulse+spike+9)/26000),.color=hisu_music_color((unsigned)x)};
    }
    for (int i=0;i<HISU_MUSIC_NOTES;++i) {
        unsigned phase=(ms/(unsigned)(19+i*3)+(unsigned)i*29)%126;
        int x=9+i*37+(int)((ms/800+(unsigned)i)%5)-2,y=148-(int)phase;
        if (x>35 && x<82 && y<100) x+=32;
        frame->notes[i]=(hisu_music_note_t){.x=x,.y=y,.size=5+(int)(energy/128),
            .kind=i%3,.color=hisu_music_color((unsigned)x)};
    }
    frame->clef_x=41+(int)(ms/1100%3);
    frame->clef_y=5+(int)(ms/650%4);
    frame->clef_size=76+(int)(energy/85);
}
