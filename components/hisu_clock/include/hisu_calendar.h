#pragma once
#include <stdbool.h>
typedef struct { int day, month, year; bool leap; } hisu_lunar_date_t;
int hisu_calendar_jdn(int day, int month, int year);
void hisu_calendar_date(int jdn, int *day, int *month, int *year);
bool hisu_calendar_lunar(int day, int month, int year, hisu_lunar_date_t *out);
