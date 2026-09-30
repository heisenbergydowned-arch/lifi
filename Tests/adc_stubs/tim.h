#ifndef TEST_TIM_H
#define TEST_TIM_H

#include "adc.h"

typedef struct { uint32_t marker; } TIM_TypeDef;
typedef struct { TIM_TypeDef *Instance; } TIM_HandleTypeDef;

extern TIM_TypeDef mock_tim3;
extern TIM_HandleTypeDef htim3;
#define TIM3 (&mock_tim3)
#define TIM_FLAG_UPDATE 1U

void Mock_SetCounter(TIM_HandleTypeDef *timer, uint32_t counter);
void Mock_ClearFlag(TIM_HandleTypeDef *timer, uint32_t flag);
#define __HAL_TIM_SET_COUNTER(timer, counter) Mock_SetCounter((timer), (counter))
#define __HAL_TIM_CLEAR_FLAG(timer, flag) Mock_ClearFlag((timer), (flag))

HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *timer);
HAL_StatusTypeDef HAL_TIM_Base_Stop(TIM_HandleTypeDef *timer);

#endif
