#include "DS1302.h"
#include <stdio.h>
#include <string.h>
bool DS1302_IsValidTime(const DS1302_Time *time)
{
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t max_day;
    if (time == NULL || time->year < 2000U || time->year > 2099U ||
        time->month < 1U || time->month > 12U || time->hour > 23U ||
        time->minute > 59U || time->second > 59U) return false;
    max_day = days[time->month - 1U];
    if (time->month == 2U && time->year % 4U == 0U) ++max_day;
    return time->day >= 1U && time->day <= max_day;
}
bool DS1302_FormatTime(const DS1302_Time *time, char *out, size_t capacity)
{
    int count;
    if (out == NULL || capacity < 26U || !DS1302_IsValidTime(time)) return false;
    count = snprintf(out, capacity, "%04u-%02u-%02uT%02u:%02u:%02u+08:00",
                     (unsigned)time->year, (unsigned)time->month, (unsigned)time->day,
                     (unsigned)time->hour, (unsigned)time->minute, (unsigned)time->second);
    return count == 25;
}
bool DS1302_ParseTime(const char *text, DS1302_Time *time)
{
    DS1302_Time parsed;
    unsigned i;
    if (text == NULL || time == NULL || strlen(text) != 25U) return false;
    if (text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':' || strcmp(text + 19, "+08:00") != 0) return false;
    for (i = 0U; i < 19U; ++i)
    {
        if (i == 4U || i == 7U || i == 10U || i == 13U || i == 16U) continue;
        if (text[i] < '0' || text[i] > '9') return false;
    }
#define TWO(pos) ((uint8_t)((text[pos] - '0') * 10 + text[(pos) + 1U] - '0'))
    parsed.year = (uint16_t)(100U * TWO(0) + TWO(2));
    parsed.month = TWO(5); parsed.day = TWO(8); parsed.hour = TWO(11);
    parsed.minute = TWO(14); parsed.second = TWO(17);
#undef TWO
    if (!DS1302_IsValidTime(&parsed)) return false;
    *time = parsed;
    return true;
}
