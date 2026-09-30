#ifndef __MY_ADC_H
#define __MY_ADC_H

#include "adc.h"
#include "tim.h"
#include <stdio.h>
/* 双缓冲配置 */
#define ADC_BUF_SIZE        512    /* 每个缓冲区512个采样点 */
#define ADC_BUF_TOTAL       (ADC_BUF_SIZE * 2)  /* 总缓冲区大小 */

/* 默认采样率配置 */
#define ADC_DEFAULT_SAMPLE_RATE  10000  /* 默认10kHz采样率 */

/* 双缓冲区（放在D2 SRAM以支持DMA访问） */
extern uint16_t adc_buf[ADC_BUF_TOTAL];

/* 双缓冲状态标志 */
extern volatile uint8_t adc_half_flag;   /* 半传输完成标志 */
extern volatile uint8_t adc_full_flag;   /* 全传输完成标志 */

/* 函数声明 */
void My_ADC_Init(void);
void ADC_Start(void);
void ADC_Stop(void);
void ADC_SetSampleRate(uint32_t freq_hz);

#endif /* __MY_ADC_H */
