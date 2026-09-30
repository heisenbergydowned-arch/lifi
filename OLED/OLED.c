#include "OLED.h"

#include OLED_CUBEMX_GPIO_HEADER
#include "OLED_Font.h"

#include <stddef.h>

#define OLED_WIDTH_PIXELS               128U
#define OLED_PAGE_COUNT                 8U
#define OLED_TEXT_LINE_COUNT            4U
#define OLED_TEXT_COLUMN_COUNT          16U
#define OLED_COMMAND_CONTROL_BYTE       0x00U
#define OLED_DATA_CONTROL_BYTE          0x40U

static void OLED_I2C_Delay(void)
{
    volatile uint32_t cycle;

    for (cycle = 0U; cycle < OLED_SOFT_I2C_DELAY_CYCLES; ++cycle)
    {
        __NOP();
    }
}

static void OLED_WriteSCL(GPIO_PinState state)
{
    HAL_GPIO_WritePin(OLED_CUBEMX_SCL_PORT,
                      OLED_CUBEMX_SCL_PIN,
                      state);
}

static void OLED_WriteSDA(GPIO_PinState state)
{
    HAL_GPIO_WritePin(OLED_CUBEMX_SDA_PORT,
                      OLED_CUBEMX_SDA_PIN,
                      state);
}

static void OLED_I2C_Start(void)
{
    OLED_WriteSDA(GPIO_PIN_SET);
    OLED_WriteSCL(GPIO_PIN_SET);
    OLED_I2C_Delay();
    OLED_WriteSDA(GPIO_PIN_RESET);
    OLED_I2C_Delay();
    OLED_WriteSCL(GPIO_PIN_RESET);
    OLED_I2C_Delay();
}

static void OLED_I2C_Stop(void)
{
    OLED_WriteSDA(GPIO_PIN_RESET);
    OLED_I2C_Delay();
    OLED_WriteSCL(GPIO_PIN_SET);
    OLED_I2C_Delay();
    OLED_WriteSDA(GPIO_PIN_SET);
    OLED_I2C_Delay();
}

static void OLED_I2C_SendByte(uint8_t byte)
{
    uint8_t mask;

    for (mask = 0x80U; mask != 0U; mask >>= 1U)
    {
        OLED_WriteSDA((byte & mask) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);
        OLED_I2C_Delay();
        OLED_WriteSCL(GPIO_PIN_SET);
        OLED_I2C_Delay();
        OLED_WriteSCL(GPIO_PIN_RESET);
        OLED_I2C_Delay();
    }

    /* Release SDA during the ninth clock. The original driver ignores ACK. */
    OLED_WriteSDA(GPIO_PIN_SET);
    OLED_I2C_Delay();
    OLED_WriteSCL(GPIO_PIN_SET);
    OLED_I2C_Delay();
    OLED_WriteSCL(GPIO_PIN_RESET);
    OLED_I2C_Delay();
}

static void OLED_I2C_BeginWrite(uint8_t control_byte)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(OLED_I2C_WRITE_ADDRESS);
    OLED_I2C_SendByte(control_byte);
}

static void OLED_WriteBytes(uint8_t control_byte,
                            const uint8_t *data,
                            uint16_t length)
{
    uint16_t index;

    if ((data == NULL) || (length == 0U))
    {
        return;
    }

    OLED_I2C_BeginWrite(control_byte);
    for (index = 0U; index < length; ++index)
    {
        OLED_I2C_SendByte(data[index]);
    }
    OLED_I2C_Stop();
}

static void OLED_SetCursor(uint8_t page, uint8_t column)
{
    uint8_t commands[3];

    commands[0] = (uint8_t)(0xB0U | page);
    commands[1] = (uint8_t)(0x10U | ((column & 0xF0U) >> 4U));
    commands[2] = (uint8_t)(column & 0x0FU);
    OLED_WriteBytes(OLED_COMMAND_CONTROL_BYTE, commands, 3U);
}

static uint32_t OLED_Pow(uint32_t base, uint32_t exponent)
{
    uint32_t result = 1U;

    while (exponent-- != 0U)
    {
        result *= base;
    }
    return result;
}

static uint8_t OLED_TextPositionValid(uint8_t line, uint8_t column)
{
    return (uint8_t)(((line >= 1U) && (line <= OLED_TEXT_LINE_COUNT) &&
                      (column >= 1U) &&
                      (column <= OLED_TEXT_COLUMN_COUNT)) ? 1U : 0U);
}

void OLED_Clear(void)
{
    uint8_t page;
    uint8_t column;

    for (page = 0U; page < OLED_PAGE_COUNT; ++page)
    {
        OLED_SetCursor(page, 0U);
        OLED_I2C_BeginWrite(OLED_DATA_CONTROL_BYTE);
        for (column = 0U; column < OLED_WIDTH_PIXELS; ++column)
        {
            OLED_I2C_SendByte(0x00U);
        }
        OLED_I2C_Stop();
    }
}

