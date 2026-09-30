#ifndef OLED_H
#define OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
 * CubeMX/HAL adapter.
 *
 * Recommended CubeMX user labels:
 *   PB8 -> OLED_SCL
 *   PB9 -> OLED_SDA
 *
 * CubeMX then generates OLED_SCL_GPIO_Port/OLED_SCL_Pin and
 * OLED_SDA_GPIO_Port/OLED_SDA_Pin in main.h. If another generated header or
 * other labels are used, override the four OLED_CUBEMX_* macros below.
 */
#ifndef OLED_CUBEMX_GPIO_HEADER
#define OLED_CUBEMX_GPIO_HEADER          "main.h"
#endif

#ifndef OLED_CUBEMX_SCL_PORT
#define OLED_CUBEMX_SCL_PORT             OLED_SCL_GPIO_Port
#endif

#ifndef OLED_CUBEMX_SCL_PIN
#define OLED_CUBEMX_SCL_PIN              OLED_SCL_Pin
#endif

#ifndef OLED_CUBEMX_SDA_PORT
#define OLED_CUBEMX_SDA_PORT             OLED_SDA_GPIO_Port
#endif

#ifndef OLED_CUBEMX_SDA_PIN
#define OLED_CUBEMX_SDA_PIN              OLED_SDA_Pin
#endif

/* SSD1306 7-bit address 0x3C expressed as the transmitted write byte. */
#ifndef OLED_I2C_WRITE_ADDRESS
#define OLED_I2C_WRITE_ADDRESS           0x78U
#endif

/* Software-I2C half-cycle padding. 32 is a conservative STM32F103 default. */
#ifndef OLED_SOFT_I2C_DELAY_CYCLES
#define OLED_SOFT_I2C_DELAY_CYCLES       32U
#endif

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, const char *String);
void OLED_ShowNum(uint8_t Line,
                  uint8_t Column,
                  uint32_t Number,
                  uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line,
                        uint8_t Column,
                        int32_t Number,
                        uint8_t Length);
void OLED_ShowHexNum(uint8_t Line,
                     uint8_t Column,
                     uint32_t Number,
                     uint8_t Length);
void OLED_ShowBinNum(uint8_t Line,
                     uint8_t Column,
                     uint32_t Number,
                     uint8_t Length);

#ifdef __cplusplus
}
#endif

#endif /* OLED_H */
