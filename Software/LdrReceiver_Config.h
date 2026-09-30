#ifndef LDR_RECEIVER_CONFIG_H
#define LDR_RECEIVER_CONFIG_H

/* Must match the phone transmitter's basic time T (dot duration), in ms. */
#ifndef LDR_MORSE_DOT_MS
#define LDR_MORSE_DOT_MS          60U
#endif

/* Measured raw ADC: ambient about 530, illuminated about 430.
 * Bright light lowers ADC. Divider example: 3.3V -- fixed R -- PA0 -- LDR -- GND. */
#ifndef LDR_BRIGHT_IS_HIGH
#define LDR_BRIGHT_IS_HIGH       0
#endif
#define LDR_FIXED_RESISTOR_OHM   10000U
#define LDR_ADC_REFERENCE_MV     3300U

/* Keep the phone torch OFF during this initial ambient-light calibration. */
#define LDR_CALIBRATION_MS       1000U

/* Normalized ADC counts relative to the calibrated ambient level, not ohms.
 * Initial tuning for the reported 100-count light/dark difference.
 * With bright-is-low and ambient raw=530: ON<=470, OFF>=500.
 * ON must be greater than OFF; readings between them keep the current state. */
#define LDR_ON_DELTA_COUNTS      60U
#define LDR_OFF_DELTA_COUNTS     30U
#define LDR_EDGE_STABLE_MS       3U

/* Raw ADC snapshot every 20 ms, full voltage/resistance diagnostics every 1 s.
 * Set to 0 to suppress ADC diagnostics; decode and fault messages remain.
 * These periods limit UART output, not the 1 kHz ADC acquisition rate. */
#ifndef LDR_DEBUG_ADC
#define LDR_DEBUG_ADC            0 /* Preserve the user's disabled continuous ADC logs. */
#endif
#ifndef LDR_DEBUG_RAW_PERIOD_MS
#define LDR_DEBUG_RAW_PERIOD_MS  20U
#endif
#define LDR_DEBUG_PERIOD_MS      1000U

#if (LDR_DEBUG_RAW_PERIOD_MS < 10U) || (LDR_DEBUG_RAW_PERIOD_MS > 60000U)
#error "LDR_DEBUG_RAW_PERIOD_MS must be 10..60000 to limit UART traffic"
#endif
#if (LDR_MORSE_DOT_MS < 60U) || (LDR_MORSE_DOT_MS > 2000U)
#error "LDR_MORSE_DOT_MS must be 60..2000 and match the transmitter"
#endif
#if (LDR_ON_DELTA_COUNTS <= LDR_OFF_DELTA_COUNTS) || (LDR_OFF_DELTA_COUNTS == 0U) || (LDR_ON_DELTA_COUNTS >= 4095U)
#error "Invalid LDR threshold offsets"
#endif
#if (LDR_BRIGHT_IS_HIGH != 0) && (LDR_BRIGHT_IS_HIGH != 1)
#error "LDR_BRIGHT_IS_HIGH must be 0 or 1"
#endif
#if (LDR_FIXED_RESISTOR_OHM == 0U) || (LDR_FIXED_RESISTOR_OHM > 1000000U)
#error "LDR fixed resistor must be 1..1000000 ohms"
#endif
#if (LDR_CALIBRATION_MS < 20U) || (LDR_CALIBRATION_MS > 10000U)
#error "LDR calibration duration must be 20..10000 ms"
#endif

#endif
