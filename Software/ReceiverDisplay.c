#include "ReceiverDisplay.h"

#include "OLED.h"
#include "MessageService.h"
#include "LdrReceiver.h"
#include "ESP8266.h"
#include "DS1302.h"
#include "buzzer.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ROWS 4U
#define COLS 16U
#define TEXT_PER_PAGE (2U * COLS)
#define MODEL_PERIOD_MS 100U
#define STATUS_PERIOD_MS 3000U
#define TEXT_PAGE_PERIOD_MS 4000U

/* Text, not a 1KB graphics framebuffer. Redraw only differing characters.
 * Formatting/snapshots and GPIO transfers all remain outside interrupts. */
static char wanted[ROWS][COLS], painted[ROWS][COLS];
static MessageService_DisplayState message;
static LdrReceiver_Status receiver;
static ESP8266_Stats network;
static DS1302_Time rtc;
static uint32_t model_ms, rtc_ms, status_ms, page_ms, shown_sequence, shown_dot_ms, shown_dash_t;
static uint8_t scan_cell, status_page, text_page;
static bool was_receiving, time_valid;

static void Put(unsigned row, const char *text)
{
    unsigned col;
    for (col = 0U; col < COLS && text[col] != '\0'; ++col)
        wanted[row][col] = text[col] >= ' ' && text[col] <= '~' ? text[col] : '?';
}

static const char *ShortStatus(const char *status)
{
    if (strcmp(status, "WAIT_MODULE") == 0 || status[0] == '\0') return "WAIT";
    if (strcmp(status, "BOOT") == 0) return "BOOT";
    if (strcmp(status, "UART_READY") == 0) return "UART READY";
    if (strcmp(status, "WIFI_CONNECTED") == 0) return "WIFI OK";
    if (strcmp(status, "WIFI_DISCONNECTED") == 0) return "WIFI OFF";
    if (strcmp(status, "VPS_CONFIRMED") == 0) return "VPS OK";
    if (strcmp(status, "WAIT_NTP_FOR_TLS") == 0) return "WAIT TIME";
    if (strcmp(status, "BUFFERED_NOT_UPLOADED") == 0) return "UPLOADING";
    if (strcmp(status, "BUSY_PENDING_UPLOAD") == 0) return "BUSY";
    if (strstr(status, "CONFIG_MISSING") != NULL) return "SET CONFIG";
    if (strncmp(status, "HTTPS_", 6U) == 0) return "HTTPS ERR";
    if (strcmp(status, "BAD_FRAME") == 0 || strcmp(status, "UART_LINE_OVERFLOW") == 0) return "FRAME ERR";
    return "SEE SERIAL";
}

