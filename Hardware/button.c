#include "button.h"
#include "main.h"
static uint32_t changed_ms;
static uint8_t candidate, stable;
static bool armed;
static uint8_t ReadKeys(void)
{
    uint8_t mask = 0U;
    if (HAL_GPIO_ReadPin(Low_Mode_GPIO_Port, Low_Mode_Pin) == GPIO_PIN_RESET) mask |= 1U;
    if (HAL_GPIO_ReadPin(Medium_Mode_GPIO_Port, Medium_Mode_Pin) == GPIO_PIN_RESET) mask |= 2U;
    if (HAL_GPIO_ReadPin(High_Mode_GPIO_Port, High_Mode_Pin) == GPIO_PIN_RESET) mask |= 4U;
    return mask;
}
void Button_Init(uint32_t now_ms)
{
    candidate = stable = ReadKeys();
    changed_ms = now_ms;
    armed = (stable == 0U);
}
bool Button_Poll(uint32_t now_ms, uint32_t *selected_t_ms)
{
    uint8_t sample = ReadKeys();
    if (selected_t_ms == 0) return false;
    if (sample != candidate) { candidate = sample; changed_ms = now_ms; }
    if ((uint32_t)(now_ms - changed_ms) < BUTTON_DEBOUNCE_MS || stable == candidate) return false;
    stable = candidate;
    if (stable == 0U) { armed = true; return false; }
    if (!armed) return false;
    armed = false;
    switch (stable)
    {
    case 1U: *selected_t_ms = BUTTON_SLOW_T_MS; return true;
    case 2U: *selected_t_ms = BUTTON_MEDIUM_T_MS; return true;
    case 4U: *selected_t_ms = BUTTON_FAST_T_MS; return true;
    default: return false;
    }
}
