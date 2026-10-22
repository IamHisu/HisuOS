#pragma once
#include <stdint.h>
#include <stddef.h>

#define HISU_MUSIC_BARS 48
#define HISU_MUSIC_NOTES 5
#define HISU_MUSIC_SPARKS_PER_NOTE 32
#define HISU_MUSIC_MAX_BURSTS 4
#define HISU_MUSIC_SPARKS (HISU_MUSIC_SPARKS_PER_NOTE*HISU_MUSIC_MAX_BURSTS)
#define HISU_MUSIC_VIEW_Y 72
#define HISU_MUSIC_VIEW_HEIGHT 168
#define HISU_MUSIC_BASELINE (HISU_MUSIC_VIEW_HEIGHT-1)

typedef struct { int current_height, next_y, next_height; } hisu_music_lyric_layout_t;

static inline hisu_music_lyric_layout_t hisu_music_lyric_layout(int measured_height)
{
    int height=measured_height>44 ? 66 : measured_height>22 ? 44 : 22;
    return (hisu_music_lyric_layout_t){height,height,height<66 ? 22 : 0};
}
typedef struct { int x, height; uint32_t color; } hisu_music_bar_t;
enum { HISU_NOTE_REST, HISU_NOTE_RISE, HISU_NOTE_WAIT, HISU_NOTE_BURST, HISU_NOTE_FADE };
typedef struct { int x, y, size, kind; uint32_t color; unsigned phase, opacity; } hisu_music_note_t;
typedef struct { int16_t x, y, tail_x, tail_y; uint32_t color; unsigned opacity; } hisu_music_spark_t;
typedef struct {
    uint32_t born_ms, phase_ms, seed;
    int x, from_y, target_y, drift;
    unsigned flight_ms, phase;
} hisu_music_flight_t;
typedef struct {
    hisu_music_flight_t notes[HISU_MUSIC_NOTES];
    uint32_t random, last_ms, beat_ms;
    unsigned floor, previous;
    int initialized, beat_seen;
} hisu_music_scene_t;
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
    hisu_music_spark_t sparks[HISU_MUSIC_SPARKS];
    unsigned spark_count;
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

static inline unsigned hisu_music_triangle(uint32_t phase)
{
    unsigned p=phase%256;
    return p<128 ? p : 255-p;
}

static inline uint32_t hisu_music_hash(uint32_t value)
{
    value^=value>>16; value*=0x7feb352dU; value^=value>>15;
    value*=0x846ca68bU; return value^(value>>16);
}

static inline void hisu_music_spawn(hisu_music_scene_t *scene,unsigned index,uint32_t ms)
{
    scene->random=hisu_music_hash(scene->random+0x9e3779b9U);
    uint32_t r=scene->random;
    scene->notes[index]=(hisu_music_flight_t){.born_ms=ms,.phase_ms=ms,.seed=r,
        .x=34+(int)(r%164),.from_y=145+(int)((r>>8)%10),
        .target_y=32+(int)((r>>16)%54),.drift=(int)((r>>24)%13)-6,
        .flight_ms=3600+(r%1800),.phase=HISU_NOTE_RISE};
}

static inline void hisu_music_scene_init(hisu_music_scene_t *scene,uint32_t seed,uint32_t ms)
{
    *scene=(hisu_music_scene_t){.random=seed,.last_ms=ms-1,.initialized=1};
    for (unsigned i=0;i<HISU_MUSIC_NOTES;++i) {
        hisu_music_spawn(scene,i,ms-i*650);
        if (i>=3) { scene->notes[i].phase=HISU_NOTE_REST; scene->notes[i].phase_ms=ms+400+(i-3)*900; }
    }
}

/* Audio-onset heuristic, not BPM/FFT. Flights are time-driven; only a bounded
 * group of eligible notes bursts on an energy attack. Flat/silent input fades out. */
