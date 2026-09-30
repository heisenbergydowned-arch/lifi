#include "LdrReceiver.h"

#include "LdrReceiver_Config.h"
#include "LdrAdc.h"
#include "MorseDecoder.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ADC_FULL_SCALE 4095U

typedef struct
{
    MorseDecoder decoder;
    uint32_t calibration_sum;
    uint32_t calibration_count;
    uint32_t last_sample_ms;
    uint32_t candidate_since_ms;
    uint32_t last_raw_debug_ms;
    uint32_t last_debug_ms;
    uint32_t last_dropped;
    uint16_t dark_level;
    uint16_t on_threshold;
    uint16_t off_threshold;
    uint16_t median_samples[3];
    uint16_t last_raw, last_normalized;
    uint8_t median_index;
    bool have_sample;
    bool median_ready;
    bool calibrated;
    bool thresholds_valid;
    bool light;
    bool candidate_light;
    bool failed;
} ReceiverState;

static ReceiverState receiver;
static LdrReceiver_EventHandler event_handler;

void LdrReceiver_SetEventHandler(LdrReceiver_EventHandler handler) { event_handler = handler; }
uint32_t LdrReceiver_GetDotTimeMs(void) { return receiver.decoder.dot_time_ms; }
void LdrReceiver_GetStatus(LdrReceiver_Status *status)
{
    if (status == NULL) return;
    status->dot_ms = receiver.decoder.dot_time_ms;
    status->dash_t = receiver.decoder.dash_t;
    status->word_gap_t = receiver.decoder.word_gap_t;
    status->msg_gap_t = receiver.decoder.msg_gap_t;
    status->sample_ms = receiver.last_sample_ms;
    status->raw = receiver.last_raw;
    status->normalized = receiver.last_normalized;
    status->have_sample = receiver.have_sample;
    status->calibrated = receiver.calibrated;
    status->thresholds_valid = receiver.thresholds_valid;
    status->synchronized = receiver.decoder.synchronized;
    status->light = receiver.light;
    status->failed = receiver.failed;
}
bool LdrReceiver_SetProtocolParams(uint32_t dot_ms, uint32_t dash_t,
                                   uint32_t word_gap_t, uint32_t msg_gap_t)
{
    if ((dot_ms < MORSE_DECODER_T_MS_MIN) || (dot_ms > MORSE_DECODER_T_MS_MAX) ||
        (dash_t < MORSE_DECODER_DASH_T_MIN) || (dash_t > MORSE_DECODER_DASH_T_MAX) ||
        (word_gap_t < MORSE_DECODER_WORD_GAP_T_MIN) ||
        (word_gap_t > MORSE_DECODER_WORD_GAP_T_MAX) ||
        (msg_gap_t < MORSE_DECODER_MSG_GAP_T_MIN) ||
        (msg_gap_t > MORSE_DECODER_MSG_GAP_T_MAX))
    {
        return false; /* Invalid C1 frame: keep the old configuration. */
    }
    if ((dot_ms == receiver.decoder.dot_time_ms) &&
        (dash_t == receiver.decoder.dash_t) &&
        (word_gap_t == receiver.decoder.word_gap_t) &&
        (msg_gap_t == receiver.decoder.msg_gap_t))
    {
        return true; /* No change: idempotent, no decoder reset. */
    }
    MorseDecoder_Init(&receiver.decoder, dot_ms, dash_t, word_gap_t, msg_gap_t);
    MorseDecoder_Reset(&receiver.decoder, receiver.last_sample_ms);
    receiver.light = false; receiver.candidate_light = false;
    receiver.candidate_since_ms = receiver.last_sample_ms;
    if (event_handler != NULL) event_handler(NULL, receiver.last_sample_ms);
    printf("[T] t=%lu dash=%lu wg=%lu mg=%lu; partial message discarded, wait dark >=%lu ms.\r\n",
           (unsigned long)dot_ms, (unsigned long)dash_t,
           (unsigned long)word_gap_t, (unsigned long)msg_gap_t,
           (unsigned long)(2U * dot_ms));
    return true;
}
bool LdrReceiver_SetDotTimeMs(uint32_t dot_ms)
{
    /* Hardware button entry: change T only, keep the current gaps. */
    return LdrReceiver_SetProtocolParams(dot_ms, receiver.decoder.dash_t,
                                         receiver.decoder.word_gap_t,
                                         receiver.decoder.msg_gap_t);
}

