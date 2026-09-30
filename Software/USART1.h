#ifndef __USART1_USER_H
#define __USART1_USER_H

#ifdef __cplusplus
extern "C" {
#endif

/* 使用 CubeMX 自动生成的 huart1 和 DMA 句柄。 */
#include "usart.h"

HAL_StatusTypeDef USART1_SendByte(uint8_t data);
HAL_StatusTypeDef USART1_SendData(const uint8_t *data, uint16_t len);
HAL_StatusTypeDef USART1_SendString(const char *str);
HAL_StatusTypeDef USART1_SendDMA(const uint8_t *data, uint16_t len);
HAL_StatusTypeDef USART1_ReceiveDMA(uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __USART1_USER_H */
