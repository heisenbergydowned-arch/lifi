#ifndef RECEIVER_DISPLAY_H
#define RECEIVER_DISPLAY_H

#include <stdint.h>

/* 128x64 OLED, existing 8x16 ASCII font: 4 rows x 16 columns.
 * Call Init after GPIO/receiver/message/network initialization, BEFORE ADC
 * sampling starts. Init includes the OLED power-up delay and one clear.
 * Process is main-loop only: one changed character at most per call, with
 * interrupts enabled. This module owns all OLED drawing after Init; do not
 * call OLED drawing APIs from another module at the same time. */
void ReceiverDisplay_Init(uint32_t now_ms);
void ReceiverDisplay_Process(uint32_t now_ms);

#endif