static uint16_t Normalize(uint16_t raw)
{
#if LDR_BRIGHT_IS_HIGH
    return raw;
#else
    return (uint16_t)(ADC_FULL_SCALE - raw);
#endif
}

static uint16_t Median3(uint16_t raw)
{
    uint16_t a;
    uint16_t b;
    uint16_t c;
    uint16_t swap;

    if (!receiver.median_ready)
    {
        receiver.median_samples[0] = raw;
        receiver.median_samples[1] = raw;
        receiver.median_samples[2] = raw;
        receiver.median_ready = true;
    }
    receiver.median_samples[receiver.median_index] = raw;
    receiver.median_index = (uint8_t)((receiver.median_index + 1U) % 3U);
    a = receiver.median_samples[0];
    b = receiver.median_samples[1];
    c = receiver.median_samples[2];
    if (a > b) { swap = a; a = b; b = swap; }
    if (b > c) { swap = b; b = c; c = swap; }
    if (a > b) { b = a; }
    return b;
}

static void PrintEvents(void)
{
    MorseDecoderEvent event;

    while (MorseDecoder_PopEvent(&receiver.decoder, &event))
    {
        if (event_handler != NULL) event_handler(&event, receiver.last_sample_ms);
        switch (event.type)
        {
        case MORSE_EVENT_CHARACTER:
            printf("[RX] %c  %s\r\n", event.character, event.code);
            break;
        case MORSE_EVENT_SPACE:
            printf("[RX] <space>\r\n");
            break;
        case MORSE_EVENT_IDLE:
            printf("[RX] <idle>\r\n");
            break;
        case MORSE_EVENT_INVALID:
            printf("[RX] ?  invalid=%s\r\n", event.code);
            break;
        default:
            break;
        }
    }
}

static void Calibrate(uint16_t normalized, uint32_t now_ms)
{
    receiver.calibration_sum += normalized;
    ++receiver.calibration_count;
    if (receiver.calibration_count < LDR_CALIBRATION_MS)
    {
        return;
    }

    receiver.dark_level = (uint16_t)(receiver.calibration_sum /
                                      receiver.calibration_count);
    receiver.calibrated = true;
    receiver.thresholds_valid =
        ((uint32_t)receiver.dark_level + LDR_ON_DELTA_COUNTS < ADC_FULL_SCALE);

    if (!receiver.thresholds_valid)
    {
        printf("[LDR] CAL FAIL: dark=%u, no bright headroom. Torch OFF, check wiring/light, reset.\r\n",
               (unsigned int)receiver.dark_level);
        return;
    }

    receiver.on_threshold = (uint16_t)(receiver.dark_level + LDR_ON_DELTA_COUNTS);
    receiver.off_threshold = (uint16_t)(receiver.dark_level + LDR_OFF_DELTA_COUNTS);
    receiver.light = false;
    receiver.candidate_light = false;
    receiver.candidate_since_ms = now_ms;
    MorseDecoder_Reset(&receiver.decoder, now_ms);
    /* Calibration already observed a full second with the torch off. The
     * decoder still requires its own dark synchronization before first light. */
    printf("[LDR] CAL OK: dark=%u on=%u off=%u (normalized ADC counts). Wait %lu ms, then send.\r\n",
           (unsigned int)receiver.dark_level,
           (unsigned int)receiver.on_threshold,
           (unsigned int)receiver.off_threshold,
           (unsigned long)(2U * receiver.decoder.dot_time_ms));
#if LDR_BRIGHT_IS_HIGH
    printf("[LDR] RAW: ambient=%u ON>=%u OFF<=%u (median ADC counts).\r\n",
           (unsigned int)receiver.dark_level,
           (unsigned int)receiver.on_threshold,
           (unsigned int)receiver.off_threshold);
#else
    printf("[LDR] RAW: ambient=%u ON<=%u OFF>=%u (median ADC counts).\r\n",
           (unsigned int)(ADC_FULL_SCALE - receiver.dark_level),
           (unsigned int)(ADC_FULL_SCALE - receiver.on_threshold),
           (unsigned int)(ADC_FULL_SCALE - receiver.off_threshold));
#endif
}

