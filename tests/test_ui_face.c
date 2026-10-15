#include <assert.h>
#include <stdio.h>
#include "hisu_ui_face.h"

int main(void)
{
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
