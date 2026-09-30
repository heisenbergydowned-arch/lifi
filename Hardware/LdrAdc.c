#include "LdrAdc.h"

#include "adc.h"
#include "tim.h"
#include <stddef.h>

#define DMA_SAMPLE_COUNT 64U
#define QUEUE_COUNT      256U
#define QUEUE_MASK       (QUEUE_COUNT - 1U)

/* F103 SRAM is DMA-accessible and has no data cache. DMA transfers halfwords. */
static uint16_t dma_samples[DMA_SAMPLE_COUNT];
static LdrAdcSample sample_queue[QUEUE_COUNT];
static volatile uint32_t queue_write;
static volatile uint32_t queue_read;
static volatile uint32_t dropped_samples;
static volatile uint32_t hardware_errors;
static volatile uint32_t last_dma_tick_ms;
static volatile bool running;
static bool initialized;
static uint32_t sample_time_ms;

static void ResetQueue(void)
{
    queue_write = 0U;
    queue_read = 0U;
    dropped_samples = 0U;
    hardware_errors = 0U;
    sample_time_ms = 0U;
    last_dma_tick_ms = HAL_GetTick();
}

LdrAdcStatus LdrAdc_Init(void)
{
    if (running || (hadc1.Instance != ADC1) || (htim3.Instance != TIM3))
    {
        return LDR_ADC_BAD_STATE;
    }
    initialized = false;
    ResetQueue();
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        return LDR_ADC_CALIBRATION_ERROR;
    }
    initialized = true;
    return LDR_ADC_OK;
}

LdrAdcStatus LdrAdc_Start(void)
{
    if (!initialized || running)
    {
        return LDR_ADC_BAD_STATE;
    }

    ResetQueue();
    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);
    running = true;

    /* Consumer first: no timer trigger can arrive before DMA is ready. */
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)dma_samples,
                          DMA_SAMPLE_COUNT) != HAL_OK)
    {
        running = false;
        (void)HAL_ADC_Stop_DMA(&hadc1);
        return LDR_ADC_START_ERROR;
    }
    if (HAL_TIM_Base_Start(&htim3) != HAL_OK)
    {
        running = false;
        (void)HAL_TIM_Base_Stop(&htim3);
        (void)HAL_ADC_Stop_DMA(&hadc1);
        return LDR_ADC_START_ERROR;
    }
    return LDR_ADC_OK;
}

LdrAdcStatus LdrAdc_Stop(void)
{
    HAL_StatusTypeDef timer_status;
    HAL_StatusTypeDef adc_status;

    if (!initialized)
    {
        return LDR_ADC_BAD_STATE;
    }
    if (!running)
    {
        return LDR_ADC_OK;
    }
    timer_status = HAL_TIM_Base_Stop(&htim3);
    running = false;
    adc_status = HAL_ADC_Stop_DMA(&hadc1);
    if ((timer_status != HAL_OK) || (adc_status != HAL_OK))
    {
        return LDR_ADC_STOP_ERROR;
    }
    return LDR_ADC_OK;
}

static void QueueSamples(uint32_t offset)
{
    uint32_t i;
    uint32_t now_ms;

    if (!running || (hardware_errors != 0U))
    {
        return;
    }
    now_ms = HAL_GetTick();
    if ((uint32_t)(now_ms - last_dma_tick_ms) >= DMA_SAMPLE_COUNT)
    {
        /* A whole DMA buffer period passed without service. Do not pretend
         * the overwritten samples form a continuous optical waveform. */
        ++hardware_errors;
        return;
    }
    last_dma_tick_ms = now_ms;

    /* Single ISR producer, main-loop consumer. Work is bounded to 32 copies. */
    for (i = 0U; i < (DMA_SAMPLE_COUNT / 2U); ++i)
    {
        uint32_t write = queue_write;
        uint32_t next = (write + 1U) & QUEUE_MASK;

        ++sample_time_ms;
        if (next == queue_read)
        {
            ++dropped_samples;
            continue;
        }
        sample_queue[write].raw = dma_samples[offset + i];
        sample_queue[write].time_ms = sample_time_ms;
        __DMB();
        queue_write = next;
    }
}

bool LdrAdc_Read(LdrAdcSample *sample)
{
    uint32_t read;

    if (sample == NULL)
    {
        return false;
    }
    read = queue_read;
    if (read == queue_write)
    {
        return false;
    }
    __DMB();
    *sample = sample_queue[read];
    __DMB();
    queue_read = (read + 1U) & QUEUE_MASK;
    return true;
}

void LdrAdc_GetStats(LdrAdcStats *stats)
{
    if (stats != NULL)
    {
        /* Snapshot the IRQ-owned timestamp BEFORE reading the current tick.
         * Otherwise an IRQ between the two reads could make last > now and
         * turn unsigned subtraction into a spurious very large timeout. */
        uint32_t last_tick = last_dma_tick_ms;
        uint32_t now_ms = HAL_GetTick();

        stats->dropped_samples = dropped_samples;
        stats->hardware_errors = hardware_errors;
        stats->running = running;
        if (running &&
            ((uint32_t)(now_ms - last_tick) > (2U * DMA_SAMPLE_COUNT)))
        {
            /* No DMA callback at all: make a stopped trigger/ADC visible to
             * the main loop. Requires a normally advancing HAL millisecond tick. */
            ++stats->hardware_errors;
        }
    }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc1)
    {
        QueueSamples(0U);
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc1)
    {
        QueueSamples(DMA_SAMPLE_COUNT / 2U);
    }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if ((hadc == &hadc1) && running)
    {
        /* Main-loop receiver stops acquisition and reports the failure. */
        ++hardware_errors;
    }
}
