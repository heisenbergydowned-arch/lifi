#include "LdrAdc.h"
#include "adc.h"
#include "tim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ADC_TypeDef mock_adc1;
TIM_TypeDef mock_tim3;
ADC_HandleTypeDef hadc1 = {ADC1};
TIM_HandleTypeDef htim3 = {TIM3};

static uint32_t tick_ms;
static bool callback_during_tick_read;
static uint16_t *dma_buffer;
static uint32_t dma_length;
static HAL_StatusTypeDef calibration_result = HAL_OK;
static HAL_StatusTypeDef adc_start_result = HAL_OK;
static HAL_StatusTypeDef adc_stop_result = HAL_OK;
static HAL_StatusTypeDef timer_start_result = HAL_OK;
static HAL_StatusTypeDef timer_stop_result = HAL_OK;
static unsigned int calibration_calls;
static unsigned int adc_start_calls;
static unsigned int timer_start_calls;
static unsigned int adc_stop_calls;
static unsigned int timer_stop_calls;
static uint32_t timer_counter;
static uint32_t cleared_flag;
static char calls[128];
static unsigned int calls_length;
static unsigned int checks;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s (HAL calls: %s)\n", \
                __LINE__, #condition, calls); \
        exit(1); \
    } \
} while (0)

static void Record(char operation)
{
    CHECK(calls_length < sizeof(calls) - 1U);
    calls[calls_length++] = operation;
    calls[calls_length] = '\0';
}

static void ClearCalls(void)
{
    calls_length = 0U;
    calls[0] = '\0';
}

uint32_t HAL_GetTick(void)
{
    uint32_t snapshot = tick_ms;
    if (callback_during_tick_read)
    {
        /* Model an IRQ after GetTick read its return value but before the
         * caller resumes: the callback publishes a newer DMA timestamp. */
        callback_during_tick_read = false;
        tick_ms++;
        HAL_ADC_ConvHalfCpltCallback(&hadc1);
    }
    return snapshot;
}

HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *hadc)
{
    CHECK(hadc == &hadc1);
    CHECK(hadc->Instance == ADC1);
    calibration_calls++;
    Record('C');
    return calibration_result;
}

HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *hadc,
                                  uint32_t *destination, uint32_t length)
{
    CHECK(hadc == &hadc1);
    CHECK(destination != NULL);
    dma_buffer = (uint16_t *)destination;
    dma_length = length;
    adc_start_calls++;
    Record('A');
    return adc_start_result;
}

HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *hadc)
{
    CHECK(hadc == &hadc1);
    adc_stop_calls++;
    Record('a');
    return adc_stop_result;
}

HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *timer)
{
    CHECK(timer == &htim3);
    CHECK(timer->Instance == TIM3);
    timer_start_calls++;
    Record('T');
    return timer_start_result;
}

HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *timer)
{
    CHECK(timer == &htim3);
    timer_stop_calls++;
    Record('t');
    return timer_stop_result;
}

void Mock_SetCounter(TIM_HandleTypeDef *timer, uint32_t counter)
{
    CHECK(timer == &htim3);
    timer_counter = counter;
}

void Mock_ClearFlag(TIM_HandleTypeDef *timer, uint32_t flag)
{
    CHECK(timer == &htim3);
    cleared_flag = flag;
}

static LdrAdcStats Stats(void)
{
    LdrAdcStats stats;
    LdrAdc_GetStats(&stats);
    return stats;
}

static void StartAcquisition(void)
{
    timer_counter = 999U;
    cleared_flag = 0U;
    CHECK(LdrAdc_Init() == LDR_ADC_OK);
    ClearCalls();
    CHECK(LdrAdc_Start() == LDR_ADC_OK);
    CHECK(strcmp(calls, "AT") == 0); /* DMA is armed before trigger starts. */
    CHECK(timer_counter == 0U);
    CHECK(cleared_flag == TIM_FLAG_UPDATE);
    CHECK(dma_length == 64U);
    CHECK(Stats().running);
}

