#include "HMI.h"

#include "usart.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ================================================================
 * 全局接收状态：保持原标准库工程的使用方式
 * ================================================================ */
uint8_t HMI_RX_BUF[HMI_RX_BUF_SIZE];
volatile uint16_t HMI_RX_STA = 0U;

/* DMA 缓冲区按 Cortex-M7 D-Cache 行大小对齐。 */
static uint8_t hmi_dma_rx_buf[HMI_DMA_RX_BUF_SIZE] __ALIGNED(32);
static uint8_t hmi_dma_tx_buf[HMI_TX_BUF_SIZE] __ALIGNED(32);

static volatile uint8_t hmi_tx_busy = 0U;
static uint8_t hmi_ff_count = 0U;

/* ================================================================
 * DMA 与 Cache 辅助函数
 * 当前 CubeMX 工程未开启 D-Cache；这里同时兼容以后开启 D-Cache 的情况。
 * ================================================================ */
static void HMI_InvalidateRxCache(void)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
    {
        SCB_InvalidateDCache_by_Addr((uint32_t *)hmi_dma_rx_buf,
                                    (int32_t)sizeof(hmi_dma_rx_buf));
    }
}

static void HMI_CleanTxCache(void)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
    {
        SCB_CleanDCache_by_Addr((uint32_t *)hmi_dma_tx_buf,
                               (int32_t)sizeof(hmi_dma_tx_buf));
    }
}

static HAL_StatusTypeDef HMI_StartReceiveDMA(void)
{
    HAL_StatusTypeDef status;

    HMI_InvalidateRxCache();
    status = HAL_UARTEx_ReceiveToIdle_DMA(&huart2,
                                         hmi_dma_rx_buf,
                                         HMI_DMA_RX_BUF_SIZE);
    if (status == HAL_OK)
    {
        /* HMI 按空闲帧处理，不需要半传输回调。 */
        __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
    }

    return status;
}

