#ifndef __HMI_H
#define __HMI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usart.h"

/* ================================================================
 * 淘晶驰串口屏协议配置
 * USART2 的波特率、GPIO 和 DMA 由 CubeMX 生成代码负责初始化
 * ================================================================ */
#define HMI_RX_BUF_SIZE       64U
#define HMI_DMA_RX_BUF_SIZE   64U
#define HMI_TX_BUF_SIZE       128U
#define HMI_TX_TIMEOUT_MS     100U

#define HMI_RX_COMPLETE_FLAG  0x8000U
#define HMI_RX_LENGTH_MASK    0x7FFFU
#define HMI_END               0xFFU

/* 淘晶驰协议标准返回码 */
#define HMI_RET_CMD_OK        0x01U
#define HMI_RET_INVALID_CMD   0x02U
#define HMI_RET_INVALID_CMP   0x03U
#define HMI_RET_INVALID_VAL   0x04U
#define HMI_RET_INVALID_PAGE  0x05U
#define HMI_RET_STR_DATA      0x70U
#define HMI_RET_NUM_DATA      0x71U
#define HMI_RET_TOUCH_EVENT   0x65U
#define HMI_RET_PAGE_EVENT    0x66U
#define HMI_RET_READY         0x88U
#define HMI_RET_UPGRADE       0x89U

typedef struct
{
    uint8_t page_id;
    uint8_t comp_id;
    uint8_t event;
} HMI_TouchEvent;

/* bit15：接收完成；bit14~0：有效数据长度，不包含 FF FF FF 帧尾 */
extern uint8_t HMI_RX_BUF[HMI_RX_BUF_SIZE];
extern volatile uint16_t HMI_RX_STA;

HAL_StatusTypeDef HMI_Init(void);

HAL_StatusTypeDef HMI_SendByte(uint8_t data);
HAL_StatusTypeDef HMI_SendStr(const char *str);
HAL_StatusTypeDef HMI_SendCmd(const char *cmd);

HAL_StatusTypeDef HMI_SetPage(uint8_t page_id);
HAL_StatusTypeDef HMI_SetBrightness(uint8_t brightness);
HAL_StatusTypeDef HMI_SetText(const char *comp, const char *text);
HAL_StatusTypeDef HMI_SetVal(const char *comp, uint32_t val);
HAL_StatusTypeDef HMI_SetBco(const char *comp, uint16_t color);
HAL_StatusTypeDef HMI_SetPco(const char *comp, uint16_t color);
HAL_StatusTypeDef HMI_SetVis(const char *comp, uint8_t visible);
HAL_StatusTypeDef HMI_GetVal(const char *comp);
HAL_StatusTypeDef HMI_GetText(const char *comp);
HAL_StatusTypeDef HMI_Reset(void);
HAL_StatusTypeDef HMI_Refresh(void);

uint8_t HMI_IsFrameReady(void);
uint16_t HMI_GetRxLength(void);
void HMI_ClearRxFrame(void);
uint8_t HMI_Parse(const uint8_t *buf, uint16_t len, HMI_TouchEvent *evt);

#ifdef __cplusplus
}
#endif

#endif /* __HMI_H */