static void ProduceHalf(unsigned int block, uint32_t firstSample)
{
    uint32_t i;
    uint32_t offset = (block & 1U) ? (dma_length / 2U) : 0U;
    for (i = 0U; i < dma_length / 2U; i++)
    {
        dma_buffer[offset + i] = (uint16_t)(firstSample + i);
    }
    tick_ms += 32U;
    if ((block & 1U) == 0U)
    {
        HAL_ADC_ConvHalfCpltCallback(&hadc1);
    }
    else
    {
        HAL_ADC_ConvCpltCallback(&hadc1);
    }
}

static void ReadRange(uint32_t first, uint32_t last)
{
    uint32_t i;
    LdrAdcSample sample;
    for (i = first; i <= last; i++)
    {
        CHECK(LdrAdc_Read(&sample));
        CHECK(sample.raw == (uint16_t)i);
        CHECK(sample.time_ms == i);
    }
}

static void TestInitialization(void)
{
    LdrAdcStats stats;
    CHECK(LdrAdc_Start() == LDR_ADC_BAD_STATE);
    CHECK(LdrAdc_Stop() == LDR_ADC_BAD_STATE);
    hadc1.Instance = NULL;
    CHECK(LdrAdc_Init() == LDR_ADC_BAD_STATE);
    hadc1.Instance = ADC1;
    htim3.Instance = NULL;
    CHECK(LdrAdc_Init() == LDR_ADC_BAD_STATE);
    htim3.Instance = TIM3;
    CHECK(calibration_calls == 0U);

    calibration_result = HAL_ERROR;
    CHECK(LdrAdc_Init() == LDR_ADC_CALIBRATION_ERROR);
    CHECK(LdrAdc_Start() == LDR_ADC_BAD_STATE);
    CHECK(calibration_calls == 1U);
    calibration_result = HAL_OK;
    CHECK(LdrAdc_Init() == LDR_ADC_OK);
    CHECK(calibration_calls == 2U);
    /* Recalibration is explicitly allowed while stopped. */
    CHECK(LdrAdc_Init() == LDR_ADC_OK);
    CHECK(calibration_calls == 3U);
    stats = Stats();
    CHECK(!stats.running && stats.hardware_errors == 0U &&
          stats.dropped_samples == 0U);

    ClearCalls();
    CHECK(LdrAdc_Start() == LDR_ADC_OK);
    CHECK(strcmp(calls, "AT") == 0);
    CHECK(LdrAdc_Start() == LDR_ADC_BAD_STATE);
    CHECK(LdrAdc_Init() == LDR_ADC_BAD_STATE);
    CHECK(adc_start_calls == 1U && timer_start_calls == 1U);
    CHECK(calibration_calls == 3U);

    ClearCalls();
    CHECK(LdrAdc_Stop() == LDR_ADC_OK);
    CHECK(strcmp(calls, "ta") == 0); /* Stop the producer before its DMA. */
    CHECK(!Stats().running);
    CHECK(LdrAdc_Stop() == LDR_ADC_OK);
    CHECK(timer_stop_calls == 1U && adc_stop_calls == 1U);
    LdrAdc_GetStats(NULL);
}

static void TestStartFailure(bool timerFailure)
{
    CHECK(LdrAdc_Init() == LDR_ADC_OK);
    if (timerFailure)
    {
        timer_start_result = HAL_ERROR;
    }
    else
    {
        adc_start_result = HAL_ERROR;
    }
    ClearCalls();
    CHECK(LdrAdc_Start() == LDR_ADC_START_ERROR);
    CHECK(!Stats().running);
    if (timerFailure)
    {
        CHECK(strcmp(calls, "ATta") == 0);
    }
    else
    {
        CHECK(strcmp(calls, "Aa") == 0);
        CHECK(timer_start_calls == 0U);
    }
    CHECK(adc_stop_calls == 1U);

    /* A failed start can be retried after its HAL fault is removed. */
    adc_start_result = HAL_OK;
    timer_start_result = HAL_OK;
    ClearCalls();
    CHECK(LdrAdc_Start() == LDR_ADC_OK);
    CHECK(strcmp(calls, "AT") == 0);
    ProduceHalf(0U, 1U);
    ReadRange(1U, 32U);
    CHECK(LdrAdc_Stop() == LDR_ADC_OK);
}

