/* Vietnamese calendar rules: Ho Ngoc Duc, https://www.xemamlich.uhm.vn/calrules.html.
 * Independent C implementation of the published astronomical equations.
 * Calendar dates use Vietnam's UTC+7 meridian, including leap months.
 */
#include "hisu_calendar.h"
#include <math.h>
#define PI 3.14159265358979323846
int hisu_calendar_jdn(int day, int month, int year)
{
    int a = (14-month)/12, y = year+4800-a, m = month+12*a-3;
    return day+(153*m+2)/5+365*y+y/4-y/100+y/400-32045;
}
void hisu_calendar_date(int jd, int *day, int *month, int *year)
{
    int a = jd+32044, b = (4*a+3)/146097, c = a-146097*b/4;
    int d = (4*c+3)/1461, e = c-1461*d/4, m = (5*e+2)/153;
    *day = e-(153*m+2)/5+1; *month = m+3-12*(m/10); *year = 100*b+d-4800+m/10;
}
static int new_moon(int k)
{
    double t = k/1236.85, t2 = t*t, t3 = t2*t, r = PI/180;
    double jd = 2415020.75933+29.53058868*k+0.0001178*t2-0.000000155*t3;
    jd += 0.00033*sin((166.56+132.87*t-0.009173*t2)*r);
    double sun = (359.2242+29.10535608*k-0.0000333*t2-0.00000347*t3)*r;
    double moon = (306.0253+385.81691806*k+0.0107306*t2+0.00001236*t3)*r;
    double lat = (21.2964+390.67050646*k-0.0016528*t2-0.00000239*t3)*r;
    double correction = (0.1734-0.000393*t)*sin(sun)+0.0021*sin(2*sun)
        -0.4068*sin(moon)+0.0161*sin(2*moon)-0.0004*sin(3*moon)
        +0.0104*sin(2*lat)-0.0051*sin(sun+moon)-0.0074*sin(sun-moon)
        +0.0004*sin(2*lat+sun)-0.0004*sin(2*lat-sun)-0.0006*sin(2*lat+moon)
        +0.0010*sin(2*lat-moon)+0.0005*sin(2*moon+sun);
    double delta = -0.000278+0.000265*t+0.000262*t2;
    return (int)floor(jd+correction-delta+0.5+7.0/24);
}
static int solar_sector(int jd)
{
    double t = (jd-2451545.5-7.0/24)/36525, t2 = t*t, r = PI/180;
    double anomaly = (357.52910+35999.05030*t-0.0001559*t2-0.00000048*t*t2)*r;
    double longitude = 280.46645+36000.76983*t+0.0003032*t2
        +(1.914600-0.004817*t-0.000014*t2)*sin(anomaly)
        +(0.019993-0.000101*t)*sin(2*anomaly)+0.000290*sin(3*anomaly);
    longitude *= r;
    longitude -= 2*PI*floor(longitude/(2*PI));
    return (int)floor(longitude*6/PI);
}
static int month_eleven(int year)
{
    int k = (int)floor((hisu_calendar_jdn(31,12,year)-2415021)/29.530588853);
    int jd = new_moon(k);
    return solar_sector(jd) >= 9 ? new_moon(k-1) : jd;
}
static int leap_offset(int start)
{
    int k = (int)floor((start-2415021.076998695)/29.530588853+0.5);
    int sector = solar_sector(new_moon(k+1));
    for (int i = 2; i <= 14; ++i) {
        int next = solar_sector(new_moon(k+i));
        if (next == sector || i == 14) return i-1;
        sector = next;
    }
    return 13;
}
bool hisu_calendar_lunar(int day, int month, int year, hisu_lunar_date_t *out)
{
    if (!out || year < 2024 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) return false;
    int jd = hisu_calendar_jdn(day,month,year), d,m,y;
    hisu_calendar_date(jd,&d,&m,&y);
    if (d != day || m != month || y != year) return false;
    int k = (int)floor((jd-2415021.076998695)/29.530588853);
    int start = new_moon(k+1);
    if (start > jd) start = new_moon(k);
    int a = month_eleven(year), b = a, lunar_year = year;
    if (a >= start) a = month_eleven(year-1);
    else { lunar_year++; b = month_eleven(year+1); }
    int diff = (start-a)/29, lunar_month = diff+11;
    bool leap = false;
    if (b-a > 365) {
        int offset = leap_offset(a);
        if (diff >= offset) { lunar_month = diff+10; leap = diff == offset; }
    }
    if (lunar_month > 12) lunar_month -= 12;
    if (lunar_month >= 11 && diff < 4) lunar_year--;
    *out = (hisu_lunar_date_t){jd-start+1,lunar_month,lunar_year,leap};
    return true;
}
