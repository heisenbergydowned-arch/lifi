#ifndef MORSE_DECODER_H
#define MORSE_DECODER_H

#include <stdbool.h>
#include <stdint.h>

#define MORSE_DECODER_MAX_SYMBOLS 8U
#define MORSE_DECODER_QUEUE_LENGTH 16U

/* ProtocolConfig four-tuple, the single source of truth on all three ends
 * (server, gateway, STM32). t_ms = basic unit T (dot duration); dash_t =
 * dash length in dot units; word_gap_t = gap between words in dot units;
 * msg_gap_t = idle gap ending a message in dot units. Every end validates
 * these same ranges before applying, so the four-tuple stays identical. */
#define MORSE_DECODER_T_MS_MIN         60U
#define MORSE_DECODER_T_MS_MAX         2000U
#define MORSE_DECODER_DEFAULT_DOT_MS   60U
#define MORSE_DECODER_DASH_T_MIN       2U
#define MORSE_DECODER_DASH_T_MAX       5U
#define MORSE_DECODER_DEFAULT_DASH_T   3U
#define MORSE_DECODER_WORD_GAP_T_MIN   2U
#define MORSE_DECODER_WORD_GAP_T_MAX   8U
#define MORSE_DECODER_DEFAULT_WORD_GAP_T 4U
#define MORSE_DECODER_MSG_GAP_T_MIN    4U
#define MORSE_DECODER_MSG_GAP_T_MAX    20U
#define MORSE_DECODER_DEFAULT_MSG_GAP_T 10U

typedef enum
{
    MORSE_EVENT_CHARACTER = 0,
    MORSE_EVENT_SPACE,
    MORSE_EVENT_IDLE,
    MORSE_EVENT_INVALID
} MorseDecoderEventType;

typedef enum
{
    MORSE_ERROR_NONE = 0,
    MORSE_ERROR_UNKNOWN_CODE,
    MORSE_ERROR_PULSE_TOO_LONG,
    MORSE_ERROR_TOO_MANY_SYMBOLS,
    MORSE_ERROR_EVENT_OVERFLOW
} MorseDecoderError;

typedef struct
{
    MorseDecoderEventType type;
    MorseDecoderError error;
    char character;
    char code[MORSE_DECODER_MAX_SYMBOLS + 1U];
} MorseDecoderEvent;

/* Caller-owned storage; treat these fields as private. No dynamic allocation. */
typedef struct
{
    uint32_t dot_time_ms;  /* T in ms, 60..2000 */
    uint32_t dash_t;       /* dash = dash_t*T, 2..5 */
    uint32_t word_gap_t;   /* word gap = word_gap_t*T, 2..8 */
    uint32_t msg_gap_t;    /* message idle = msg_gap_t*T, 4..20 */
    uint32_t last_change_ms;
    uint32_t preceding_dark_ms;
    bool sampled;
    bool synchronized;
    bool light;
    bool discard_character;
    bool pending_space;
    bool message_active;
    bool queue_overflow;
    uint8_t code_length;
    char code[MORSE_DECODER_MAX_SYMBOLS + 1U];
    uint8_t queue_read;
    uint8_t queue_write;
    uint8_t queue_count;
    MorseDecoderEvent queue[MORSE_DECODER_QUEUE_LENGTH];
} MorseDecoder;

/* dotTimeMs must match the phone's DOT duration (basic unit T), not a
 * complete flash cycle. dashT = dash length in dot units; wordGapT = gap
 * between words in dot units; msgGapT = idle gap in dot units. Zero values
 * use the defaults above; other out-of-range values are clamped to the
 * documented min/max (validation is the caller's job). dash_t=3 with
 * word_gap_t=4 and msg_gap_t=10 reproduces the classic 2T dot/dash
 * boundary, 4T word gap and 10T idle exactly (backward compatible).
 * Init and Reset discard partial symbols AND all unread events.
 */
void MorseDecoder_Init(MorseDecoder *decoder, uint32_t dotTimeMs,
                       uint32_t dashT, uint32_t wordGapT, uint32_t msgGapT);
void MorseDecoder_Reset(MorseDecoder *decoder, uint32_t nowMs);

/* Call every 1 ms with an already filtered light/dark decision. Both APIs
 * belong to the same main-loop context; they are not ISR/thread safe.
 * nowMs may wrap at UINT32_MAX. Call Reset after missing samples/time jumps.
 * Startup/reset waits for 2T of darkness before accepting a complete pulse.
 */
void MorseDecoder_Feed(MorseDecoder *decoder, bool light, uint32_t nowMs);
bool MorseDecoder_PopEvent(MorseDecoder *decoder, MorseDecoderEvent *event);

/* Timing (T = dot_time_ms, gaps in dot units from the Init four-tuple):
 * dot/dash boundary (1+dash_t)/2*T; character dark gap >=2T;
 * word gap >=word_gap_t*T; idle >=msg_gap_t*T
 * (display newline ONLY, not a reliable protocol frame boundary).
 * SPACE is deferred until a subsequent valid pulse, so idle has no trailing
 * SPACE. Pulses <T/4 are ignored; pulses >2*dash_t*T invalidate the
 * current character.
 * Supported: A-Z, 0-9, + = . , ? / - . INVALID has character '?' and a reason.
 * Drain the queue after Feed. Overflow preserves queued events, then yields
 * one INVALID/MORSE_ERROR_EVENT_OVERFLOW event to make lost data explicit.
 */

#endif
