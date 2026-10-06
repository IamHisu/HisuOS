/* Host runner: cc -Icomponents/hisu_clock/include tests/test_calendar.c
 * components/hisu_clock/src/hisu_calendar.c -lm -o calendar-test
 */
#include "hisu_calendar.h"
#include <assert.h>
#include <stdio.h>
#ifdef _MSC_VER
#include <stdlib.h>
#endif
static void check(int d,int m,int y,int ld,int lm,int ly,bool leap)
{
    hisu_lunar_date_t date;
    assert(hisu_calendar_lunar(d,m,y,&date));
    assert(date.day == ld && date.month == lm && date.year == ly && date.leap == leap);
}
int main(void)
{
#ifdef _MSC_VER
    /* Keep failed host assertions noninteractive on Windows. */
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    check(10,2,2024,1,1,2024,false);
    check(29,1,2025,1,1,2025,false);
    check(17,2,2026,1,1,2026,false);
    check(16,2,2026,29,12,2025,false);
    check(17,9,2024,15,8,2024,false);
    check(25,7,2025,1,6,2025,true);
    hisu_lunar_date_t date;
    assert(hisu_calendar_lunar(7,5,2054,&date));
    assert(date.day >= 1 && date.day <= 30);
    assert(hisu_calendar_lunar(9,4,2062,&date));
    assert(date.day >= 1 && date.day <= 30);
    assert(!hisu_calendar_lunar(30,2,2024,&date));
    assert(!hisu_calendar_lunar(1,1,2100,&date));
    int first = hisu_calendar_jdn(1,1,2024), last = hisu_calendar_jdn(31,12,2099);
    for (int jd = first; jd <= last; ++jd) {
        int d,m,y;
        hisu_calendar_date(jd,&d,&m,&y);
        assert(hisu_calendar_jdn(d,m,y) == jd);
        assert(hisu_calendar_lunar(d,m,y,&date));
        assert(date.day >= 1 && date.day <= 30 && date.month >= 1 && date.month <= 12);
    }
    int d,m,y;
    hisu_calendar_date(hisu_calendar_jdn(1,3,2024)-1,&d,&m,&y);
    assert(d == 29 && m == 2 && y == 2024);
    hisu_calendar_date(hisu_calendar_jdn(31,12,2025)+1,&d,&m,&y);
    assert(d == 1 && m == 1 && y == 2026);
    puts("Calendar tests passed");
}
