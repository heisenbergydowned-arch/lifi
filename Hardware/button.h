#ifndef BUTTON_H
#define BUTTON_H
#include <stdbool.h>
#include <stdint.h>
#define BUTTON_DEBOUNCE_MS 20U
#define BUTTON_SLOW_T_MS 240U
#define BUTTON_MEDIUM_T_MS 120U
#define BUTTON_FAST_T_MS 60U
/* PA3/PA4/PA5: switches to GND, CubeMX pull-ups. Held startup keys and
 * multi-key chords must be released before another press is accepted. */
void Button_Init(uint32_t now_ms);
bool Button_Poll(uint32_t now_ms, uint32_t *selected_t_ms);
#endif