static void TestStopFailure(bool timerFailure)
{
    LdrAdcSample sample;
    StartAcquisition();
    if (timerFailure)
    {
        timer_stop_result = HAL_ERROR;
    }
    else
    {
        adc_stop_result = HAL_ERROR;
    }
    ClearCalls();
    CHECK(LdrAdc_Stop() == LDR_ADC_STOP_ERROR);
    CHECK(strcmp(calls, "ta") == 0);
    CHECK(!Stats().running);
    /* Late completion callbacks cannot publish data after Stop. */
    ProduceHalf(0U, 1U);
    CHECK(!LdrAdc_Read(&sample));
}

static void TestSamplesAndFiltering(void)
{
    ADC_HandleTypeDef otherAdc = {ADC1};
    LdrAdcSample sample;
    StartAcquisition();
    CHECK(!LdrAdc_Read(NULL));
    CHECK(!LdrAdc_Read(&sample));
    tick_ms += 16U;
    HAL_ADC_ConvHalfCpltCallback(&otherAdc);
    HAL_ADC_ConvCpltCallback(&otherAdc);
    HAL_ADC_ErrorCallback(&otherAdc);
    CHECK(!LdrAdc_Read(&sample));
    CHECK(Stats().hardware_errors == 0U);
    tick_ms -= 16U;

    ProduceHalf(0U, 1U);
    ProduceHalf(1U, 33U);
    ReadRange(1U, 64U);
    CHECK(!LdrAdc_Read(&sample));
    ProduceHalf(2U, 65U);
    ReadRange(65U, 96U);
    CHECK(!LdrAdc_Read(&sample));
    CHECK(Stats().dropped_samples == 0U);
    CHECK(Stats().hardware_errors == 0U);

    CHECK(LdrAdc_Stop() == LDR_ADC_OK);
    HAL_ADC_ErrorCallback(&hadc1);
    ProduceHalf(3U, 97U);
    CHECK(!LdrAdc_Read(&sample));
    CHECK(Stats().hardware_errors == 0U);

    CHECK(LdrAdc_Start() == LDR_ADC_OK);
    ProduceHalf(0U, 1U); /* New session timestamps begin at 1 ms again. */
    ReadRange(1U, 32U);
    CHECK(!LdrAdc_Read(&sample));
}

static void TestQueueOverflow(void)
{
    unsigned int block;
    LdrAdcSample sample;
    StartAcquisition();
    for (block = 0U; block < 10U; block++)
    {
        ProduceHalf(block, (32U * block) + 1U);
    }
    /* The 256-entry SPSC ring reserves one slot: keep the first 255. */
    CHECK(Stats().dropped_samples == 65U);
    CHECK(Stats().hardware_errors == 0U);
    ReadRange(1U, 100U);
    ProduceHalf(10U, 321U);
    ReadRange(101U, 255U); /* Previously unread samples were not overwritten. */
    ReadRange(321U, 352U); /* Lost time remains visible in the timestamps. */
    CHECK(!LdrAdc_Read(&sample));
    CHECK(Stats().dropped_samples == 65U);
    CHECK(LdrAdc_Stop() == LDR_ADC_OK);
    CHECK(LdrAdc_Start() == LDR_ADC_OK);
    CHECK(Stats().dropped_samples == 0U);
    CHECK(!LdrAdc_Read(&sample));
}

static void TestAdcError(void)
{
    LdrAdcSample sample;
    StartAcquisition();
    HAL_ADC_ErrorCallback(&hadc1);
    CHECK(Stats().hardware_errors == 1U);
    ProduceHalf(0U, 1U);
    CHECK(!LdrAdc_Read(&sample));
    CHECK(Stats().hardware_errors == 1U);
    CHECK(LdrAdc_Stop() == LDR_ADC_OK);
    CHECK(LdrAdc_Start() == LDR_ADC_OK);
    CHECK(Stats().hardware_errors == 0U);
    ProduceHalf(0U, 1U);
    ReadRange(1U, 32U);
}

