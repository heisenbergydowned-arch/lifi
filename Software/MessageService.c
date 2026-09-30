#include "MessageService.h"
#include "MorseMessage.h"
#include "LdrReceiver.h"
#include "DS1302.h"
#include "ESP8266.h"
#include "buzzer.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static MorseMessage message;
static IotRecord record;
static uint32_t sequence, boot_counter;
static bool boot_valid, rtc_available;

static void ReceiveEvent(const MorseDecoderEvent *event, uint32_t sample_ms)
{
    DS1302_Time time;
    bool alarm;
    (void)sample_ms;
    if (event == NULL)
    {
        if (message.length) printf("[MSG] unfinished text discarded after stream reset.\r\n");
        MorseMessage_Init(&message); return;
    }
    if (!MorseMessage_Feed(&message, event)) return;
    alarm = MorseMessage_IsDistress(&message);
    if (alarm)
    {
        Buzzer_StartAlarm(HAL_GetTick());
        printf("[ALARM] SOS/HELP: buzzer ON for 60s from this message.\r\n");
    }
    if (sequence == UINT32_MAX)
    {
        printf("[MSG] sequence exhausted; reporting stopped, reset required.\r\n");
        MorseMessage_Init(&message); return;
    }
    memset(&record, 0, sizeof(record));
    record.address = DEVICE_ADDRESS; record.boot = boot_counter; record.sequence = ++sequence;
    record.uptime_ms = HAL_GetTick(); record.dot_ms = LdrReceiver_GetDotTimeMs();
    if (boot_valid) record.flags |= IOT_FLAG_BOOT_VALID;
    if (message.invalid) record.flags |= IOT_FLAG_DECODE_INVALID;
    if (message.truncated) record.flags |= IOT_FLAG_TRUNCATED;
    if (alarm) record.flags |= IOT_FLAG_DISTRESS;
    strcpy(record.timestamp, "-");
    if (rtc_available && DS1302_ReadTime(&time) && DS1302_FormatTime(&time, record.timestamp, sizeof(record.timestamp)))
        record.flags |= IOT_FLAG_TIME_VALID;
    strcpy(record.text, message.text);
    printf("[MSG] addr=%lu boot=%lu id=%lu time=%s T=%lu flags=%lu text=\"%s\"\r\n",
           (unsigned long)record.address, (unsigned long)record.boot, (unsigned long)record.sequence,
           record.timestamp, (unsigned long)record.dot_ms, (unsigned long)record.flags, record.text);
    if (!ESP8266_Queue(&record))
        printf("[NET] NOT queued id=%lu (invalid/full); local alarm remains independent.\r\n", (unsigned long)sequence);
    MorseMessage_Init(&message);
}
void MessageService_Init(bool rtc_present)
{
    DS1302_Time time;
    sequence = 0U; boot_counter = 0U; rtc_available = rtc_present;
    memset(&record, 0, sizeof(record));
    boot_valid = rtc_present && DS1302_NextBootCounter(&boot_counter);
    MorseMessage_Init(&message);
    LdrReceiver_SetEventHandler(ReceiveEvent);
    printf("[RTC] present=%u time_valid=%u boot=%lu boot_valid=%u; timezone=+08:00.\r\n",
           (unsigned)rtc_present, (unsigned)(rtc_present && DS1302_ReadTime(&time)),
           (unsigned long)boot_counter, (unsigned)boot_valid);
    printf("[MSG] addr=%u; frame end=10T darkness; max=96 chars; single-shot transmitter.\r\n", (unsigned)DEVICE_ADDRESS);
}
uint32_t MessageService_GetBootCounter(void) { return boot_counter; }
void MessageService_GetDisplayState(MessageService_DisplayState *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    if (message.length != 0U)
    {
        state->receiving = true;
        strcpy(state->text, message.text);
        if (message.invalid) state->flags |= IOT_FLAG_DECODE_INVALID;
        if (message.truncated) state->flags |= IOT_FLAG_TRUNCATED;
    }
    else if (sequence != 0U)
    {
        strcpy(state->text, record.text);
        state->sequence = record.sequence;
        state->flags = record.flags;
    }
}
void MessageService_SetNetworkTime(const char *iso_time)
{
    DS1302_Time time;
    if (!DS1302_ParseTime(iso_time, &time)) { printf("[RTC] invalid network date rejected.\r\n"); return; }
    if (!rtc_available) rtc_available = DS1302_Init();
    if (rtc_available && DS1302_SetTime(&time)) printf("[RTC] synchronized: %s\r\n", iso_time);
    else printf("[RTC] synchronization failed; check DS1302 wiring/battery.\r\n");
}