static void DecodeSample(uint16_t normalized, uint32_t now_ms)
{
    bool wanted = receiver.light;

    if (!receiver.light && (normalized >= receiver.on_threshold))
    {
        wanted = true;
    }
    else if (receiver.light && (normalized <= receiver.off_threshold))
    {
        wanted = false;
    }

    if (wanted == receiver.light)
    {
        receiver.candidate_light = receiver.light;
        receiver.candidate_since_ms = now_ms;
    }
    else if (wanted != receiver.candidate_light)
    {
        receiver.candidate_light = wanted;
        receiver.candidate_since_ms = now_ms;
    }
    else if ((uint32_t)(now_ms - receiver.candidate_since_ms) >= LDR_EDGE_STABLE_MS)
    {
        receiver.light = wanted;
    }

    MorseDecoder_Feed(&receiver.decoder, receiver.light, now_ms);
    PrintEvents();
}

static void PrintDiagnostics(uint16_t raw, uint16_t normalized, uint32_t now_ms)
{
#if LDR_DEBUG_ADC
    uint32_t denominator;
    uint32_t numerator;
    uint32_t voltage_mv;

    /* Keep the original DMA value, before median filtering and normalization.
     * Use acquisition time: UART delivery can lag behind the DMA sample. */
    if ((uint32_t)(now_ms - receiver.last_raw_debug_ms) >= LDR_DEBUG_RAW_PERIOD_MS)
    {
        receiver.last_raw_debug_ms = now_ms;
        printf("[ADC] t_ms=%lu raw=%u\r\n",
               (unsigned long)now_ms, (unsigned int)raw);
    }

    if ((uint32_t)(now_ms - receiver.last_debug_ms) < LDR_DEBUG_PERIOD_MS)
    {
        return;
    }
    receiver.last_debug_ms = now_ms;
    voltage_mv = ((uint32_t)raw * LDR_ADC_REFERENCE_MV) / ADC_FULL_SCALE;
#if LDR_BRIGHT_IS_HIGH
    denominator = raw;
    numerator = ADC_FULL_SCALE - raw;
#else
    /* Reversed divider: 3.3V -- fixed resistor -- PA0 -- LDR -- GND. */
    denominator = ADC_FULL_SCALE - raw;
    numerator = raw;
#endif
    if ((raw == 0U) || (raw == ADC_FULL_SCALE))
    {
        printf("[LDR] adc=%u norm=%u mV=%lu R=out-of-range light=%u\r\n",
               (unsigned int)raw, (unsigned int)normalized,
               (unsigned long)voltage_mv, (unsigned int)receiver.light);
    }
    else
    {
        uint32_t resistance = (LDR_FIXED_RESISTOR_OHM * numerator) / denominator;
        printf("[LDR] adc=%u norm=%u mV=%lu R=%luohm light=%u\r\n",
               (unsigned int)raw, (unsigned int)normalized,
               (unsigned long)voltage_mv, (unsigned long)resistance,
               (unsigned int)receiver.light);
    }
#else
    (void)raw;
    (void)normalized;
    (void)now_ms;
#endif
}

