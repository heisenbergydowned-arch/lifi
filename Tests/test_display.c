/* Production display policy and incremental repaint. The OLED/RTC and source
 * snapshots are fake here; this does not measure GPIO speed or optical timing. */
#include "ReceiverDisplay.h"
#include "OLED.h"
#include "MessageService.h"
#include "LdrReceiver.h"
#include "ESP8266.h"
#include "DS1302.h"
#include "buzzer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, writes, init_calls, rtc_reads;
static char screen[4][17];
static MessageService_DisplayState msg;
static LdrReceiver_Status ldr;
static ESP8266_Stats net;
static bool alarm, rtc_ok;
static const char *esp_status;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL display:%d: %s\n", __LINE__, #x); exit(1); } } while (0)

void MessageService_GetDisplayState(MessageService_DisplayState *out) { *out = msg; }
void LdrReceiver_GetStatus(LdrReceiver_Status *out) { *out = ldr; }
void ESP8266_GetStats(ESP8266_Stats *out) { *out = net; }
const char *ESP8266_GetLastStatus(void) { return esp_status; }
bool Buzzer_IsActive(void) { return alarm; }
bool DS1302_ReadTime(DS1302_Time *out)
{
    ++rtc_reads;
    out->year = 2026U; out->month = 9U; out->day = 10U;
    out->hour = 22U; out->minute = 13U; out->second = 56U;
    return rtc_ok;
}
void OLED_Init(void)
{
    unsigned row;
    ++init_calls;
    for (row = 0U; row < 4U; ++row) { memset(screen[row], ' ', 16U); screen[row][16] = '\0'; }
}
void OLED_ShowChar(uint8_t row, uint8_t col, char ch)
{
    CHECK(row >= 1U && row <= 4U && col >= 1U && col <= 16U);
    CHECK(ch >= ' ' && ch <= '~');
    screen[row - 1U][col - 1U] = ch; ++writes;
}
static void Render(uint32_t now)
{
    unsigned i, before_rtc = rtc_reads;
    for (i = 0U; i < 64U; ++i)
    {
        unsigned before = writes;
        ReceiverDisplay_Process(now);
        CHECK(writes - before <= 1U);
    }
    CHECK(rtc_reads - before_rtc <= 1U);
}
static void Row(unsigned row, const char *expected)
{
    char padded[17]; size_t len = strlen(expected);
    CHECK(len <= 16U);
    memset(padded, ' ', 16U); padded[16] = '\0'; memcpy(padded, expected, len);
    if (strcmp(screen[row], padded) != 0)
        fprintf(stderr, "row=%u expected=[%s] actual=[%s]\n", row, padded, screen[row]);
    CHECK(strcmp(screen[row], padded) == 0);
}
static void Reset(uint32_t now)
{
    memset(&msg, 0, sizeof(msg)); memset(&ldr, 0, sizeof(ldr)); memset(&net, 0, sizeof(net));
    alarm = false; rtc_ok = true; esp_status = "WAIT_MODULE";
    ldr.dot_ms = 60U; ldr.dash_t = 3U; ReceiverDisplay_Init(now);
}
static void Ready(void)
{
    ldr.have_sample = ldr.calibrated = ldr.thresholds_valid = ldr.synchronized = true;
    ldr.raw = 530U; ldr.normalized = 3565U;
}
int main(void)
{
    unsigned before, i;
    Reset(0U); Render(0U); Row(0U, "CALIBRATING..."); Row(1U, "Keep torch OFF");
    CHECK(init_calls == 1U && rtc_reads == 1U);
    Ready(); Render(100U); Row(0U, "A1 T60 D3"); Row(1U, "READY TO RECEIVE");
    Row(2U, "Waiting for text"); Row(3U, "");
    before = writes; Render(150U); CHECK(writes == before && rtc_reads == 1U);

    msg.receiving = true; strcpy(msg.text, "HEL"); Render(200U);
    Row(1U, "RX... P1/1"); Row(2U, "HEL"); Row(3U, "");
    memset(msg.text, 'A', 32U); strcpy(msg.text + 32U, "B"); Render(300U);
    Row(1U, "RX... P2/2"); Row(2U, "B"); Row(3U, "");
    for (i = 0U; i < 96U; ++i) msg.text[i] = (char)('A' + i / 32U);
    msg.text[96] = '\0'; msg.receiving = false; msg.sequence = 9U;
    Render(400U); Row(1U, "#9 P1/3"); Row(2U, "AAAAAAAAAAAAAAAA"); Row(3U, "AAAAAAAAAAAAAAAA");
    Render(4400U); Row(0U, "TIME 22:13:56"); Row(1U, "ADC:0530 L:0");
    Row(2U, "BBBBBBBBBBBBBBBB"); Row(3U, "BBBBBBBBBBBBBBBB");
    Render(8400U); Row(0U, "ESP:WAIT"); Row(1U, "ACK0 Q0");
    Row(2U, "CCCCCCCCCCCCCCCC"); Row(3U, "CCCCCCCCCCCCCCCC");
    Render(12400U); Row(1U, "#9 P1/3"); Row(2U, "AAAAAAAAAAAAAAAA");
    strcpy(msg.text, "E"); ++msg.sequence; Render(12500U);
    Row(2U, "E"); Row(3U, ""); /* Erase all old 96-character page remnants. */
    strcpy(msg.text, "SOS"); ++msg.sequence; alarm = true; Render(12600U);
    Row(0U, "!ALARM SOS/HELP!"); Row(2U, "SOS");
    strcpy(msg.text, "HELLO"); ++msg.sequence; Render(12700U);
    Row(0U, "!ALARM SOS/HELP!"); Row(2U, "HELLO");
    alarm = false; Render(12800U); Row(0U, "A1 T60 D3");
    msg.flags = IOT_FLAG_DECODE_INVALID; Render(12900U); Row(0U, "RX HAS ERRORS");
    msg.flags = IOT_FLAG_TRUNCATED; Render(13000U); Row(0U, "TEXT TRUNCATED");
    ldr.failed = true; Render(13100U); Row(0U, "ADC ERROR"); Row(1U, "RESET RECEIVER");

    Reset(0U); Ready(); Render(0U);
    msg.sequence = UINT32_MAX; strcpy(msg.text, "A\001?"); Render(100U);
    Row(1U, "#4294967295 P1/1"); Row(2U, "A??");
    rtc_ok = false; ldr.raw = 0U; Render(3100U);
    Row(0U, "TIME --:--:--"); Row(1U, "ADC:0000 L:0");
    net.acknowledged = UINT32_MAX; net.queued = 8U; esp_status = "WIFI_DISCONNECTED";
    Render(6100U); Row(0U, "ESP:WIFI OFF"); Row(1U, "ACK4294967295 Q8");
    esp_status = "HTTPS_NOT_CONFIRMED_-1"; Render(6200U); Row(0U, "ESP:HTTPS ERR");
    esp_status = "VPS_CONFIRMED"; Render(6300U); Row(0U, "ESP:VPS OK");
    ldr.dot_ms = 120U; Render(6400U); Row(0U, "A1 T120 D3");
    ldr.synchronized = false; Render(6500U); Row(0U, "WAIT DARK SYNC");
    ldr.synchronized = true; ldr.thresholds_valid = false; Render(6600U); Row(0U, "CAL ERROR");

    Reset(UINT32_MAX - 900U); Ready(); Render(UINT32_MAX - 900U);
    Render(2199U); Row(0U, "TIME 22:13:56");
    before = rtc_reads; Render(2299U); Render(2399U); CHECK(rtc_reads == before);
    Row(2U, "Waiting for text"); CHECK(init_calls == 3U);
    fprintf(stderr, "PASS: display, %u assertions: one-char slices, live text, 96-char pages, erase, alarms, invalid RTC, ADC raw, network, T, tick wrap\n", checks);
    return 0;
}
