#ifndef LDR_ADC_H
#define LDR_ADC_H

#include <stdbool.h>
#include <stdint.h>

/* Matches CubeMX TIM3: 72 MHz / (71 + 1) / (999 + 1).
 * This is the acquisition rate, not the Morse dot duration in ms. */
#define LDR_ADC_SAMPLE_RATE_HZ 1000U

typedef enum
{
    LDR_ADC_OK = 0,
    LDR_ADC_BAD_STATE,
    LDR_ADC_CALIBRATION_ERROR,
    LDR_ADC_START_ERROR,
    LDR_ADC_STOP_ERROR
} LdrAdcStatus;

typedef struct
{
    uint32_t time_ms; /* Acquisition time, independent of printf/main-loop delay. */
    uint16_t raw;
} LdrAdcSample;

typedef struct
{
    uint32_t dropped_samples;
    uint32_t hardware_errors;
    bool running;
} LdrAdcStats;

/* Call only after CubeMX initialized ADC1, DMA and TIM3. No MX calls here.
 * Init while stopped clears the queue and repeats calibration; while running
 * it returns BAD_STATE. Start refuses a second start. Stop is idempotent.
 */
LdrAdcStatus LdrAdc_Init(void);
LdrAdcStatus LdrAdc_Start(void);
LdrAdcStatus LdrAdc_Stop(void);
bool LdrAdc_Read(LdrAdcSample *sample);
void LdrAdc_GetStats(LdrAdcStats *stats);

#endif
