#include "IotProtocol.h"
#include "DS1302.h"
#include <stdio.h>
#include <string.h>
uint16_t IotProtocol_Crc(const char *text, size_t length)
{
    uint16_t crc = 0xFFFFU;
    size_t i;
    unsigned bit;
    for (i = 0U; i < length; ++i)
    {
        crc ^= (uint16_t)((uint16_t)(uint8_t)text[i] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
            crc = (uint16_t)((crc & 0x8000U) ? ((uint32_t)crc << 1U) ^ 0x1021U : (uint32_t)crc << 1U);
    }
    return crc;
}
bool IotProtocol_Seal(char *line, size_t capacity)
{
    size_t length = 0U, i;
    if (line == NULL) return false;
    while (length < capacity && line[length]) ++length;
    if (length == 0U || length + 7U > capacity) return false;
    for (i = 0U; i < length; ++i)
        if ((unsigned char)line[i] < 32U || (unsigned char)line[i] > 126U || line[i] == '*') return false;
    return snprintf(line + length, capacity - length, "*%04X\n", (unsigned)IotProtocol_Crc(line, length)) == 6;
}
bool IotProtocol_Open(char *line, uint16_t *crc)
{
    size_t length, star, i;
    uint16_t expected = 0U;
    if (line == NULL) return false;
    length = strlen(line);
    if (length && line[length - 1U] == '\n') --length;
    if (length && line[length - 1U] == '\r') --length;
    if (length < 6U) return false;
    star = length - 5U;
    if (line[star] != '*') return false;
    for (i = 0U; i < star; ++i)
        if ((unsigned char)line[i] < 32U || (unsigned char)line[i] > 126U || line[i] == '*') return false;
    for (i = star + 1U; i < length; ++i)
    {
        unsigned digit;
        if (line[i] >= '0' && line[i] <= '9') digit = (unsigned)(line[i] - '0');
        else if (line[i] >= 'A' && line[i] <= 'F') digit = (unsigned)(line[i] - 'A') + 10U;
        else return false;
        expected = (uint16_t)((expected << 4U) | digit);
    }
    if (expected != IotProtocol_Crc(line, star)) return false;
    line[star] = '\0';
    if (crc != NULL) *crc = expected;
    return true;
}
bool IotProtocol_U32(const char *text, uint32_t *value)
{
    uint32_t result = 0U;
    unsigned digit;
    if (text == NULL || value == NULL || !*text) return false;
    while (*text)
    {
        if (*text < '0' || *text > '9') return false;
        digit = (unsigned)(*text++ - '0');
        if (result > (UINT32_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    *value = result; return true;
}
static bool Valid(const IotRecord *record)
{
    DS1302_Time time;
    size_t i;
    if (memchr(record->timestamp, '\0', sizeof(record->timestamp)) == NULL) return false;
    if (record->address == 0U || record->address > 65535U || record->sequence == 0U ||
        record->dot_ms < 60U || record->dot_ms > 2000U || record->flags > 31U) return false;
    if ((record->flags & IOT_FLAG_BOOT_VALID) && record->boot == 0U) return false;
    if ((record->flags & IOT_FLAG_DISTRESS) && (record->flags & (IOT_FLAG_DECODE_INVALID | IOT_FLAG_TRUNCATED))) return false;
    if (record->flags & IOT_FLAG_TIME_VALID)
    {
        if (!DS1302_ParseTime(record->timestamp, &time)) return false;
    }
    else if (strcmp(record->timestamp, "-") != 0) return false;
    for (i = 0U; i < IOT_TEXT_SIZE && record->text[i]; ++i)
    {
        char ch = record->text[i];
        if (!(ch >= 'A' && ch <= 'Z') && !(ch >= '0' && ch <= '9') && strchr(" +=.,?/-", ch) == NULL) return false;
    }
    return i > 0U && i < IOT_TEXT_SIZE;
}
bool IotProtocol_Encode(const IotRecord *record, char *line, size_t capacity)
{
    int count;
    if (record == NULL || line == NULL || !Valid(record)) return false;
    count = snprintf(line, capacity, "M1|%lu|%lu|%lu|%lu|%lu|%lu|%s|%s",
                     (unsigned long)record->address, (unsigned long)record->boot,
                     (unsigned long)record->sequence, (unsigned long)record->uptime_ms,
                     (unsigned long)record->dot_ms, (unsigned long)record->flags, record->timestamp, record->text);
    return count > 0 && (size_t)count < capacity && IotProtocol_Seal(line, capacity);
}
bool IotProtocol_Decode(char *line, IotRecord *record)
{
    char *fields[9], *at;
    unsigned n = 1U;
    uint16_t crc;
    if (record == NULL || !IotProtocol_Open(line, &crc)) return false;
    fields[0] = line;
    for (at = line; *at; ++at)
    {
        if (*at == '|')
        {
            if (n >= 9U) return false;
            *at = '\0'; fields[n++] = at + 1;
        }
    }
    if (n != 9U || strcmp(fields[0], "M1") != 0) return false;
    memset(record, 0, sizeof(*record));
    if (!IotProtocol_U32(fields[1], &record->address) || !IotProtocol_U32(fields[2], &record->boot) ||
        !IotProtocol_U32(fields[3], &record->sequence) || !IotProtocol_U32(fields[4], &record->uptime_ms) ||
        !IotProtocol_U32(fields[5], &record->dot_ms) || !IotProtocol_U32(fields[6], &record->flags) ||
        strlen(fields[7]) >= sizeof(record->timestamp) || strlen(fields[8]) >= sizeof(record->text)) return false;
    strcpy(record->timestamp, fields[7]); strcpy(record->text, fields[8]); record->wire_crc = crc;
    return Valid(record);
}
