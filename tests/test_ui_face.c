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
        hisu_music_frame(ms,energy,&frame);
        printf("{\"bars\":[");
        for (int i=0;i<HISU_MUSIC_BARS;++i) printf("%s[%d,%d,%u]",i ? "," : "",frame.bars[i].x,frame.bars[i].height,frame.bars[i].color);
        printf("],\"notes\":[");
        for (int i=0;i<HISU_MUSIC_NOTES;++i) printf("%s[%d,%d,%d,%u,%d]",i ? "," : "",frame.notes[i].x,frame.notes[i].y,frame.notes[i].size,frame.notes[i].color,frame.notes[i].kind);
        printf("],\"background\":[");
        for (unsigned x=0;x<240;x+=12) printf("%s%u",x ? "," : "",hisu_music_color(x+6));
        printf("],\"clef\":[");
        for (unsigned curve=0;curve<HISU_MUSIC_CLEF_CURVES;++curve) {
            printf("%s[",curve ? "," : "");
            for (unsigned step=0;step<=HISU_MUSIC_CLEF_STEPS;++step) {
                hisu_music_point_t p=hisu_music_clef_point(curve,step,frame.clef_x,frame.clef_y,frame.clef_size);
                printf("%s[%d,%d]",step ? "," : "",p.x,p.y);
            }
            printf("]");
        }
        printf("]}"); return 0;
    }
    int16_t silence[4]={0}, loud[4]={32767,-32768,16000,-16000};
    assert(!hisu_music_pcm_level(silence,4,100));
    assert(!hisu_music_pcm_level(loud,4,0));
    assert(hisu_music_pcm_level(loud,4,100)==255);
    for (uint32_t t=0;t<15000;t+=50) for (unsigned level=0;level<=255;level+=51) {
        hisu_music_frame_t frame; hisu_music_frame(t,level,&frame);
        for (int i=0;i<HISU_MUSIC_BARS;++i) {
            assert(frame.bars[i].x>=0 && frame.bars[i].x<240);
            assert(86-frame.bars[i].height-4>=0 && 86+frame.bars[i].height+5<164);
            if (!level) assert(frame.bars[i].height==1);
        }
        for (int i=0;i<HISU_MUSIC_NOTES;++i) {
            assert(frame.notes[i].x-2>=0 && frame.notes[i].x+frame.notes[i].size+11<=240);
            assert(frame.notes[i].y-19>=0 && frame.notes[i].y+frame.notes[i].size+2<164);
        }
        for (unsigned curve=0;curve<HISU_MUSIC_CLEF_CURVES;++curve)
            for (unsigned step=0;step<=HISU_MUSIC_CLEF_STEPS;++step) {
                hisu_music_point_t p=hisu_music_clef_point(curve,step,frame.clef_x,frame.clef_y,frame.clef_size);
                assert(p.x>=0 && p.x<240 && p.y>=0 && p.y<164);
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
