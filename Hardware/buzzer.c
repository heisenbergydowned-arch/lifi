#include "buzzer.h"
#include "main.h"

static bool active;
static uint32_t started_ms;
static void SetOutput(bool on)
{
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin,
                      (on == (BUZZER_ACTIVE_HIGH != 0)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
void Buzzer_Stop(void) { active = false; SetOutput(false); }
void Buzzer_Init(void) { started_ms = 0U; Buzzer_Stop(); }
void Buzzer_StartAlarm(uint32_t now_ms)
{
    /* A new completed distress message restarts the 60-second interval. */
    started_ms = now_ms; active = true; SetOutput(true);
}
void Buzzer_Process(uint32_t now_ms)
{
    if (active && (uint32_t)(now_ms - started_ms) >= BUZZER_ALARM_MS) Buzzer_Stop();
}
bool Buzzer_IsActive(void) { return active; }
