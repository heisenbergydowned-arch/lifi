#ifndef IOT_TEST_MAIN_H
#define IOT_TEST_MAIN_H
#include <stdint.h>
typedef struct { unsigned number; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;
typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2, HAL_TIMEOUT = 3 } HAL_StatusTypeDef;
extern GPIO_TypeDef port_a, port_b;
#define Low_Mode_GPIO_Port (&port_a)
#define Medium_Mode_GPIO_Port (&port_a)
#define High_Mode_GPIO_Port (&port_a)
#define Low_Mode_Pin (1U << 3U)
#define Medium_Mode_Pin (1U << 4U)
#define High_Mode_Pin (1U << 5U)
#define RTC_CE_GPIO_Port (&port_b)
#define RTC_SCLK_GPIO_Port (&port_b)
#define RTC_IO_GPIO_Port (&port_b)
#define BUZZER_GPIO_Port (&port_b)
#define RTC_CE_Pin (1U << 0U)
#define RTC_SCLK_Pin (1U << 1U)
#define RTC_IO_Pin (1U << 5U)
#define BUZZER_Pin (1U << 6U)
#define __NOP() ((void)0)
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState value);
uint32_t HAL_GetTick(void);
#endif
