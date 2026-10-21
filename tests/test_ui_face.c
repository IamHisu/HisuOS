#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "hisu_ui_face.h"
#include "hisu_ui_music.h"

int main(int argc, char **argv)
{
    if (argc==4 && !strcmp(argv[1],"--music-json")) {
        hisu_music_frame_t frame;
        unsigned ms=(unsigned)strtoul(argv[2],NULL,10),energy=(unsigned)strtoul(argv[3],NULL,10);
        hisu_music_scene_t scene; hisu_music_scene_init(&scene,42,0);
        for (unsigned t=0;t<=ms;t+=50)
            hisu_music_frame(t,t%500<100 ? energy : energy/4,&scene,&frame);
        printf("{\"bars\":[");
        for (int i=0;i<HISU_MUSIC_BARS;++i) printf("%s[%d,%d,%u]",i ? "," : "",frame.bars[i].x,frame.bars[i].height,frame.bars[i].color);
        printf("],\"notes\":[");
        for (int i=0;i<HISU_MUSIC_NOTES;++i) printf("%s[%d,%d,%d,%u,%d,%u,%u]",i ? "," : "",frame.notes[i].x,frame.notes[i].y,frame.notes[i].size,frame.notes[i].color,frame.notes[i].kind,frame.notes[i].phase,frame.notes[i].opacity);
        printf("],\"sparks\":[");
        for (unsigned i=0;i<frame.spark_count;++i) {
            hisu_music_spark_t spark=frame.sparks[i];
            printf("%s[%d,%d,%d,%d,%u,%u]",i ? "," : "",spark.x,spark.y,spark.tail_x,spark.tail_y,spark.color,spark.opacity);
        }
        printf("],\"background\":[");
        for (unsigned x=0;x<240;x+=12) printf("%s%u",x ? "," : "",hisu_music_color(x+6));
        printf("],\"baseline\":%d,\"view_y\":%d,\"height\":%d,\"energy\":%u}",
            HISU_MUSIC_BASELINE,HISU_MUSIC_VIEW_Y,HISU_MUSIC_VIEW_HEIGHT,frame.energy); return 0;
    }
    int16_t silence[4]={0}, loud[4]={32767,-32768,16000,-16000};
    assert(!hisu_music_pcm_level(silence,4,100));
    assert(!hisu_music_pcm_level(loud,4,0));
    assert(hisu_music_pcm_level(loud,4,100)==255);
    hisu_music_scene_t quiet_scene,loud_scene;
    hisu_music_scene_init(&quiet_scene,42,0); hisu_music_scene_init(&loud_scene,42,0);
    hisu_music_frame_t quiet_frame,loud_frame;
    hisu_music_frame(500,0,&quiet_scene,&quiet_frame);
    hisu_music_frame(500,255,&loud_scene,&loud_frame);
    assert(!memcmp(quiet_frame.notes,loud_frame.notes,sizeof(quiet_frame.notes))); /* No beat bobbing. */
    assert(quiet_scene.notes[0].target_y!=quiet_scene.notes[1].target_y || quiet_scene.notes[0].x!=quiet_scene.notes[1].x);
    /* Eligible notes wait for an onset; rising ones cannot burst early. */
    hisu_music_scene_init(&loud_scene,42,0);
    for (unsigned i=0;i<HISU_MUSIC_NOTES;++i) { loud_scene.notes[i].phase=HISU_NOTE_WAIT; loud_scene.notes[i].phase_ms=0; }
    hisu_music_frame(50,0,&loud_scene,&loud_frame); assert(!loud_frame.spark_count);
    hisu_music_frame(100,180,&loud_scene,&loud_frame);
    assert(HISU_MUSIC_SPARKS_PER_NOTE==32 && loud_frame.spark_count==128);
    assert(sizeof(hisu_music_spark_t)==16 && sizeof(hisu_music_frame_t)<3072);
    for (unsigned i=0;i<HISU_MUSIC_MAX_BURSTS;++i) assert(loud_frame.notes[i].phase==HISU_NOTE_BURST);
    assert(loud_frame.notes[4].phase==HISU_NOTE_WAIT);
    hisu_music_frame(700,180,&loud_scene,&loud_frame);
    assert(loud_frame.spark_count);
    for (unsigned i=0;i<loud_frame.spark_count;++i) {
        assert(loud_frame.sparks[i].y>=loud_frame.sparks[i].tail_y); /* Falling, not static petals. */
        assert(loud_frame.sparks[i].opacity<=51);
    }
    hisu_music_frame(900,180,&loud_scene,&loud_frame);
    assert(loud_frame.notes[0].phase==HISU_NOTE_RISE && loud_frame.notes[1].phase==HISU_NOTE_RISE);
    hisu_music_scene_init(&quiet_scene,42,0); quiet_scene.notes[0].phase=HISU_NOTE_WAIT;
    hisu_music_frame(1250,0,&quiet_scene,&quiet_frame); assert(quiet_frame.notes[0].phase==HISU_NOTE_FADE && !quiet_frame.spark_count);
    hisu_music_frame(1650,0,&quiet_scene,&quiet_frame); assert(quiet_frame.notes[0].phase==HISU_NOTE_RISE);
    for (unsigned level=0;level<=255;level+=51) {
    hisu_music_scene_t scene; hisu_music_scene_init(&scene,42,0);
    for (uint32_t t=0;t<60000;t+=50) {
        hisu_music_frame_t frame; hisu_music_frame(t,t%500<100 ? level : level/4,&scene,&frame);
        for (int i=0;i<HISU_MUSIC_BARS;++i) {
            assert(frame.bars[i].x>=0 && frame.bars[i].x<240);
            assert(frame.bars[i].x+3<=240);
            assert(frame.bars[i].height>=0 && HISU_MUSIC_BASELINE-frame.bars[i].height-2>=0);
            if (!level) assert(frame.bars[i].height==0);
        }
        for (int i=0;i<HISU_MUSIC_NOTES;++i) {
            assert(frame.notes[i].x-3>=0 && frame.notes[i].x+frame.notes[i].size+14<=240);
            assert(frame.notes[i].y-22>=0 && frame.notes[i].y+frame.notes[i].size+3<HISU_MUSIC_VIEW_HEIGHT);
        }
        unsigned active=0,bursts=0;
        for (unsigned i=0;i<HISU_MUSIC_NOTES;++i) {
            active+=frame.notes[i].phase!=HISU_NOTE_REST;
            bursts+=frame.notes[i].phase==HISU_NOTE_BURST;
            if (!level) assert(frame.notes[i].phase!=HISU_NOTE_BURST);
        }
        assert(active>=3 && active<=5 && bursts<=HISU_MUSIC_MAX_BURSTS && frame.spark_count<=HISU_MUSIC_SPARKS);
        for (unsigned i=0;i<frame.spark_count;++i) {
            assert(frame.sparks[i].x>=2 && frame.sparks[i].x<=237 && frame.sparks[i].y>=2 && frame.sparks[i].y<=165);
            assert(frame.sparks[i].opacity<=255);
        }
    }
    }
    hisu_face_t face;
    assert(hisu_face_parse("happy", &face) && face == HISU_FACE_HAPPY);
    assert(!hisu_face_parse(NULL, &face));
    assert(!hisu_face_parse("not-a-face", &face));
    for (int f = 0; f < HISU_FACE_COUNT; ++f) {
        for (uint32_t t = 0; t < 15000; t += 25) {
            hisu_eye_t eyes[2];
            hisu_face_frame((hisu_face_t)f, true, true, t, eyes);
            for (int i = 0; i < 2; ++i) {
                assert(eyes[i].x >= 0 && eyes[i].width > 0 && eyes[i].height >= 4);
                assert(eyes[i].x + eyes[i].width <= 240);
                assert(eyes[i].y >= 40 && eyes[i].y + eyes[i].height < 164);
            }
            assert(eyes[0].x + eyes[0].width < eyes[1].x);
        }
    }
    hisu_eye_t open[2], closed[2];
    hisu_face_frame(HISU_FACE_NEUTRAL, false, false, 500, open);
    hisu_face_frame(HISU_FACE_NEUTRAL, false, false, 75, closed);
    assert(open[0].height == 58 && closed[0].height == 4);
    puts("Face geometry tests passed: all expressions, blinking, 240x240 bounds, subtitle separation.");
    return 0;
}