static inline void hisu_music_frame(uint32_t ms, unsigned energy,hisu_music_scene_t *scene,hisu_music_frame_t *frame)
{
    if (energy>255) energy=255;
    if (!scene->initialized) hisu_music_scene_init(scene,0x487375U,ms);
    int advance=ms!=scene->last_ms;
    int beat=advance && energy>=36 && energy>scene->previous+20 && energy>scene->floor+18 &&
        (!scene->beat_seen || (uint32_t)(ms-scene->beat_ms)>=240);
    if (advance) {
        scene->floor=(scene->floor*7+energy)/8; scene->previous=energy; scene->last_ms=ms;
        if (beat) { scene->beat_seen=1; scene->beat_ms=ms; }
    }
    frame->energy=energy;
    frame->spark_count=0;
    for (int i=0;i<HISU_MUSIC_BARS;++i) {
        unsigned distance=(unsigned)(i>23 ? i-23 : 23-i);
        unsigned envelope=130-distance*3;
        unsigned pulse=32+hisu_music_triangle(ms/(unsigned)(4+i%5)+(unsigned)i*31);
        int x=1+i*5;
        frame->bars[i]=(hisu_music_bar_t){.x=x,
            .height=(int)(energy*envelope*pulse/40000),.color=hisu_music_color((unsigned)x)};
    }
    unsigned bursts=0;
    for (unsigned i=0;i<HISU_MUSIC_NOTES;++i)
        if (scene->notes[i].phase==HISU_NOTE_BURST && (uint32_t)(ms-scene->notes[i].phase_ms)<750) ++bursts;
    for (unsigned i=0;i<HISU_MUSIC_NOTES;++i) {
        hisu_music_flight_t *flight=&scene->notes[i];
        if (flight->phase==HISU_NOTE_REST && (int32_t)(ms-flight->phase_ms)>=0) hisu_music_spawn(scene,i,ms);
        unsigned age=(uint32_t)(ms-flight->born_ms);
        if (flight->phase==HISU_NOTE_RISE && age>=flight->flight_ms) {
            flight->phase=HISU_NOTE_WAIT; flight->phase_ms=ms;
        }
        if (flight->phase==HISU_NOTE_WAIT) {
            if (beat && bursts<HISU_MUSIC_MAX_BURSTS) { flight->phase=HISU_NOTE_BURST; flight->phase_ms=ms; ++bursts; }
            else if ((uint32_t)(ms-flight->phase_ms)>=1200) { flight->phase=HISU_NOTE_FADE; flight->phase_ms=ms; }
        }
        unsigned phase_age=(uint32_t)(ms-flight->phase_ms);
        if ((flight->phase==HISU_NOTE_BURST && phase_age>=750) ||
            (flight->phase==HISU_NOTE_FADE && phase_age>=350)) {
            if (i<3) hisu_music_spawn(scene,i,ms);
            else { flight->phase=HISU_NOTE_REST; flight->phase_ms=ms+600+flight->seed%1400; }
            age=0; phase_age=0;
        }
        unsigned progress=age<flight->flight_ms ? age : flight->flight_ms;
        int x=flight->x+flight->drift*(int)progress/(int)flight->flight_ms;
        int y=flight->from_y-(flight->from_y-flight->target_y)*(int)progress/(int)flight->flight_ms;
        unsigned opacity=age<200 ? age*235/200 : 235;
        if (flight->phase==HISU_NOTE_FADE) opacity=235*(350-phase_age)/350;
        if (flight->phase==HISU_NOTE_REST || flight->phase==HISU_NOTE_BURST) opacity=0;
        uint32_t color=hisu_music_color(flight->seed%240);
        frame->notes[i]=(hisu_music_note_t){.x=x,.y=y,.size=6,.kind=(int)(flight->seed%3),
            .color=color,.phase=flight->phase,.opacity=opacity};
        if (flight->phase!=HISU_NOTE_BURST) continue;
        /* Streaks follow independent ballistic paths, then fall under gravity. */
        for (unsigned j=0;j<HISU_MUSIC_SPARKS_PER_NOTE && frame->spark_count<HISU_MUSIC_SPARKS;++j) {
            uint32_t r=hisu_music_hash(flight->seed+j*0x9e3779b9U);
            int vx=(int)(r%153)-76,vy=(int)((r>>8)%91)-65;
            int t=(int)phase_age,old=t>100 ? t-100 : 0;
            int px=x+3+vx*t/1000,py=y-7+vy*t/1000+75*t*t/1000000;
            int tx=x+3+vx*old/1000,ty=y-7+vy*old/1000+75*old*old/1000000;
            if (px<2 || px>237 || py<2 || py>165 || tx<2 || tx>237 || ty<2 || ty>165) continue;
            frame->sparks[frame->spark_count++]=(hisu_music_spark_t){.x=(int16_t)px,.y=(int16_t)py,.tail_x=(int16_t)tx,.tail_y=(int16_t)ty,
                .color=hisu_music_color((flight->seed%240+j*7)%240),.opacity=255*(750-phase_age)/750};
        }
    }
}
