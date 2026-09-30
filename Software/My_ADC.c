#include "My_ADC.h"


/* 双缓冲区（放在D2 SRAM以支持DMA访问） */
uint16_t adc_buf[ADC_BUF_TOTAL];

/* 双缓冲状态标志 */
volatile uint8_t adc_half_flag = 0;
volatile uint8_t adc_full_flag = 0;

/* DMA半传输完成回调 */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc)
{
    if (hadc->Instance == ADC1) {
        adc_half_flag = 1;
    }
}

/* DMA全传输完成回调 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if (hadc->Instance == ADC1) {
        adc_full_flag = 1;
    }
}

void My_ADC_Init(void)
{
    ADC_SetSampleRate(ADC_DEFAULT_SAMPLE_RATE);

    if (HAL_ADCEx_Calibration_Start(&hadc1,
                                    ADC_CALIB_OFFSET,
                                    ADC_SINGLE_ENDED) != HAL_OK)
    {
        printf("ADC calibration failed\r\n");
        return;
    }

    printf("ADC initialized: block=%u, Fs=%lu Hz\r\n",
           (unsigned int)ADC_BUF_SIZE,
           (unsigned long)ADC_DEFAULT_SAMPLE_RATE);
}

void ADC_Start(void)
{
    HAL_StatusTypeDef status;

    adc_half_flag = 0;
    adc_full_flag = 0;

    /* 启动TIM2 */
    HAL_TIM_Base_Start(&htim2);

    /* 启动ADC DMA */
    status = HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buf, ADC_BUF_TOTAL);

    if (status != HAL_OK) {
        printf("ADC Start Error: %d\r\n", status);
    } else {
        printf("ADC Started OK\r\n");
    }
}

void ADC_Stop(void)
{
    /* 停止ADC DMA */
    HAL_ADC_Stop_DMA(&hadc1);

    /* 停止TIM2 */
    HAL_TIM_Base_Stop(&htim2);

    adc_half_flag = 0;
    adc_full_flag = 0;

    printf("ADC Stopped\r\n");
}

void ADC_SetSampleRate(uint32_t freq_hz)
{
    /* TIM2时钟 = PCLK1 * 2 (APB1 timer clock) */
    uint32_t tim_clk = HAL_RCC_GetPCLK1Freq() * 2;
    uint32_t period = tim_clk / freq_hz - 1;

    /* 设置TIM2周期 */
    __HAL_TIM_SET_AUTORELOAD(&htim2, period);
    __HAL_TIM_SET_COUNTER(&htim2, 0);

    printf("ADC Sample Rate: %d Hz (TIM2 Period: %lu)\r\n", freq_hz, period);
}
