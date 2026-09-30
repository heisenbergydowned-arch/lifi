#ifndef DS1302_H
#define DS1302_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct
{
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} DS1302_Time;
/* PB0 CE, PB1 SCLK, PB5 IO (open drain + external 4.7k pull-up to 3.3V).
 * Init probes write-protect and disables trickle charge. Never replaces a
 * valid clock with build time. Main context only, interrupts stay enabled. */
bool DS1302_Init(void);
bool DS1302_ReadTime(DS1302_Time *time);
bool DS1302_SetTime(const DS1302_Time *time);
bool DS1302_IsValidTime(const DS1302_Time *time);
/* ISO-8601 China local time: 25 characters + NUL. */
bool DS1302_FormatTime(const DS1302_Time *time, char *out, size_t capacity);
bool DS1302_ParseTime(const char *text, DS1302_Time *time);
/* Reserves battery RAM 0..11 for a checked boot counter; no Flash writes. */
bool DS1302_NextBootCounter(uint32_t *counter);
#endif
