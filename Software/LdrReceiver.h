#ifndef LDR_RECEIVER_H
#define LDR_RECEIVER_H

#include "MorseDecoder.h"
/* NULL event means stream reset. Main context; time uses ADC sample clock. */
typedef void (*LdrReceiver_EventHandler)(const MorseDecoderEvent *event, uint32_t time_ms);
void LdrReceiver_SetEventHandler(LdrReceiver_EventHandler handler);
bool LdrReceiver_SetDotTimeMs(uint32_t dot_ms);
/* Applies the ProtocolConfig four-tuple from a C1 frame. All four values
 * must lie in the MorseDecoder ranges; any invalid value keeps the old
 * configuration and returns false (whole frame rejected, no partial apply).
 * All-equal to the current values is a no-op and returns true (idempotent).
 * RAM-only: power loss returns to LDR_MORSE_DOT_MS plus default gaps. */
bool LdrReceiver_SetProtocolParams(uint32_t dot_ms, uint32_t dash_t,
                                   uint32_t word_gap_t, uint32_t msg_gap_t);
uint32_t LdrReceiver_GetDotTimeMs(void);
typedef struct
{
    uint32_t dot_ms, sample_ms;
    uint32_t dash_t, word_gap_t, msg_gap_t;
    uint16_t raw, normalized;
    bool have_sample, calibrated, thresholds_valid, synchronized, light, failed;
} LdrReceiver_Status;
/* Latest consumed ADC sample; raw is before median filtering/polarity change.
 * Read in main context. No ADC conversions or peripheral writes here. */
void LdrReceiver_GetStatus(LdrReceiver_Status *status);

/* No peripheral initialization here. Call after LdrAdc_Init, before Start. */
void LdrReceiver_Init(void);
/* Consume DMA samples and print results. Main-loop only; call frequently. */
void LdrReceiver_Process(void);

#endif
