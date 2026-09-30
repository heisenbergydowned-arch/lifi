/* Runs the production GPIO drivers, decoder/receiver, message coordinator
 * and UART transport against an independent pin-level DS1302/UART model.
 * Logical test evidence only; does not measure a real LDR, RTC or Wi-Fi. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "usart.h"
#include "button.h"
#include "buzzer.h"
#include "DS1302.h"
#include "MorseMessage.h"
#include "ESP8266.h"
#include "MessageService.h"
#include "LdrReceiver.h"
#include "LdrReceiver_Config.h"
#include "LdrAdc.h"

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
GPIO_TypeDef port_a = {1U}, port_b = {2U};
UART_HandleTypeDef huart3 = {3U}, other_uart = {1U};
static uint32_t tick;
static uint8_t keys;
static bool buzzer_pin;
static uint8_t rtc[9], ram[31];
static bool ds_present = true, ce, sclk, master_release = true, slave_bit;
static unsigned command_bits, data_bits, read_bit;
static uint8_t command, write_value;
static unsigned contention;
static uint8_t *uart_rx;
static const uint8_t *uart_tx;
static uint16_t uart_tx_len;
static bool uart_active, fail_rx, fail_tx;
static char sent[IOT_LINE_SIZE];
static LdrAdcSample adc_queue[64];
static unsigned adc_read, adc_write;
static uint32_t sample_clock;
static LdrAdcStats adc_stats;

uint32_t HAL_GetTick(void) { return tick; }
static unsigned DsAddress(unsigned offset)
{
    unsigned base = (command >> 1U) & 31U;
    return base == 31U ? offset : base;
}
static uint8_t DsRead(unsigned offset)
{
    unsigned index = DsAddress(offset);
    return command & 0x40U ? ram[index % 31U] : rtc[index % 9U];
}
static void DsWrite(unsigned offset, uint8_t value)
{
    unsigned index = DsAddress(offset);
    if (!(command & 0x40U) && index == 7U) { rtc[7] = value & 0x80U; return; }
    if (rtc[7] & 0x80U) return;
    if (command & 0x40U) ram[index % 31U] = value;
    else rtc[index % 9U] = value;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState value)
{
    bool high = value == GPIO_PIN_SET;
    CHECK(port == &port_b);
    if (pin == BUZZER_Pin) { buzzer_pin = high; return; }
    if (pin == RTC_IO_Pin) { master_release = high; return; }
    if (pin == RTC_CE_Pin)
    {
        if (high && !ce) { command_bits = data_bits = read_bit = 0U; command = write_value = 0U; }
        ce = high; return;
    }
    CHECK(pin == RTC_SCLK_Pin);
    if (ds_present && ce)
    {
        if (high && !sclk)
        {
            if (command_bits < 8U)
            {
                if (master_release) command |= (uint8_t)(1U << command_bits);
                ++command_bits;
            }
            else if (!(command & 1U))
            {
                if (master_release) write_value |= (uint8_t)(1U << (data_bits % 8U));
                ++data_bits;
                if (data_bits % 8U == 0U) { DsWrite(data_bits / 8U - 1U, write_value); write_value = 0U; }
            }
        }
        else if (!high && sclk && command_bits == 8U && (command & 1U))
        {
            slave_bit = (DsRead(read_bit / 8U) & (1U << (read_bit % 8U))) != 0U;
            ++read_bit;
            if (!master_release && slave_bit) ++contention;
        }
    }
    sclk = high;
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == &port_a)
    {
        unsigned mask = pin == Low_Mode_Pin ? 1U : pin == Medium_Mode_Pin ? 2U : 4U;
        return keys & mask ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }
    CHECK(port == &port_b && pin == RTC_IO_Pin);
    if (!ds_present) return GPIO_PIN_SET;
    return slave_bit && master_release ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length)
{
    CHECK(uart == &huart3 && length == 1U);
    if (fail_rx) { fail_rx = false; return HAL_ERROR; }
    uart_rx = data; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length)
{
    CHECK(uart == &huart3 && length < sizeof(sent));
    if (fail_tx) { fail_tx = false; return HAL_BUSY; }
    CHECK(!uart_active);
    uart_tx = data; uart_tx_len = length; uart_active = true;
    memcpy(sent, data, length); sent[length] = '\0'; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart) { CHECK(uart == &huart3); uart_rx = NULL; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart) { CHECK(uart == &huart3); uart_active = false; return HAL_OK; }
static void FinishTx(void)
{
    CHECK(uart_active && memcmp(uart_tx, sent, uart_tx_len) == 0);
    uart_active = false; HAL_UART_TxCpltCallback(&huart3);
}
static void ReceiveText(const char *text)
{
    while (*text) { CHECK(uart_rx != NULL); *uart_rx = (uint8_t)*text++; HAL_UART_RxCpltCallback(&huart3); }
}
static void ReceiveBody(const char *body)
{
    char line[IOT_LINE_SIZE]; strcpy(line, body); CHECK(IotProtocol_Seal(line, sizeof(line))); ReceiveText(line);
}
bool LdrAdc_Read(LdrAdcSample *sample)
{
    if (adc_read == adc_write) return false;
    *sample = adc_queue[adc_read++];
    if (adc_read == adc_write) adc_read = adc_write = 0U;
    return true;
}
void LdrAdc_GetStats(LdrAdcStats *stats) { *stats = adc_stats; }
LdrAdcStatus LdrAdc_Stop(void) { return LDR_ADC_OK; }

static void TestButtonsAndBuzzer(void)
{
    uint32_t selected = 999U;
    keys = 0U; Button_Init(0U); keys = 1U;
    CHECK(!Button_Poll(1U, &selected)); keys = 0U; CHECK(!Button_Poll(8U, &selected));
    keys = 1U; CHECK(!Button_Poll(11U, &selected)); CHECK(!Button_Poll(30U, &selected));
    CHECK(Button_Poll(31U, &selected) && selected == 240U); CHECK(!Button_Poll(9999U, &selected));
    keys = 0U; CHECK(!Button_Poll(10000U, &selected)); CHECK(!Button_Poll(10020U, &selected));
    keys = 2U; CHECK(!Button_Poll(10021U, &selected)); CHECK(Button_Poll(10041U, &selected) && selected == 120U);
    keys = 4U; Button_Init(UINT32_MAX - 30U); CHECK(!Button_Poll(100U, &selected));
    keys = 0U; CHECK(!Button_Poll(UINT32_MAX - 15U, &selected)); CHECK(!Button_Poll(5U, &selected));
    keys = 4U; CHECK(!Button_Poll(6U, &selected)); CHECK(Button_Poll(26U, &selected) && selected == 60U);
    keys = 0U; Button_Init(0U); keys = 3U; CHECK(!Button_Poll(1U, &selected)); CHECK(!Button_Poll(21U, &selected));
    keys = 1U; CHECK(!Button_Poll(22U, &selected)); CHECK(!Button_Poll(42U, &selected));
    Buzzer_Init(); CHECK(!Buzzer_IsActive() && !buzzer_pin);
    Buzzer_StartAlarm(100U); Buzzer_Process(60099U); CHECK(Buzzer_IsActive() && buzzer_pin);
    Buzzer_Process(60100U); CHECK(!Buzzer_IsActive() && !buzzer_pin);
    Buzzer_StartAlarm(UINT32_MAX - 100U); Buzzer_Process(59898U); CHECK(Buzzer_IsActive());
    Buzzer_Process(59899U); CHECK(!Buzzer_IsActive());
    Buzzer_StartAlarm(1U); Buzzer_StartAlarm(50000U); Buzzer_Process(60001U); CHECK(Buzzer_IsActive());
    Buzzer_Process(110000U); CHECK(!Buzzer_IsActive());
    fprintf(stderr, "PASS: keys/debounce/chords/wrap and 60s nonblocking buzzer\n");
}
static void TestRtc(void)
{
    DS1302_Time t = {2028U,2U,29U,23U,58U,37U}, read;
    char iso[26]; uint32_t boot;
    memset(rtc, 0, sizeof(rtc)); memset(ram, 0xFF, sizeof(ram)); rtc[0] = 0x80U;
    CHECK(DS1302_Init()); CHECK(!DS1302_ReadTime(&read)); CHECK(rtc[8] == 0U);
    CHECK(DS1302_SetTime(&t)); CHECK(DS1302_ReadTime(&read));
    CHECK(read.year == 2028U && read.month == 2U && read.day == 29U && read.second == 37U);
    CHECK(DS1302_FormatTime(&read, iso, sizeof(iso)) && strcmp(iso, "2028-02-29T23:58:37+08:00") == 0);
    CHECK(DS1302_ParseTime(iso, &read)); CHECK(!DS1302_ParseTime("2027-02-29T23:58:37+08:00", &read));
    CHECK(!DS1302_ParseTime("2028-02-29T24:00:00+08:00", &read)); CHECK(!DS1302_ParseTime("2028-02-29T23:58:37+00:00", &read));
    t.year = 2027U; CHECK(!DS1302_SetTime(&t)); t.year = 2028U;
    rtc[2] = 0x92U; CHECK(DS1302_ReadTime(&read) && read.hour == 0U);
    rtc[2] = 0xB2U; CHECK(DS1302_ReadTime(&read) && read.hour == 12U);
    rtc[2] = 0xA1U; CHECK(DS1302_ReadTime(&read) && read.hour == 13U);
    rtc[0] = 0x6AU; CHECK(!DS1302_ReadTime(&read)); CHECK(DS1302_SetTime(&t));
    CHECK(DS1302_NextBootCounter(&boot) && boot == 1U); CHECK(DS1302_NextBootCounter(&boot) && boot == 2U);
    CHECK(DS1302_Init()); CHECK(DS1302_NextBootCounter(&boot) && boot == 3U); CHECK(contention == 0U);
    ds_present = false; CHECK(!DS1302_Init()); CHECK(!DS1302_ReadTime(&read)); CHECK(!DS1302_NextBootCounter(&boot)); ds_present = true;
    fprintf(stderr, "PASS: GPIO-level DS1302 reads/writes, first bit, BCD/leap date, battery counter, absent device\n");
}
static IotRecord Example(void)
{
    IotRecord record;
    memset(&record, 0, sizeof(record)); record.address = 1U; record.boot = 3U; record.sequence = 1U;
    record.uptime_ms = 4000U; record.dot_ms = 60U; record.flags = IOT_FLAG_TIME_VALID | IOT_FLAG_BOOT_VALID;
    strcpy(record.timestamp, "2028-02-29T23:58:37+08:00"); strcpy(record.text, "HELLO 123"); return record;
}
static void TestProtocolAndMessage(void)
{
    IotRecord record = Example(), decoded;
    char wire[IOT_LINE_SIZE], scratch[IOT_LINE_SIZE]; size_t i;
    MorseMessage msg; MorseDecoderEvent event; const char *text;
    CHECK(IotProtocol_Crc("123456789", 9U) == 0x29B1U);
    CHECK(IotProtocol_Encode(&record, wire, sizeof(wire))); strcpy(scratch, wire);
    CHECK(IotProtocol_Decode(scratch, &decoded) && decoded.sequence == record.sequence && strcmp(decoded.text, record.text) == 0);
    for (i = 0U; i < strlen(wire) - 1U; ++i) { strcpy(scratch, wire); scratch[i] ^= 1; CHECK(!IotProtocol_Decode(scratch, &decoded)); }
    record.sequence = UINT32_MAX; record.uptime_ms = UINT32_MAX; CHECK(IotProtocol_Encode(&record, wire, sizeof(wire)));
    strcpy(scratch, wire); CHECK(IotProtocol_Decode(scratch, &decoded) && decoded.sequence == UINT32_MAX);
    strcpy(record.text, "HELP|X"); CHECK(!IotProtocol_Encode(&record, wire, sizeof(wire)));
    strcpy(record.text, "HELP\"X"); CHECK(!IotProtocol_Encode(&record, wire, sizeof(wire)));
    strcpy(record.text, "HELP"); memset(record.timestamp, 'X', sizeof(record.timestamp)); CHECK(!IotProtocol_Encode(&record, wire, sizeof(wire)));
    record = Example(); CHECK(!IotProtocol_Encode(&record, wire, 10U));
    record.flags = IOT_FLAG_TIME_VALID | IOT_FLAG_DISTRESS | IOT_FLAG_TRUNCATED; CHECK(!IotProtocol_Encode(&record, wire, sizeof(wire)));
    memset(&event, 0, sizeof(event));
    for (i = 0U; i < 6U; ++i)
    {
        static const char *texts[] = {"sos", "Help", "PLEASE HELP.", "HELPER", "SOS1", "XSOS"};
        MorseMessage_Init(&msg);
        for (text = texts[i]; *text; ++text) { event.type = MORSE_EVENT_CHARACTER; event.character = *text; CHECK(!MorseMessage_Feed(&msg, &event)); }
        event.type = MORSE_EVENT_IDLE; CHECK(MorseMessage_Feed(&msg, &event)); CHECK(MorseMessage_IsDistress(&msg) == (i < 3U));
    }
    MorseMessage_Init(&msg); event.type = MORSE_EVENT_CHARACTER; event.character = 'S';
    for (i = 0U; i < 100U; ++i) (void)MorseMessage_Feed(&msg, &event);
    CHECK(msg.length == 96U && msg.truncated && !MorseMessage_IsDistress(&msg));
    MorseMessage_Init(&msg); event.type = MORSE_EVENT_INVALID; (void)MorseMessage_Feed(&msg, &event);
    CHECK(msg.invalid && !MorseMessage_IsDistress(&msg));
    fprintf(stderr, "PASS: CRC corruption, bounds, serialization, exact distress words, truncation\n");
}
static IotRecord StartQueuedMessage(uint32_t now)
{
    IotRecord record; char copy[IOT_LINE_SIZE];
    ESP8266_Process(now); CHECK(uart_active && strncmp(sent, "M1|", 3U) == 0);
    strcpy(copy, sent); CHECK(IotProtocol_Decode(copy, &record)); FinishTx(); return record;
}
static void Ack(const IotRecord *record, uint32_t now)
{
    char body[100];
    snprintf(body, sizeof(body), "A1|%lu|%lu|%lu|%u", (unsigned long)record->address,
             (unsigned long)record->boot, (unsigned long)record->sequence, (unsigned)record->wire_crc);
    ReceiveBody(body); ESP8266_Process(now);
}
static void InitNet(void)
{
    CHECK(ESP8266_Init(1U, 3U)); ESP8266_Process(0U); CHECK(strncmp(sent, "H1|", 3U) == 0); FinishTx();
}
static void TestTransport(void)
{
    IotRecord record = Example(), sent_record, wrong; ESP8266_Stats stats; unsigned i;
    InitNet(); CHECK(ESP8266_Queue(&record)); sent_record = StartQueuedMessage(10U);
    CHECK(strcmp(ESP8266_GetLastStatus(), "WAIT_MODULE") == 0);
    ReceiveBody("S1|WIFI_CONNECTED"); ESP8266_Process(11U);
    CHECK(strcmp(ESP8266_GetLastStatus(), "WIFI_CONNECTED") == 0);
    ReceiveText("S1|WIFI_DISCONNECTED*0000\n"); ESP8266_Process(12U);
    CHECK(strcmp(ESP8266_GetLastStatus(), "WIFI_CONNECTED") == 0);
    ReceiveBody("S1|ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ"); ESP8266_Process(13U);
    CHECK(strlen(ESP8266_GetLastStatus()) == 39U);
    ESP8266_Process(60009U); CHECK(!uart_active); ESP8266_Process(60010U); CHECK(uart_active); FinishTx();
    ESP8266_GetStats(&stats); CHECK(stats.queued == 1U && stats.retries == 1U && stats.acknowledged == 0U);
    wrong = sent_record; ++wrong.sequence; Ack(&wrong, 60011U); ESP8266_GetStats(&stats); CHECK(stats.queued == 1U);
    Ack(&sent_record, 60012U); ESP8266_GetStats(&stats); CHECK(stats.queued == 0U && stats.acknowledged == 1U);
    InitNet();
    for (i = 0U; i < 8U; ++i) { record.sequence = i + 1U; CHECK(ESP8266_Queue(&record)); }
    CHECK(!ESP8266_Queue(&record)); record.flags |= IOT_FLAG_DISTRESS; strcpy(record.text, "SOS"); record.sequence = 99U;
    CHECK(ESP8266_Queue(&record)); ESP8266_GetStats(&stats); CHECK(stats.queued == 8U && stats.dropped == 2U);
    ESP8266_Process(10U); CHECK(uart_active); record.sequence = 100U; CHECK(ESP8266_Queue(&record)); FinishTx();
    HAL_UART_RxCpltCallback(&other_uart); HAL_UART_TxCpltCallback(&other_uart); HAL_UART_ErrorCallback(&other_uart);
    HAL_UART_ErrorCallback(&huart3); ESP8266_Process(11U); ESP8266_GetStats(&stats); CHECK(stats.receive_errors == 1U);
    ReceiveText("discarded\n"); ReceiveText("A1|bad*0000\n"); ESP8266_Process(12U); ESP8266_GetStats(&stats); CHECK(stats.receive_errors == 2U);
    InitNet(); record = Example(); CHECK(ESP8266_Queue(&record)); fail_tx = true; ESP8266_Process(10U); CHECK(!uart_active);
    ESP8266_Process(11U); CHECK(uart_active); ESP8266_Process(511U); CHECK(!uart_active); /* bounded TX recovery */
    fail_rx = true; CHECK(!ESP8266_Init(1U, 3U)); CHECK(uart_rx == NULL);
    ESP8266_Process(0U); CHECK(uart_rx != NULL); FinishTx();
    fprintf(stderr, "PASS: server ACK matching, retry, queue policy, ISR filtering, RX/TX recovery and TX buffer ownership\n");
}
static void Level(uint32_t duration, bool light)
{
    while (duration--)
    {
        adc_queue[adc_write].raw = light ? 430U : 530U;
        adc_queue[adc_write++].time_ms = ++sample_clock; tick = sample_clock;
        if (adc_write == 32U) LdrReceiver_Process();
    }
    LdrReceiver_Process();
}
static void Code(const char *code)
{
    uint32_t dot = LdrReceiver_GetDotTimeMs();
    while (*code)
    {
        Level(dot * (*code == '-' ? 3U : 1U), true); ++code;
        Level(dot * (*code ? 1U : 3U), false);
    }
}
static void PrepareReceiver(bool rtc_ok)
{
    tick = sample_clock = 0U; adc_read = adc_write = 0U; memset(&adc_stats, 0, sizeof(adc_stats));
    Buzzer_Init(); LdrReceiver_Init(); MessageService_Init(rtc_ok);
    CHECK(ESP8266_Init(DEVICE_ADDRESS, MessageService_GetBootCounter()));
    ESP8266_SetTimeHandler(MessageService_SetNetworkTime); ESP8266_Process(0U); FinishTx();
    Level(LDR_CALIBRATION_MS + 2U * 60U + 16U, false);
}
static void TestReceiverToReport(void)
{
    ESP8266_Stats stats; IotRecord out; MessageService_DisplayState display; LdrReceiver_Status light;
    PrepareReceiver(true);
    MessageService_GetDisplayState(&display); CHECK(display.text[0] == '\0' && display.sequence == 0U && !display.receiving);
    Code("...");
    MessageService_GetDisplayState(&display); CHECK(display.receiving && strcmp(display.text, "S") == 0 && display.sequence == 0U);
    LdrReceiver_GetStatus(&light); CHECK(light.raw == 530U && light.normalized == 3565U && light.dot_ms == 60U && light.calibrated);
    Code("---"); Code("...");
    ESP8266_GetStats(&stats); CHECK(stats.queued == 0U && !Buzzer_IsActive());
    Level(8U * 60U, false); ESP8266_GetStats(&stats); CHECK(stats.queued == 1U && Buzzer_IsActive());
    out = StartQueuedMessage(tick); CHECK(strcmp(out.text, "SOS") == 0 && out.sequence == 1U && out.address == DEVICE_ADDRESS);
    MessageService_GetDisplayState(&display); CHECK(!display.receiving && strcmp(display.text, "SOS") == 0 && display.sequence == 1U);
    CHECK((out.flags & (IOT_FLAG_TIME_VALID | IOT_FLAG_DISTRESS)) == (IOT_FLAG_TIME_VALID | IOT_FLAG_DISTRESS)); Ack(&out, tick);
    MessageService_GetDisplayState(&display); CHECK(strcmp(display.text, "SOS") == 0 && display.sequence == 1U && (display.flags & IOT_FLAG_DISTRESS));
    Code("."); MessageService_GetDisplayState(&display); CHECK(display.receiving && strcmp(display.text, "E") == 0);
    CHECK(LdrReceiver_SetDotTimeMs(120U)); MessageService_GetDisplayState(&display);
    CHECK(!display.receiving && strcmp(display.text, "SOS") == 0 && display.sequence == 1U);
    CHECK(LdrReceiver_SetDotTimeMs(60U)); Level(2U * 60U + 16U, false);
    Code("...."); Code("."); Code(".-.."); Code(".--."); Level(8U * 60U, false);
    out = StartQueuedMessage(tick); CHECK(strcmp(out.text, "HELP") == 0 && out.sequence == 2U); Ack(&out, tick);
    PrepareReceiver(false); Code("..."); CHECK(LdrReceiver_SetDotTimeMs(120U)); Level(2U * 120U + 16U, false);
    Code("---"); Code("..."); Level(8U * 120U, false);
    out = StartQueuedMessage(tick); CHECK(strcmp(out.text, "OS") == 0 && !Buzzer_IsActive() && out.dot_ms == 120U);
    CHECK(!(out.flags & IOT_FLAG_TIME_VALID) && strcmp(out.timestamp, "-") == 0); Ack(&out, tick);
    CHECK(!LdrReceiver_SetDotTimeMs(59U) && LdrReceiver_GetDotTimeMs() == 120U);
    PrepareReceiver(true); Code("..."); sample_clock += 100U; Level(3U * 60U + 16U, false);
    Code("---"); Code("..."); Level(8U * 60U, false); out = StartQueuedMessage(tick);
    CHECK(strcmp(out.text, "OS") == 0 && !Buzzer_IsActive()); Ack(&out, tick);
    ReceiveBody("T1|2029-04-30T12:34:56+08:00"); ESP8266_Process(tick);
    { DS1302_Time time; CHECK(DS1302_ReadTime(&time) && time.year == 2029U && time.second == 56U); }
    PrepareReceiver(true); Code("...."); Code("."); Code(".-.."); Code(".--."); Code("."); Code(".-.");
    Level(8U * 60U, false); out = StartQueuedMessage(tick);
    CHECK(strcmp(out.text, "HELPER") == 0 && !Buzzer_IsActive()); Ack(&out, tick);
    PrepareReceiver(true); Level(7U * 60U, true); Level(3U * 60U, false);
    Code("..."); Code("---"); Code("..."); Level(8U * 60U, false); out = StartQueuedMessage(tick);
    CHECK((out.flags & IOT_FLAG_DECODE_INVALID) != 0U && !Buzzer_IsActive()); Ack(&out, tick);
    PrepareReceiver(true); Code("..."); Code("---"); Code("..."); Level(4U * 60U, false);
    Code("...."); Code("."); Code(".-.."); Code(".--."); Level(8U * 60U, false); out = StartQueuedMessage(tick);
    CHECK(strcmp(out.text, "SOS HELP") == 0 && Buzzer_IsActive()); Ack(&out, tick);
    fprintf(stderr, "PASS: ADC 530/430 -> Morse -> full numbered SOS/HELP -> 60s alarm -> timestamped UART report\n");
}
int main(void)
{
    TestButtonsAndBuzzer(); TestRtc(); TestProtocolAndMessage(); TestTransport(); TestReceiverToReport();
    fprintf(stderr, "PASS: IoT extension, %u assertions (includes pin-level transactions)\n", checks);
    return 0;
}
