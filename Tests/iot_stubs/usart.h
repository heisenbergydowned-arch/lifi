#ifndef IOT_TEST_USART_H
#define IOT_TEST_USART_H
#include "main.h"
typedef struct { unsigned number; } UART_HandleTypeDef;
extern UART_HandleTypeDef huart3, other_uart;
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart);
#endif
