#include "DS1302.h"
#include "main.h"
#include <string.h>

/* At least 5 us at this project's 72 MHz; IRQs may lengthen the clock.
 * The DS1302 synchronous interface permits pauses. No IRQ masking. */
static void Delay(void)
{
    volatile uint32_t i;
    for (i = 0U; i < 360U; ++i) { __NOP(); }
}
static void Clock(bool high)
{
    HAL_GPIO_WritePin(RTC_SCLK_GPIO_Port, RTC_SCLK_Pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    Delay();
}
static void Data(bool high)
{
    /* Open-drain high releases IO for reads; no repeated GPIO init. */
    HAL_GPIO_WritePin(RTC_IO_GPIO_Port, RTC_IO_Pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    Delay();
}
static void Begin(void)
{
    HAL_GPIO_WritePin(RTC_CE_GPIO_Port, RTC_CE_Pin, GPIO_PIN_RESET);
    Clock(false); Data(true);
    HAL_GPIO_WritePin(RTC_CE_GPIO_Port, RTC_CE_Pin, GPIO_PIN_SET);
    Delay();
}
static void End(void)
{
    HAL_GPIO_WritePin(RTC_CE_GPIO_Port, RTC_CE_Pin, GPIO_PIN_RESET);
    Delay(); Clock(false); Data(true);
}
static void WriteByte(uint8_t value, bool read_command)
{
    unsigned bit;
    for (bit = 0U; bit < 8U; ++bit)
    {
        Data((value & 1U) != 0U); Clock(true);
        /* Command bit 7's falling edge outputs D0. Release BEFORE it. */
        if (read_command && bit == 7U) Data(true);
        Clock(false); value >>= 1U;
    }
}
static uint8_t ReadByte(void)
{
    uint8_t value = 0U;
    unsigned bit;
    for (bit = 0U; bit < 8U; ++bit)
    {
        if (HAL_GPIO_ReadPin(RTC_IO_GPIO_Port, RTC_IO_Pin) == GPIO_PIN_SET)
            value |= (uint8_t)(1U << bit);
        Clock(true); Clock(false);
    }
    return value;
}
static uint8_t ReadRegister(uint8_t address)
{
    uint8_t value;
    Begin(); WriteByte((uint8_t)(address | 1U), true);
    value = ReadByte(); End(); return value;
}
static void WriteRegister(uint8_t address, uint8_t value)
{
    Begin(); WriteByte((uint8_t)(address & 0xFEU), false);
    WriteByte(value, false); End();
}
static uint8_t Bcd(uint8_t value) { return (uint8_t)((value / 10U) * 16U + value % 10U); }
static bool DecodeBcd(uint8_t raw, uint8_t *value)
{
    if ((raw & 15U) > 9U || (raw >> 4U) > 9U) return false;
    *value = (uint8_t)((raw >> 4U) * 10U + (raw & 15U)); return true;
}
bool DS1302_Init(void)
{
    End(); WriteRegister(0x8EU, 0U);
    if (ReadRegister(0x8EU) != 0U) return false;
    WriteRegister(0x90U, 0U); /* Trickle charge OFF. */
    WriteRegister(0x8EU, 0x80U);
    return ReadRegister(0x8EU) == 0x80U && ReadRegister(0x90U) == 0U;
}
bool DS1302_ReadTime(DS1302_Time *time)
{
    uint8_t raw[8], year, hour;
    DS1302_Time parsed;
    unsigned i;
    if (time == NULL) return false;
    Begin(); WriteByte(0xBFU, true);
    for (i = 0U; i < 8U; ++i) raw[i] = ReadByte();
    End();
    if ((raw[0] & 0x80U) != 0U || (raw[1] & 0x80U) != 0U ||
        (raw[3] & 0xC0U) != 0U || (raw[4] & 0xE0U) != 0U ||
        raw[5] < 1U || raw[5] > 7U || (raw[7] & 0x7FU) != 0U) return false;
    if (!DecodeBcd(raw[0], &parsed.second) || !DecodeBcd(raw[1], &parsed.minute) ||
        !DecodeBcd(raw[3], &parsed.day) || !DecodeBcd(raw[4], &parsed.month) ||
        !DecodeBcd(raw[6], &year)) return false;
    parsed.year = (uint16_t)(2000U + year);
    if (raw[2] & 0x80U)
    {
        if ((raw[2] & 0x40U) != 0U || !DecodeBcd((uint8_t)(raw[2] & 0x1FU), &hour) ||
            hour < 1U || hour > 12U) return false;
        parsed.hour = (uint8_t)((hour % 12U) + ((raw[2] & 0x20U) ? 12U : 0U));
    }
    else if (!DecodeBcd(raw[2], &parsed.hour)) return false;
    if (!DS1302_IsValidTime(&parsed)) return false;
    *time = parsed; return true;
}
bool DS1302_SetTime(const DS1302_Time *time)
{
    DS1302_Time check;
    uint8_t raw[8];
    unsigned i;
    if (!DS1302_IsValidTime(time)) return false;
    raw[0] = Bcd(time->second); raw[1] = Bcd(time->minute); raw[2] = Bcd(time->hour);
    raw[3] = Bcd(time->day); raw[4] = Bcd(time->month); raw[5] = 1U; /* Weekday unused. */
    raw[6] = Bcd((uint8_t)(time->year - 2000U)); raw[7] = 0x80U;
    WriteRegister(0x8EU, 0U); Begin(); WriteByte(0xBEU, false);
    for (i = 0U; i < 8U; ++i) WriteByte(raw[i], false);
    End();
    return DS1302_ReadTime(&check) && check.year == time->year && check.month == time->month &&
           check.day == time->day && check.hour == time->hour &&
           check.minute == time->minute && check.second == time->second;
}
bool DS1302_NextBootCounter(uint32_t *counter)
{
    uint8_t raw[12], verify[12];
    uint32_t old = 0U, inverse = 0U;
    unsigned i;
    if (counter == NULL) return false;
    Begin(); WriteByte(0xFFU, true);
    for (i = 0U; i < 12U; ++i) raw[i] = ReadByte();
    End();
    for (i = 0U; i < 4U; ++i)
    {
        old |= (uint32_t)raw[i + 2U] << (8U * i);
        inverse |= (uint32_t)raw[i + 6U] << (8U * i);
    }
    if (raw[0] != 0x4DU || raw[1] != 1U || raw[10] != 0xA5U || raw[11] != 0x5AU ||
        old != ~inverse) old = 0U;
    if (old == UINT32_MAX) return false;
    ++old; raw[0] = 0x4DU; raw[1] = 1U; raw[10] = 0xA5U; raw[11] = 0x5AU;
    for (i = 0U; i < 4U; ++i)
    {
        raw[i + 2U] = (uint8_t)(old >> (8U * i));
        raw[i + 6U] = (uint8_t)(~old >> (8U * i));
    }
    WriteRegister(0x8EU, 0U);
    WriteRegister(0xD4U, 0U); /* Invalidate marker first; commit bytes written last. */
    Begin(); WriteByte(0xFEU, false);
    for (i = 0U; i < 12U; ++i) WriteByte(raw[i], false);
    End(); WriteRegister(0x8EU, 0x80U);
    Begin(); WriteByte(0xFFU, true);
    for (i = 0U; i < 12U; ++i) verify[i] = ReadByte();
    End();
    if (memcmp(raw, verify, sizeof(raw)) != 0) return false;
    *counter = old; return true;
}