void OLED_ShowChar(uint8_t line, uint8_t column, char character)
{
    uint8_t character_code;
    uint8_t font_index;
    uint8_t page;
    uint16_t font_offset;

    if (OLED_TextPositionValid(line, column) == 0U)
    {
        return;
    }

    character_code = (uint8_t)character;
    if ((character_code < (uint8_t)' ') ||
        (character_code > (uint8_t)'~'))
    {
        character_code = (uint8_t)'?';
    }

    font_index = (uint8_t)(character_code - (uint8_t)' ');
    font_offset = (uint16_t)font_index * 16U;
    page = (uint8_t)((line - 1U) * 2U);

    OLED_SetCursor(page, (uint8_t)((column - 1U) * 8U));
    OLED_WriteBytes(OLED_DATA_CONTROL_BYTE, &OLED_F8x16[font_offset], 8U);

    OLED_SetCursor((uint8_t)(page + 1U),
                   (uint8_t)((column - 1U) * 8U));
    OLED_WriteBytes(OLED_DATA_CONTROL_BYTE,
                    &OLED_F8x16[font_offset + 8U],
                    8U);
}

void OLED_ShowString(uint8_t line, uint8_t column, const char *string)
{
    if ((string == NULL) || (OLED_TextPositionValid(line, column) == 0U))
    {
        return;
    }

    while ((*string != '\0') && (column <= OLED_TEXT_COLUMN_COUNT))
    {
        OLED_ShowChar(line, column, *string);
        ++string;
        ++column;
    }
}

void OLED_ShowNum(uint8_t line,
                  uint8_t column,
                  uint32_t number,
                  uint8_t length)
{
    uint8_t index;

    if ((length == 0U) || (length > 10U))
    {
        return;
    }

    for (index = 0U; index < length; ++index)
    {
        OLED_ShowChar(line,
                      (uint8_t)(column + index),
                      (char)((number /
                              OLED_Pow(10U, (uint32_t)(length - index - 1U)) %
                              10U) + (uint32_t)'0'));
    }
}

void OLED_ShowSignedNum(uint8_t line,
                        uint8_t column,
                        int32_t number,
                        uint8_t length)
{
    uint8_t index;
    uint32_t magnitude;

    if ((length == 0U) || (length > 10U))
    {
        return;
    }

    if (number >= 0)
    {
        OLED_ShowChar(line, column, '+');
        magnitude = (uint32_t)number;
    }
    else
    {
        OLED_ShowChar(line, column, '-');
        magnitude = 0U - (uint32_t)number;
    }

    for (index = 0U; index < length; ++index)
    {
        OLED_ShowChar(line,
                      (uint8_t)(column + index + 1U),
                      (char)((magnitude /
                              OLED_Pow(10U, (uint32_t)(length - index - 1U)) %
                              10U) + (uint32_t)'0'));
    }
}

void OLED_ShowHexNum(uint8_t line,
                     uint8_t column,
                     uint32_t number,
                     uint8_t length)
{
    uint8_t index;
    uint8_t digit;

    if ((length == 0U) || (length > 8U))
    {
        return;
    }

    for (index = 0U; index < length; ++index)
    {
        digit = (uint8_t)(number /
                          OLED_Pow(16U, (uint32_t)(length - index - 1U)) %
                          16U);
        OLED_ShowChar(line,
                      (uint8_t)(column + index),
                      (char)(digit < 10U ?
                             (uint8_t)(digit + (uint8_t)'0') :
                             (uint8_t)(digit - 10U + (uint8_t)'A')));
    }
}

void OLED_ShowBinNum(uint8_t line,
                     uint8_t column,
                     uint32_t number,
                     uint8_t length)
{
    uint8_t index;

    if ((length == 0U) || (length > 16U))
    {
        return;
    }

    for (index = 0U; index < length; ++index)
    {
        OLED_ShowChar(line,
                      (uint8_t)(column + index),
                      (char)((number /
                              OLED_Pow(2U, (uint32_t)(length - index - 1U)) %
                              2U) + (uint32_t)'0'));
    }
}

void OLED_Init(void)
{
    static const uint8_t init_commands[] =
    {
        0xAEU,       /* Display off. */
        0xD5U, 0x80U,
        0xA8U, 0x3FU,
        0xD3U, 0x00U,
        0x40U,
        0xA1U,
        0xC8U,
        0xDAU, 0x12U,
        0x81U, 0xCFU,
        0xD9U, 0xF1U,
        0xDBU, 0x30U,
        0xA4U,
        0xA6U,
        0x8DU, 0x14U,
        0xAFU        /* Display on. */
    };

    /* MX_GPIO_Init() must already have configured both pins as open-drain. */
    OLED_WriteSCL(GPIO_PIN_SET);
    OLED_WriteSDA(GPIO_PIN_SET);
    HAL_Delay(100U);

    OLED_WriteBytes(OLED_COMMAND_CONTROL_BYTE,
                    init_commands,
                    (uint16_t)sizeof(init_commands));
    OLED_Clear();
}
