#ifndef TEST_ADC_H
#define TEST_ADC_H

#include <stdint.h>

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

typedef struct { uint32_t marker; } ADC_TypeDef;
typedef struct { ADC_TypeDef *Instance; } ADC_HandleTypeDef;

extern ADC_TypeDef mock_adc1;
extern ADC_HandleTypeDef hadc1;
#define ADC1 (&mock_adc1)

/* Host tests exercise single-threaded callback/consumer ordering. */
#define __DMB() ((void)0)

uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *hadc,
                                  uint32_t *destination, uint32_t length);
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *hadc);
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc);
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc);
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc);

#endif
