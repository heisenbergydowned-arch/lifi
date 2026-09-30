#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>
#include <stdint.h>
#ifndef BUZZER_ACTIVE_HIGH
#define BUZZER_ACTIVE_HIGH 1
#endif
#define BUZZER_ALARM_MS 60000U
/* PB6 drives an ACTIVE buzzer module input/transistor, not a bare load.
 * Passive buzzers require a separate tone driver. */
void Buzzer_Init(void);
void Buzzer_StartAlarm(uint32_t now_ms);
void Buzzer_Stop(void);
void Buzzer_Process(uint32_t now_ms);
bool Buzzer_IsActive(void);




#endif /* BUZZER_H */
