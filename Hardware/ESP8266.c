#include "ESP8266.h"
#include "MorseDecoder.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define RX_SIZE 512U
static IotRecord queue[ESP8266_QUEUE_CAPACITY];
static uint8_t queued;
static uint8_t rx_byte;
static volatile uint8_t rx_ring[RX_SIZE];
static volatile uint16_t rx_head, rx_tail;
static volatile bool rx_fault, tx_busy;
static char rx_line[IOT_LINE_SIZE], tx_line[IOT_LINE_SIZE];
static uint16_t rx_used;
static bool rx_discard, waiting_ack, hello_pending;
static uint32_t sent_ms, tx_started_ms, local_address, local_boot;
static ESP8266_Stats counters;
static ESP8266_TimeHandler time_handler;
static ESP8266_ConfigHandler config_handler;
static char last_status[40];

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    uint16_t next;
    if (uart != &huart3) return;
    next = (uint16_t)((rx_head + 1U) % RX_SIZE);
    if (next == rx_tail) rx_fault = true;
    else { rx_ring[rx_head] = rx_byte; rx_head = next; }
    if (HAL_UART_Receive_IT(&huart3, &rx_byte, 1U) != HAL_OK) rx_fault = true;
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart3) tx_busy = false;
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart3) rx_fault = true;
}
bool ESP8266_Init(uint32_t address, uint32_t boot)
{
    if (address == 0U || address > 65535U) return false;
    (void)HAL_UART_AbortReceive(&huart3);
    (void)HAL_UART_AbortTransmit(&huart3);
    queued = 0U; rx_head = rx_tail = 0U; rx_used = 0U;
    rx_fault = false; tx_busy = false; rx_discard = false; waiting_ack = false;
    hello_pending = true; time_handler = NULL; config_handler = NULL;
    local_address = address; local_boot = boot;
    memset(&counters, 0, sizeof(counters));
    strcpy(last_status, "WAIT_MODULE");
    if (HAL_UART_Receive_IT(&huart3, &rx_byte, 1U) != HAL_OK)
    {
        rx_fault = true; /* Process retries; optical reception can continue. */
        return false;
    }
    return true;
}
void ESP8266_SetTimeHandler(ESP8266_TimeHandler handler) { time_handler = handler; }
void ESP8266_SetConfigHandler(ESP8266_ConfigHandler handler) { config_handler = handler; }
const char *ESP8266_GetLastStatus(void) { return last_status; }
void ESP8266_GetStats(ESP8266_Stats *stats)
{
    if (stats != NULL) { *stats = counters; stats->queued = queued; }
}
bool ESP8266_Queue(const IotRecord *record)
{
    unsigned i;
    /* Use rx_line only in Process; validate here with a dedicated scratch
     * buffer so an in-flight interrupt TX buffer is never overwritten. */
    static char check_line[IOT_LINE_SIZE];
    if (record == NULL || !IotProtocol_Encode(record, check_line, sizeof(check_line))) return false;
    if (queued == ESP8266_QUEUE_CAPACITY)
    {
        for (i = 1U; i < queued; ++i)
            if (!(queue[i].flags & IOT_FLAG_DISTRESS)) break;
        if (!(record->flags & IOT_FLAG_DISTRESS) || i == queued)
        { ++counters.dropped; return false; }
        printf("[NET] queue full: evict ordinary id=%lu for distress.\r\n", (unsigned long)queue[i].sequence);
        memmove(&queue[i], &queue[i + 1U], (queued - i - 1U) * sizeof(queue[0]));
        --queued; ++counters.dropped;
    }
    queue[queued] = *record;
    /* CRC of body, matching the gateway ACK's decimal CRC field. */
    queue[queued].wire_crc = IotProtocol_Crc(check_line, strlen(check_line) - 6U);
    ++queued; return true;
}
static unsigned Split(char *text, char **fields, unsigned capacity)
{
    unsigned n = 1U;
    fields[0] = text;
    while (*text)
    {
        if (*text == '|')
        {
            if (n >= capacity) return 0U;
            *text = '\0'; fields[n++] = text + 1;
        }
        ++text;
    }
    return n;
}
static void HandleLine(void)
{
    char *fields[5];
    unsigned count;
    uint32_t address, boot, sequence, crc;
    if (!IotProtocol_Open(rx_line, NULL)) { ++counters.receive_errors; return; }
    count = Split(rx_line, fields, 5U);
    if (count == 5U && strcmp(fields[0], "A1") == 0)
    {
        if (!IotProtocol_U32(fields[1], &address) || !IotProtocol_U32(fields[2], &boot) ||
            !IotProtocol_U32(fields[3], &sequence) || !IotProtocol_U32(fields[4], &crc)) return;
        if (queued && waiting_ack && address == queue[0].address && boot == queue[0].boot &&
            sequence == queue[0].sequence && crc == queue[0].wire_crc)
        {
            printf("[NET] VPS confirmed addr=%lu id=%lu.\r\n", (unsigned long)address, (unsigned long)sequence);
            --queued; memmove(&queue[0], &queue[1], queued * sizeof(queue[0]));
            waiting_ack = false; ++counters.acknowledged;
        }
    }
    else if (count == 2U && strcmp(fields[0], "T1") == 0)
    {
        if (time_handler != NULL) time_handler(fields[1]);
    }
    else if (count == 2U && strcmp(fields[0], "S1") == 0)
    {
        strncpy(last_status, fields[1], sizeof(last_status) - 1U);
        last_status[sizeof(last_status) - 1U] = '\0';
        printf("[ESP] %s\r\n", fields[1]);
        if (strcmp(fields[1], "BOOT") == 0) { hello_pending = true; waiting_ack = false; }
    }
    else if (count == 5U && strcmp(fields[0], "C1") == 0)
    {
        /* C1|t_ms|dash_t|word_gap_t|msg_gap_t (ProtocolConfig four-tuple).
         * Any parse or range failure drops the whole frame and keeps the
         * old configuration; the handler applies only validated values. */
        uint32_t t_ms, dash_t, word_gap_t, msg_gap_t;
        if (!IotProtocol_U32(fields[1], &t_ms) || !IotProtocol_U32(fields[2], &dash_t) ||
            !IotProtocol_U32(fields[3], &word_gap_t) || !IotProtocol_U32(fields[4], &msg_gap_t)) return;
        if ((t_ms < MORSE_DECODER_T_MS_MIN) || (t_ms > MORSE_DECODER_T_MS_MAX) ||
            (dash_t < MORSE_DECODER_DASH_T_MIN) || (dash_t > MORSE_DECODER_DASH_T_MAX) ||
            (word_gap_t < MORSE_DECODER_WORD_GAP_T_MIN) || (word_gap_t > MORSE_DECODER_WORD_GAP_T_MAX) ||
            (msg_gap_t < MORSE_DECODER_MSG_GAP_T_MIN) || (msg_gap_t > MORSE_DECODER_MSG_GAP_T_MAX)) return;
        if (config_handler != NULL) config_handler(t_ms, dash_t, word_gap_t, msg_gap_t);
    }
}
static bool Transmit(uint32_t now_ms)
{
    tx_busy = true;
    tx_started_ms = now_ms;
    if (HAL_UART_Transmit_IT(&huart3, (uint8_t *)tx_line, (uint16_t)strlen(tx_line)) == HAL_OK) return true;
    tx_busy = false; return false;
}
void ESP8266_Process(uint32_t now_ms)
{
    if (rx_fault)
    {
        (void)HAL_UART_AbortReceive(&huart3);
        rx_tail = rx_head; rx_used = 0U; rx_discard = true; rx_fault = false;
        ++counters.receive_errors;
        if (HAL_UART_Receive_IT(&huart3, &rx_byte, 1U) != HAL_OK) rx_fault = true;
    }
    while (rx_tail != rx_head)
    {
        char ch = (char)rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1U) % RX_SIZE);
        if (ch == '\n')
        {
            if (!rx_discard && rx_used) { rx_line[rx_used] = '\0'; HandleLine(); }
            rx_used = 0U; rx_discard = false;
        }
        else if (!rx_discard)
        {
            if (rx_used + 1U < sizeof(rx_line)) rx_line[rx_used++] = ch;
            else { rx_discard = true; ++counters.receive_errors; }
        }
    }
    if (tx_busy && (uint32_t)(now_ms - tx_started_ms) >= 500U)
    { (void)HAL_UART_AbortTransmit(&huart3); tx_busy = false; ++counters.receive_errors; }
    if (tx_busy) return;
    if (hello_pending)
    {
        (void)snprintf(tx_line, sizeof(tx_line), "H1|%lu|%lu", (unsigned long)local_address, (unsigned long)local_boot);
        if (IotProtocol_Seal(tx_line, sizeof(tx_line)) && Transmit(now_ms)) hello_pending = false;
        return;
    }
    if (!queued) return;
    if (waiting_ack && (uint32_t)(now_ms - sent_ms) < ESP8266_ACK_TIMEOUT_MS) return;
    if (IotProtocol_Encode(&queue[0], tx_line, sizeof(tx_line)) && Transmit(now_ms))
    {
        if (waiting_ack)
        {
            ++counters.retries;
            printf("[NET] no VPS ACK, retry id=%lu; buffered=%u.\r\n", (unsigned long)queue[0].sequence, (unsigned)queued);
        }
        waiting_ack = true; sent_ms = now_ms;
    }
}
