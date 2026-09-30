#include "USART1.h"

#include <stdio.h>
#include <string.h>

#if defined(__CC_ARM) && !defined(__MICROLIB)
#include <rt_sys.h>
#endif

#define USART1_PRINTF_TIMEOUT_MS  1000U

static HAL_StatusTypeDef USART1_Write(const uint8_t *data, uint32_t length)
{
    HAL_StatusTypeDef status;

    if ((data == NULL) || (length == 0U))
    {
        return HAL_OK;
    }

    while (length > 0U)
    {
        uint16_t chunk = (length > 0xFFFFU) ? 0xFFFFU : (uint16_t)length;

        status = HAL_UART_Transmit(&huart1, (uint8_t *)data, chunk,
                                   USART1_PRINTF_TIMEOUT_MS);
        if (status != HAL_OK)
        {
            return status;
        }

        data += chunk;
        length -= chunk;
    }

    return HAL_OK;
}

HAL_StatusTypeDef USART1_SendByte(uint8_t data)
{
    return USART1_Write(&data, 1U);
}

HAL_StatusTypeDef USART1_SendData(const uint8_t *data, uint16_t len)
{
    return USART1_Write(data, len);
}

HAL_StatusTypeDef USART1_SendString(const char *str)
{
    if (str == NULL)
    {
        return HAL_ERROR;
    }

    return USART1_Write((const uint8_t *)str, (uint32_t)strlen(str));
}

HAL_StatusTypeDef USART1_SendDMA(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_UART_Transmit_DMA(&huart1, (uint8_t *)data, len);
}

HAL_StatusTypeDef USART1_ReceiveDMA(uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_UART_Receive_DMA(&huart1, data, len);
}

/* Keil C library retargeting. stdout is USART1 (PA9 TX). */
#if defined(__CC_ARM)
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
};

FILE __stdout;
FILE __stdin;
#endif

int fputc(int ch, FILE *stream)
{
    uint8_t data = (uint8_t)ch;

    (void)stream;
    return (USART1_Write(&data, 1U) == HAL_OK) ? ch : EOF;
}

void _ttywrch(int ch)
{
    (void)fputc(ch, stdout);
}

void _sys_exit(int return_code)
{
    (void)return_code;
    while (1)
    {
    }
}

#if defined(__CC_ARM) && !defined(__MICROLIB)
FILEHANDLE _sys_open(const char *name, int openmode)
{
    (void)name;
    (void)openmode;
    return 1;
}

int _sys_close(FILEHANDLE fh)
{
    (void)fh;
    return 0;
}

int _sys_write(FILEHANDLE fh, const unsigned char *buffer, unsigned length, int mode)
{
    (void)fh;
    (void)mode;
    return (USART1_Write(buffer, length) == HAL_OK) ? 0 : (int)length;
}

int _sys_read(FILEHANDLE fh, unsigned char *buffer, unsigned length, int mode)
{
    (void)fh;
    (void)buffer;
    (void)mode;
    return (int)length;
}

int _sys_istty(FILEHANDLE fh)
{
    (void)fh;
    return 1;
}

int _sys_seek(FILEHANDLE fh, long pos)
{
    (void)fh;
    (void)pos;
    return -1;
}

long _sys_flen(FILEHANDLE fh)
{
    (void)fh;
    return -1L;
}
#endif

#if defined(__GNUC__) && !defined(__CC_ARM)
int _write(int file, char *buffer, int length)
{
    (void)file;
    return (USART1_Write((const uint8_t *)buffer, (uint32_t)length) == HAL_OK) ? length : -1;
}
#endif