void LdrReceiver_Init(void)
{
    memset(&receiver, 0, sizeof(receiver));
    event_handler = NULL;
    MorseDecoder_Init(&receiver.decoder, LDR_MORSE_DOT_MS,
                      MORSE_DECODER_DEFAULT_DASH_T,
                      MORSE_DECODER_DEFAULT_WORD_GAP_T,
                      MORSE_DECODER_DEFAULT_MSG_GAP_T);
    printf("\r\nSTM32F103 LDR Morse receiver\r\n");
    printf("PA0 ADC1, TIM3=%luHz, USART1 PA9 TX 115200 8N1\r\n",
           (unsigned long)LDR_ADC_SAMPLE_RATE_HZ);
    printf("Morsify: Flash, Auto Loop OFF, t=%lu dash=%lu wg=%lu mg=%lu.\r\n",
           (unsigned long)LDR_MORSE_DOT_MS,
           (unsigned long)MORSE_DECODER_DEFAULT_DASH_T,
           (unsigned long)MORSE_DECODER_DEFAULT_WORD_GAP_T,
           (unsigned long)MORSE_DECODER_DEFAULT_MSG_GAP_T);
#if LDR_DEBUG_ADC
    printf("[ADC] raw=0..4095 before filtering; t_ms=sample time; interval=%lu ms.\r\n",
           (unsigned long)LDR_DEBUG_RAW_PERIOD_MS);
#endif
    printf("[LDR] Keep torch OFF for %lu ms ambient calibration.\r\n",
           (unsigned long)LDR_CALIBRATION_MS);
}

void LdrReceiver_Process(void)
{
    LdrAdcStats stats;
    LdrAdcSample sample;

    if (receiver.failed)
    {
        return;
    }
    LdrAdc_GetStats(&stats);
    if (stats.hardware_errors != 0U)
    {
        LdrAdcStatus stop_status = LdrAdc_Stop();
        receiver.failed = true;
        MorseDecoder_Reset(&receiver.decoder, receiver.last_sample_ms);
        if (event_handler != NULL) event_handler(NULL, receiver.last_sample_ms);
        printf("[LDR] ADC/DMA error=%lu, stopped (status=%u). Reset board.\r\n",
               (unsigned long)stats.hardware_errors, (unsigned int)stop_status);
        return;
    }
    if (stats.dropped_samples != receiver.last_dropped)
    {
        receiver.last_dropped = stats.dropped_samples;
        printf("[LDR] WARNING: dropped samples=%lu; affected characters are discarded.\r\n",
               (unsigned long)stats.dropped_samples);
    }

    while (LdrAdc_Read(&sample))
    {
        uint16_t filtered;
        uint16_t normalized;

        if (receiver.have_sample &&
            ((uint32_t)(sample.time_ms - receiver.last_sample_ms) != 1U))
        {
            /* Do not join pulses across missing ADC samples. */
            MorseDecoder_Reset(&receiver.decoder, sample.time_ms);
            if (event_handler != NULL) event_handler(NULL, sample.time_ms);
            receiver.light = false;
            receiver.candidate_light = false;
            receiver.candidate_since_ms = sample.time_ms;
            receiver.median_ready = false;
            if (!receiver.calibrated)
            {
                receiver.calibration_count = 0U;
                receiver.calibration_sum = 0U;
            }
        }
        receiver.have_sample = true;
        receiver.last_sample_ms = sample.time_ms;
        filtered = Median3(sample.raw);
        normalized = Normalize(filtered);
        receiver.last_raw = sample.raw;
        receiver.last_normalized = normalized;

        if (!receiver.calibrated)
        {
            Calibrate(normalized, sample.time_ms);
        }
        else if (receiver.thresholds_valid)
        {
            DecodeSample(normalized, sample.time_ms);
        }
        PrintDiagnostics(sample.raw, normalized, sample.time_ms);
    }
}
