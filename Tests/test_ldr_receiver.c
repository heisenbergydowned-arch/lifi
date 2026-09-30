/* Host integration test. Compile real LdrReceiver.c and MorseDecoder.c with:
 * -std=c99 -Wall -Wextra -Werror -Dprintf=receiver_test_printf
 * MinGW additionally needs -D__USE_MINGW_ANSI_STDIO=0.
 * -I Tests/receiver_stubs -I Software -I Hardware
 * -DLDR_MORSE_DOT_MS=60 (and again with 240).
 * This substitutes acquisition and captures printf; production is not edited.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "LdrAdc.h"
#include "LdrReceiver.h"
#include "LdrReceiver_Config.h"

static char log_text[524288];
static size_t log_used;
static LdrAdcSample queue[128];
static unsigned queue_read, queue_write;
static uint32_t sample_ms;
static LdrAdcStats stats;
static unsigned stop_calls;
static unsigned failures;

int receiver_test_printf(const char *format, ...)
{
    va_list args;
    int result;
    va_start(args, format);
    result = vsnprintf(log_text + log_used, sizeof(log_text) - log_used,
                       format, args);
    va_end(args);
    assert(result >= 0 && (size_t)result < sizeof(log_text) - log_used);
    log_used += (size_t)result;
    return result;
}

bool LdrAdc_Read(LdrAdcSample *sample)
{
    if (queue_read == queue_write) return false;
    *sample = queue[queue_read++];
    if (queue_read == queue_write) queue_read = queue_write = 0;
    return true;
}

void LdrAdc_GetStats(LdrAdcStats *out) { *out = stats; }
LdrAdcStatus LdrAdc_Stop(void)
{
    ++stop_calls;
    stats.running = false;
    return LDR_ADC_OK;
}

static void reset_test(void)
{
    memset(log_text, 0, sizeof(log_text));
    log_used = 0;
    queue_read = queue_write = 0;
    sample_ms = 0;
    stop_calls = 0;
    memset(&stats, 0, sizeof(stats));
    stats.running = true;
    LdrReceiver_Init();
}

static void clear_log(void) { log_used = 0; log_text[0] = '\0'; }

static void check(bool passed, const char *case_name)
{
    fprintf(stderr, "%s dot=%u polarity=%u: %s\n", passed ? "PASS" : "FAIL",
            (unsigned)LDR_MORSE_DOT_MS, (unsigned)LDR_BRIGHT_IS_HIGH, case_name);
    if (!passed) { ++failures; fprintf(stderr, "%s\n", log_text); }
}

static void sample(uint16_t normalized)
{
    assert(queue_write < sizeof(queue) / sizeof(queue[0]));
    queue[queue_write].time_ms = ++sample_ms;
#if LDR_BRIGHT_IS_HIGH
    queue[queue_write++].raw = normalized;
#else
    queue[queue_write++].raw = (uint16_t)(4095U - normalized);
#endif
    if (queue_write == 32U) LdrReceiver_Process();
}

static void level(uint32_t count, uint16_t normalized)
{
    while (count--) sample(normalized);
    LdrReceiver_Process();
}

static void ready(void)
{
    reset_test();
    level(LDR_CALIBRATION_MS, 1000U);
    level(2U * LDR_MORSE_DOT_MS + 16U, 1000U);
    clear_log();
}

static void noisy_level(uint32_t count, uint16_t normalized)
{
    uint32_t i;
    for (i = 0; i < count; ++i) {
        int noise = (int)(i % 61U) - 30;
        uint16_t value = (uint16_t)((int)normalized + noise);
        if (i % 137U == 66U) value = normalized > 2000U ? 1000U : 3000U;
        sample(value);
    }
    LdrReceiver_Process();
}

static void send_code(const char *code, bool noisy)
{
    unsigned i;
    for (i = 0; code[i]; ++i) {
        uint32_t on_ms = LDR_MORSE_DOT_MS * (code[i] == '-' ? 3U : 1U);
        uint32_t off_ms = LDR_MORSE_DOT_MS * (code[i + 1] ? 1U : 3U);
        if (noisy) { noisy_level(on_ms, 3000U); noisy_level(off_ms, 1000U); }
        else { level(on_ms, 3000U); level(off_ms, 1000U); }
    }
}

static bool characters_equal(const char *expected)
{
    char actual[256];
    size_t used = 0;
    const char *at = log_text;
    while ((at = strstr(at, "[RX] ")) != NULL) {
        char c = at[5];
        if (c != '<' && used < sizeof(actual) - 1U) actual[used++] = c;
        at += 5;
    }
    actual[used] = '\0';
    return strcmp(actual, expected) == 0;
}

int main(void)
{
    const char *hello[] = {"....", ".", ".-..", ".-..", "---"};
    char expected_calibration[96];
    unsigned i;

    reset_test();
    level(LDR_CALIBRATION_MS - 1U, 1000U);
    check(strstr(log_text, "CAL OK") == NULL, "calibration waits for full sample count");
    level(1, 1000U);
    snprintf(expected_calibration, sizeof(expected_calibration),
             "CAL OK: dark=1000 on=%u off=%u",
             (unsigned)(1000U + LDR_ON_DELTA_COUNTS),
             (unsigned)(1000U + LDR_OFF_DELTA_COUNTS));
    check(strstr(log_text, expected_calibration) != NULL,
          "ambient calibration produces thresholds");

    ready();
    for (i = 0; i < 5; ++i) send_code(hello[i], false);
    level(12U * LDR_MORSE_DOT_MS, 1000U);
    check(characters_equal("HELLO") && strstr(log_text, "<space>") == NULL &&
          strstr(log_text, "<idle>") != NULL, "HELLO clean complete receive chain");

    ready();
    for (i = 0; i < 5; ++i) send_code(hello[i], true);
    level(12U * LDR_MORSE_DOT_MS, 1000U);
    check(characters_equal("HELLO") && strstr(log_text, "<space>") == NULL,
          "HELLO with ADC noise and isolated full-scale glitches");

    ready();
    for (i = 0; i < 20; ++i) { level(1U, 3000U); level(20U, 1000U); }
    level(12U * LDR_MORSE_DOT_MS, 1000U);
    check(characters_equal(""), "single-sample bright glitches rejected");

    ready();
    level(3U * LDR_MORSE_DOT_MS, 1300U);
    level(3U * LDR_MORSE_DOT_MS,
          (uint16_t)(1000U + (LDR_ON_DELTA_COUNTS + LDR_OFF_DELTA_COUNTS) / 2U));
    level(12U * LDR_MORSE_DOT_MS, 1000U);
    check(characters_equal("T"), "hysteresis preserves light through intermediate level");

    ready();
    level(LDR_MORSE_DOT_MS, 3000U);
    sample_ms += 900U;
    ++stats.dropped_samples;
    level(3U * LDR_MORSE_DOT_MS + 16U, 1000U);
    send_code(".", false);
    level(12U * LDR_MORSE_DOT_MS, 1000U);
    check(characters_equal("E") && strstr(log_text, "WARNING: dropped") != NULL,
          "time gap discards partial pulse then resynchronizes");

    reset_test();
    level(LDR_CALIBRATION_MS / 2U, 1000U);
    sample_ms += 100U;
    level(LDR_CALIBRATION_MS - 1U, 1000U);
    check(strstr(log_text, "CAL OK") == NULL, "gap restarts unfinished calibration");
    level(1U, 1000U);
    check(strstr(log_text, "CAL OK") != NULL, "calibration completes after clean restart");

    ready();
    stats.hardware_errors = 1U;
    LdrReceiver_Process();
    check(stop_calls == 1U && strstr(log_text, "ADC/DMA error=1") != NULL,
          "ADC error stops acquisition and reports failure");
    clear_log();
    LdrReceiver_Process();
    LdrReceiver_Process();
    check(stop_calls == 1U && log_used == 0U, "ADC failure latches without repeated logging");

    reset_test();
    level(LDR_CALIBRATION_MS, 4090U);
    level(3U * LDR_MORSE_DOT_MS, 4095U);
    level(12U * LDR_MORSE_DOT_MS, 3000U);
    check(strstr(log_text, "CAL FAIL") != NULL && characters_equal(""),
          "saturated ambient level refuses to decode");

#if !LDR_BRIGHT_IS_HIGH
    /* User-reported static levels replayed as ideal pulses, not a captured
     * optical waveform. level() accepts normalized values; the stub converts
     * these back to actual raw ADC 530 (ambient) and 430 (illuminated). */
    reset_test();
    level(LDR_CALIBRATION_MS + 2U * LDR_MORSE_DOT_MS + 16U, 4095U - 530U);
    clear_log();
    level(LDR_MORSE_DOT_MS, 4095U - 430U);
    level(LDR_MORSE_DOT_MS, 4095U - 530U);
    level(3U * LDR_MORSE_DOT_MS, 4095U - 430U);
    level(12U * LDR_MORSE_DOT_MS, 4095U - 530U);
    check(characters_equal("A") && strstr(log_text, "<idle>") != NULL,
          "reported raw ADC 530/430 ideal pulses decode A");
#endif

#if LDR_DEBUG_ADC
    ready();
    level(LDR_DEBUG_PERIOD_MS + 1U, 2048U);
    check(strstr(log_text, "R=9995ohm") != NULL,
          "divider resistance calculation at normalized ADC 2048");
    clear_log();
    level(LDR_DEBUG_PERIOD_MS + 1U, 0U);
    check(strstr(log_text, "R=out-of-range") != NULL,
          "divider zero denominator prints out-of-range");
#endif

    fprintf(stderr, "Receiver integration failures: %u\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