static void MakeFrame(uint32_t now_ms)
{
    char line[40];
    size_t length, i, offset;
    unsigned pages;

    MessageService_GetDisplayState(&message);
    LdrReceiver_GetStatus(&receiver);
    ESP8266_GetStats(&network);
    if ((uint32_t)(now_ms - rtc_ms) >= 1000U)
    {
        rtc_ms = now_ms;
        time_valid = DS1302_ReadTime(&rtc);
    }
    if (was_receiving != message.receiving || shown_sequence != message.sequence)
    {
        was_receiving = message.receiving;
        shown_sequence = message.sequence;
        status_page = text_page = 0U;
        status_ms = page_ms = now_ms;
    }
    if (shown_dot_ms != receiver.dot_ms || shown_dash_t != receiver.dash_t)
    {
        shown_dot_ms = receiver.dot_ms;
        shown_dash_t = receiver.dash_t;
        status_page = 0U;
        status_ms = now_ms;
    }
    if ((uint32_t)(now_ms - status_ms) >= STATUS_PERIOD_MS)
    {
        status_ms = now_ms;
        status_page = (uint8_t)((status_page + 1U) % 3U);
    }
    length = strlen(message.text);
    pages = length ? (unsigned)((length + TEXT_PER_PAGE - 1U) / TEXT_PER_PAGE) : 1U;
    if (message.receiving)
    {
        /* While receiving, follow the newest 32-character page. On message
         * completion, restart at page 1 and rotate through the full text. */
        text_page = (uint8_t)(pages - 1U);
        page_ms = now_ms;
    }
    else if ((uint32_t)(now_ms - page_ms) >= TEXT_PAGE_PERIOD_MS)
    {
        page_ms = now_ms;
        text_page = (uint8_t)((text_page + 1U) % pages);
    }
    if (text_page >= pages) text_page = 0U;

    memset(wanted, ' ', sizeof(wanted));
    if (status_page == 0U)
    {
        (void)snprintf(line, sizeof(line), "A%u T%lu D%lu", (unsigned)DEVICE_ADDRESS,
                       (unsigned long)receiver.dot_ms, (unsigned long)receiver.dash_t);
        Put(0U, line);
        if (message.receiving)
            (void)snprintf(line, sizeof(line), "RX... P%u/%u", (unsigned)text_page + 1U, pages);
        else if (message.sequence != 0U)
            (void)snprintf(line, sizeof(line), "#%lu P%u/%u", (unsigned long)message.sequence, (unsigned)text_page + 1U, pages);
        else
            (void)snprintf(line, sizeof(line), "READY TO RECEIVE");
        Put(1U, line);
    }
    else if (status_page == 1U)
    {
        if (time_valid)
            (void)snprintf(line, sizeof(line), "TIME %02u:%02u:%02u", (unsigned)rtc.hour, (unsigned)rtc.minute, (unsigned)rtc.second);
        else
            (void)snprintf(line, sizeof(line), "TIME --:--:--");
        Put(0U, line);
        if (receiver.have_sample)
            (void)snprintf(line, sizeof(line), "ADC:%04u L:%u", (unsigned)receiver.raw, (unsigned)receiver.light);
        else
            (void)snprintf(line, sizeof(line), "ADC:---- L:-");
        Put(1U, line);
    }
    else
    {
        (void)snprintf(line, sizeof(line), "ESP:%s", ShortStatus(ESP8266_GetLastStatus()));
        Put(0U, line);
        (void)snprintf(line, sizeof(line), "ACK%lu Q%u", (unsigned long)network.acknowledged, (unsigned)network.queued);
        Put(1U, line);
    }

    if (length != 0U)
    {
        offset = (size_t)text_page * TEXT_PER_PAGE;
        for (i = 0U; i < TEXT_PER_PAGE && offset + i < length; ++i)
        {
            char ch = message.text[offset + i];
            wanted[2U + i / COLS][i % COLS] = ch >= ' ' && ch <= '~' ? ch : '?';
        }
    }
    else
    {
        Put(2U, "Waiting for text");
    }

    /* Clear the full row before replacing a longer status, preventing stale
     * suffixes (e.g. an old alarm or message) on a shorter new string. */
    if (message.flags & (IOT_FLAG_DECODE_INVALID | IOT_FLAG_TRUNCATED))
    {
        memset(wanted[0], ' ', COLS);
        Put(0U, message.flags & IOT_FLAG_TRUNCATED ? "TEXT TRUNCATED" : "RX HAS ERRORS");
    }
    if (receiver.failed || !receiver.calibrated || !receiver.thresholds_valid || !receiver.synchronized)
    {
        memset(wanted[0], ' ', COLS);
        memset(wanted[1], ' ', COLS);
        if (receiver.failed) { Put(0U, "ADC ERROR"); Put(1U, "RESET RECEIVER"); }
        else if (!receiver.calibrated) { Put(0U, "CALIBRATING..."); Put(1U, "Keep torch OFF"); }
        else if (!receiver.thresholds_valid) { Put(0U, "CAL ERROR"); Put(1U, "Torch OFF+reset"); }
        else { Put(0U, "WAIT DARK SYNC"); Put(1U, "Keep torch OFF"); }
    }
    if (Buzzer_IsActive())
    {
        memset(wanted[0], ' ', COLS);
        Put(0U, "!ALARM SOS/HELP!");
    }
}

void ReceiverDisplay_Init(uint32_t now_ms)
{
    OLED_Init(); /* Includes initial clear. Must precede LdrAdc_Start(). */
    memset(painted, ' ', sizeof(painted));
    memset(wanted, ' ', sizeof(wanted));
    scan_cell = status_page = text_page = 0U;
    shown_sequence = 0U;
    shown_dot_ms = 0U;
    shown_dash_t = 0U;
    was_receiving = time_valid = false;
    model_ms = now_ms - MODEL_PERIOD_MS;
    rtc_ms = now_ms - 1000U;
    status_ms = page_ms = now_ms;
}

void ReceiverDisplay_Process(uint32_t now_ms)
{
    unsigned checked;
    if ((uint32_t)(now_ms - model_ms) >= MODEL_PERIOD_MS)
    {
        model_ms = now_ms;
        MakeFrame(now_ms);
    }
    for (checked = 0U; checked < ROWS * COLS; ++checked)
    {
        unsigned row = scan_cell / COLS, col = scan_cell % COLS;
        scan_cell = (uint8_t)((scan_cell + 1U) % (ROWS * COLS));
        if (wanted[row][col] != painted[row][col])
        {
            OLED_ShowChar((uint8_t)(row + 1U), (uint8_t)(col + 1U), wanted[row][col]);
            painted[row][col] = wanted[row][col];
            break; /* Bounded transfer; return to ADC/button/buzzer/network. */
        }
    }
}
