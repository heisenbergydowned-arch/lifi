#ifndef ESP8266_H
#define ESP8266_H
#include "IotProtocol.h"
#define ESP8266_QUEUE_CAPACITY 8U
#define ESP8266_ACK_TIMEOUT_MS 60000U
typedef void (*ESP8266_TimeHandler)(const char *iso_time);
/* C1 downlink: ProtocolConfig four-tuple. HandleLine parses and validates
 * all four fields against the MorseDecoder ranges before invoking the
 * handler; any parse/range failure drops the whole frame silently. */
typedef void (*ESP8266_ConfigHandler)(uint32_t t_ms, uint32_t dash_t,
                                     uint32_t word_gap_t, uint32_t msg_gap_t);
typedef struct
{
    uint32_t acknowledged, retries, dropped, receive_errors;
    uint8_t queued;
} ESP8266_Stats;
/* Custom ESP-01S gateway firmware (ESP8266_Gateway/), NOT stock AT firmware.
 * USART3 115200 8N1. Main context only. Queue is volatile, not power-loss safe.
 * ACK means the VPS endpoint confirmed this exact message ID. */
bool ESP8266_Init(uint32_t address, uint32_t boot);
void ESP8266_SetTimeHandler(ESP8266_TimeHandler handler);
void ESP8266_SetConfigHandler(ESP8266_ConfigHandler handler);
bool ESP8266_Queue(const IotRecord *record);
void ESP8266_Process(uint32_t now_ms);
void ESP8266_GetStats(ESP8266_Stats *stats);
/* Last CRC-checked S1 status, or WAIT_MODULE before any status arrives.
 * Borrowed read-only string, main context only. Not a live connectivity probe. */
const char *ESP8266_GetLastStatus(void);
#endif