static HAL_StatusTypeDef HMI_WaitTxReady(void)
{
    uint32_t tick_start = HAL_GetTick();

    while (hmi_tx_busy != 0U)
    {
        if ((HAL_GetTick() - tick_start) >= HMI_TX_TIMEOUT_MS)
        {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

static HAL_StatusTypeDef HMI_TransmitDMA(const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef status;

    if ((data == NULL) || (len == 0U) || (len > HMI_TX_BUF_SIZE))
    {
        return HAL_ERROR;
    }

    status = HMI_WaitTxReady();
    if (status != HAL_OK)
    {
        return status;
    }

    memcpy(hmi_dma_tx_buf, data, len);
    HMI_CleanTxCache();

    hmi_tx_busy = 1U;
    status = HAL_UART_Transmit_DMA(&huart2, hmi_dma_tx_buf, len);
    if (status != HAL_OK)
    {
        hmi_tx_busy = 0U;
    }

    return status;
}

static HAL_StatusTypeDef HMI_SendFormatted(const char *format, ...)
{
    char cmd[HMI_TX_BUF_SIZE - 3U];
    int len;
    va_list args;

    if (format == NULL)
    {
        return HAL_ERROR;
    }

    va_start(args, format);
    len = vsnprintf(cmd, sizeof(cmd), format, args);
    va_end(args);

    if ((len < 0) || ((uint32_t)len >= sizeof(cmd)))
    {
        return HAL_ERROR;
    }

    return HMI_SendCmd(cmd);
}

static uint8_t HMI_AppendRxByte(uint8_t data)
{
    uint16_t len = HMI_RX_STA & HMI_RX_LENGTH_MASK;

    if (len >= HMI_RX_BUF_SIZE)
    {
        HMI_RX_STA = 0U;
        hmi_ff_count = 0U;
        return 0U;
    }

    HMI_RX_BUF[len] = data;
    HMI_RX_STA = len + 1U;
    return 1U;
}

static void HMI_ConsumeRxData(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for (i = 0U; i < len; i++)
    {
        uint8_t byte = data[i];

        /* 保留上一帧，等待主循环处理并调用 HMI_ClearRxFrame()。 */
        if ((HMI_RX_STA & HMI_RX_COMPLETE_FLAG) != 0U)
        {
            return;
        }

        if (byte == HMI_END)
        {
            hmi_ff_count++;
            if (hmi_ff_count >= 3U)
            {
                HMI_RX_STA |= HMI_RX_COMPLETE_FLAG;
                hmi_ff_count = 0U;
            }
            continue;
        }

        /* 单个或两个 0xFF 不是完整帧尾时，作为有效数据写回。 */
        while (hmi_ff_count > 0U)
        {
            if (HMI_AppendRxByte(HMI_END) == 0U)
            {
                return;
            }
            hmi_ff_count--;
        }

        if (HMI_AppendRxByte(byte) == 0U)
        {
            return;
        }
    }
}

/* ================================================================
 * 公共 API
 * ================================================================ */
HAL_StatusTypeDef HMI_Init(void)
{
    HAL_StatusTypeDef status;

    HMI_ClearRxFrame();
    hmi_tx_busy = 0U;

    status = HMI_StartReceiveDMA();
    if (status != HAL_OK)
    {
        return status;
    }

    HAL_Delay(100U);
    status = HMI_Reset();
    if (status != HAL_OK)
    {
        return status;
    }
    HAL_Delay(100U);

    return HAL_OK;
}

HAL_StatusTypeDef HMI_SendByte(uint8_t data)
{
    return HMI_TransmitDMA(&data, 1U);
}

HAL_StatusTypeDef HMI_SendStr(const char *str)
{
    size_t len;

    if (str == NULL)
    {
        return HAL_ERROR;
    }

    len = strlen(str);
    if ((len == 0U) || (len > HMI_TX_BUF_SIZE))
    {
        return HAL_ERROR;
    }

    return HMI_TransmitDMA((const uint8_t *)str, (uint16_t)len);
}

HAL_StatusTypeDef HMI_SendCmd(const char *cmd)
{
    uint8_t frame[HMI_TX_BUF_SIZE];
    size_t cmd_len;

    if (cmd == NULL)
    {
        return HAL_ERROR;
    }

    cmd_len = strlen(cmd);
    if ((cmd_len == 0U) || (cmd_len > (HMI_TX_BUF_SIZE - 3U)))
    {
        return HAL_ERROR;
    }

    memcpy(frame, cmd, cmd_len);
    frame[cmd_len] = HMI_END;
    frame[cmd_len + 1U] = HMI_END;
    frame[cmd_len + 2U] = HMI_END;

    return HMI_TransmitDMA(frame, (uint16_t)(cmd_len + 3U));
}

HAL_StatusTypeDef HMI_Reset(void)
{
    return HMI_SendCmd("rest");
}

HAL_StatusTypeDef HMI_Refresh(void)
{
    return HMI_SendCmd("ref 0");
}

HAL_StatusTypeDef HMI_SetPage(uint8_t page_id)
{
    return HMI_SendFormatted("page %u", (unsigned int)page_id);
}

HAL_StatusTypeDef HMI_SetBrightness(uint8_t brightness)
{
    if (brightness > 100U)
    {
        brightness = 100U;
    }
    return HMI_SendFormatted("dim=%u", (unsigned int)brightness);
}

HAL_StatusTypeDef HMI_SetText(const char *comp, const char *text)
{
    if ((comp == NULL) || (text == NULL))
    {
        return HAL_ERROR;
    }
    return HMI_SendFormatted("%s.txt=\"%s\"", comp, text);
}

HAL_StatusTypeDef HMI_SetVal(const char *comp, uint32_t val)
{
    if (comp == NULL)
    {
        return HAL_ERROR;
    }
    return HMI_SendFormatted("%s.val=%lu", comp, (unsigned long)val);
}

HAL_StatusTypeDef HMI_SetBco(const char *comp, uint16_t color)
{
    if (comp == NULL)
    {
        return HAL_ERROR;
    }
    return HMI_SendFormatted("%s.bco=%u", comp, (unsigned int)color);
}

HAL_StatusTypeDef HMI_SetPco(const char *comp, uint16_t color)
{
    if (comp == NULL)
    {
        return HAL_ERROR;
    }
    return HMI_SendFormatted("%s.pco=%u", comp, (unsigned int)color);
}

HAL_StatusTypeDef HMI_SetVis(const char *comp, uint8_t visible)
{
    if (comp == NULL)
    {
        return HAL_ERROR;
    }
    return HMI_SendFormatted("vis %s,%u", comp, (visible != 0U) ? 1U : 0U);
}

HAL_StatusTypeDef HMI_GetVal(const char *comp)
{
    if (comp == NULL)
    {
        return HAL_ERROR;
    }
    return HMI_SendFormatted("get %s.val", comp);
}

HAL_StatusTypeDef HMI_GetText(const char *comp)
{
    if (comp == NULL)
    {
        return HAL_ERROR;
    }
    return HMI_SendFormatted("get %s.txt", comp);
}

uint8_t HMI_IsFrameReady(void)
{
    return ((HMI_RX_STA & HMI_RX_COMPLETE_FLAG) != 0U) ? 1U : 0U;
}

uint16_t HMI_GetRxLength(void)
{
    return HMI_RX_STA & HMI_RX_LENGTH_MASK;
}

void HMI_ClearRxFrame(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    HMI_RX_STA = 0U;
    hmi_ff_count = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

uint8_t HMI_Parse(const uint8_t *buf, uint16_t len, HMI_TouchEvent *evt)
{
    if ((buf == NULL) || (len == 0U))
    {
        return 0U;
    }

    switch (buf[0])
    {
        case HMI_RET_TOUCH_EVENT:
            if ((len >= 4U) && (evt != NULL))
            {
                evt->page_id = buf[1];
                evt->comp_id = buf[2];
                evt->event = buf[3];
                return 1U;
            }
            break;

        case HMI_RET_NUM_DATA:
        case HMI_RET_STR_DATA:
        case HMI_RET_READY:
        default:
            break;
    }

    return 0U;
}

/* ================================================================
 * HAL 回调：USART2 由 HMI 模块独占
 * ================================================================ */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance != USART2)
    {
        return;
    }

    HMI_InvalidateRxCache();
    HMI_ConsumeRxData(hmi_dma_rx_buf, size);
    (void)HMI_StartReceiveDMA();
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        hmi_tx_busy = 0U;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2)
    {
        return;
    }

    if (huart->gState == HAL_UART_STATE_READY)
    {
        hmi_tx_busy = 0U;
    }
    (void)HMI_StartReceiveDMA();
}
