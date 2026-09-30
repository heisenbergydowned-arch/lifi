#ifndef IOT_PROTOCOL_H
#define IOT_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define IOT_LINE_SIZE 256U
#define IOT_TEXT_SIZE 97U
#define IOT_FLAG_TIME_VALID 1U
#define IOT_FLAG_DECODE_INVALID 2U
#define IOT_FLAG_TRUNCATED 4U
#define IOT_FLAG_DISTRESS 8U
#define IOT_FLAG_BOOT_VALID 16U
typedef struct
{
    uint32_t address, boot, sequence, uptime_ms, dot_ms, flags;
    uint16_t wire_crc;
    char timestamp[26];
    char text[IOT_TEXT_SIZE];
} IotRecord;
uint16_t IotProtocol_Crc(const char *text, size_t length);
/* Seal appends *CRC16\n. Open verifies and removes the suffix in place. */
bool IotProtocol_Seal(char *line, size_t capacity);
bool IotProtocol_Open(char *line, uint16_t *crc);
bool IotProtocol_U32(const char *text, uint32_t *value);
bool IotProtocol_Encode(const IotRecord *record, char *line, size_t capacity);
bool IotProtocol_Decode(char *line, IotRecord *record);
#ifdef __cplusplus
}
#endif
#endif
