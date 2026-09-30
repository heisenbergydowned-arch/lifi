#ifndef PROTOCOL_CONFIG_H
#define PROTOCOL_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The protocol four-tuple shared by all three ends (server, gateway, STM32).
 * Ranges and defaults come from MorseDecoder.h, the single source of truth.
 * t_ms = basic unit T (dot duration, 60..2000); dash_t = dash length in dot
 * units (2..5); word_gap_t = gap between words in dot units (2..8);
 * msg_gap_t = idle gap ending a message in dot units (4..20).
 * The consistency token is the first 16 hex chars of
 * sha256("t_ms:dash_t:word_gap_t:msg_gap_t"); every end computes it and
 * compares, so a mismatch anywhere is detectable without extra traffic. */
typedef struct
{
    uint32_t t_ms;
    uint32_t dash_t;
    uint32_t word_gap_t;
    uint32_t msg_gap_t;
} ProtocolConfig;

/* Fill *out with the factory defaults (60/3/4/10, backward compatible). */
void ProtocolConfig_Default(ProtocolConfig *out);

/* Parse "t_ms|dash_t|word_gap_t|msg_gap_t" (four pipe-separated unsigned
 * decimal fields, no leading '+' or whitespace, no trailing junk). Any
 * malformed field, count mismatch or out-of-range value rejects the WHOLE
 * string: *out is left untouched and false is returned. */
bool ProtocolConfig_Parse(const char *text, ProtocolConfig *out);

/* Serialize to "t_ms|dash_t|word_gap_t|msg_gap_t" (no C1 prefix, no NUL
 * padding concerns: returns false when size is too small, including the
 * terminating NUL; success writes the string plus NUL). */
bool ProtocolConfig_ToText(const ProtocolConfig *cfg, char *text, unsigned size);

/* Write the 16-lowercase-hex consistency token plus NUL into hex16
 * (17 bytes needed). sha256 over "t:d:wg:mg" colon form, first 8 bytes. */
bool ProtocolConfig_ComputeToken(const ProtocolConfig *cfg, char *hex16);

#ifdef __cplusplus
}
#endif
#endif