static void TestDelayedCallback(void)
{
    LdrAdcSample sample;
    StartAcquisition();
    ProduceHalf(0U, 1U);
    ReadRange(1U, 32U);
    tick_ms += 31U;
    ProduceHalf(1U, 33U); /* 31+32 = 63 ms: still before a whole DMA wrap. */
    CHECK(Stats().hardware_errors == 0U);
    ReadRange(33U, 64U);
    tick_ms += 64U; /* Exactly one whole buffer period must be rejected. */
    HAL_ADC_ConvHalfCpltCallback(&hadc1);
    CHECK(Stats().hardware_errors == 1U);
    CHECK(!LdrAdc_Read(&sample));
    HAL_ADC_ConvHalfCpltCallback(&hadc1);
    CHECK(!LdrAdc_Read(&sample));
    CHECK(Stats().hardware_errors == 1U);
}

static void TestStatsInterruptedByCallback(void)
{
    uint32_t i;
    LdrAdcStats stats;
    StartAcquisition();
    for (i = 0U; i < dma_length / 2U; i++)
    {
        dma_buffer[i] = (uint16_t)(i + 1U);
    }
    tick_ms = 31U;
    callback_during_tick_read = true;
    LdrAdc_GetStats(&stats);
    CHECK(!callback_during_tick_read);
    CHECK(tick_ms == 32U);
    CHECK(stats.running);
    CHECK(stats.hardware_errors == 0U);
    CHECK(stats.dropped_samples == 0U);
    ReadRange(1U, 32U); /* Prove the IRQ actually ran and published samples. */
    CHECK(Stats().hardware_errors == 0U);
}

static void TestMissingCallbackAndTickWrap(void)
{
    tick_ms = UINT32_MAX - 63U;
    StartAcquisition();
    ProduceHalf(0U, 1U);
    ProduceHalf(1U, 33U); /* HAL millisecond tick wraps here. */
    CHECK(tick_ms == 0U);
    ReadRange(1U, 64U);
    CHECK(Stats().hardware_errors == 0U);
    tick_ms += 128U;
    CHECK(Stats().hardware_errors == 0U);
    tick_ms += 1U;
    CHECK(Stats().hardware_errors > 0U);
    CHECK(LdrAdc_Stop() == LDR_ADC_OK);
    tick_ms += 10000U;
    CHECK(Stats().hardware_errors == 0U); /* Stopped acquisition is not stalled. */
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Specify one test case.\n");
        return 2;
    }
    if (strcmp(argv[1], "initialization") == 0) TestInitialization();
    else if (strcmp(argv[1], "adc-start-failure") == 0) TestStartFailure(false);
    else if (strcmp(argv[1], "timer-start-failure") == 0) TestStartFailure(true);
    else if (strcmp(argv[1], "adc-stop-failure") == 0) TestStopFailure(false);
    else if (strcmp(argv[1], "timer-stop-failure") == 0) TestStopFailure(true);
    else if (strcmp(argv[1], "samples-and-filtering") == 0) TestSamplesAndFiltering();
    else if (strcmp(argv[1], "queue-overflow") == 0) TestQueueOverflow();
    else if (strcmp(argv[1], "adc-error") == 0) TestAdcError();
    else if (strcmp(argv[1], "delayed-callback") == 0) TestDelayedCallback();
    else if (strcmp(argv[1], "stats-interrupted-by-callback") == 0) TestStatsInterruptedByCallback();
    else if (strcmp(argv[1], "missing-callback-and-wrap") == 0) TestMissingCallbackAndTickWrap();
    else
    {
        fprintf(stderr, "Unknown test case: %s\n", argv[1]);
        return 2;
    }
    printf("PASS: production LdrAdc.c, %s, %u checks\n", argv[1], checks);
    return 0;
}
