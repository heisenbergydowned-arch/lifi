#include "MorseDecoder.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    char character;
    const char *code;
} MorseCodeEntry;

static const MorseCodeEntry morse_table[] =
{
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
    {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
    {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
    {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
    {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
    {'Y', "-.--"},  {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
    {'8', "---.."}, {'9', "----."},
    {'+', ".-.-."}, {'=', "-...-"}, {'.', ".-.-.-"}, {',', "--..--"},
    {'?', "..--.."}, {'/', "-..-."}, {'-', "-....-"}
};

static uint32_t ClampParam(uint32_t value, uint32_t minV, uint32_t maxV,
                           uint32_t defaultV)
{
    if (value == 0U)
    {
        return defaultV;
    }
    if (value < minV)
    {
        return minV;
    }
    if (value > maxV)
    {
        return maxV;
    }
    return value;
}

static void QueueEvent(MorseDecoder *decoder, MorseDecoderEventType type,
                       char character, MorseDecoderError error)
{
    MorseDecoderEvent *event;

    if (decoder->queue_count >= MORSE_DECODER_QUEUE_LENGTH)
    {
        decoder->queue_overflow = true;
        return;
    }

    event = &decoder->queue[decoder->queue_write];
    memset(event, 0, sizeof(*event));
    event->type = type;
    event->error = error;
    event->character = character;
    if ((type == MORSE_EVENT_CHARACTER) || (type == MORSE_EVENT_INVALID))
    {
        memcpy(event->code, decoder->code, sizeof(event->code));
    }
    decoder->queue_write = (uint8_t)((decoder->queue_write + 1U) %
                                    MORSE_DECODER_QUEUE_LENGTH);
    decoder->queue_count++;
}

static void ClearCode(MorseDecoder *decoder)
{
    decoder->code_length = 0U;
    memset(decoder->code, 0, sizeof(decoder->code));
}

static void FlushCharacter(MorseDecoder *decoder)
{
    uint32_t i;

    if (decoder->discard_character)
    {
        decoder->discard_character = false;
        ClearCode(decoder);
        return;
    }
    if (decoder->code_length == 0U)
    {
        return;
    }

    for (i = 0U; i < (sizeof(morse_table) / sizeof(morse_table[0])); i++)
    {
        if (strcmp(decoder->code, morse_table[i].code) == 0)
        {
            QueueEvent(decoder, MORSE_EVENT_CHARACTER,
                       morse_table[i].character, MORSE_ERROR_NONE);
            ClearCode(decoder);
            return;
        }
    }
    QueueEvent(decoder, MORSE_EVENT_INVALID, '?', MORSE_ERROR_UNKNOWN_CODE);
    ClearCode(decoder);
}

static void ProcessDarkness(MorseDecoder *decoder, uint32_t durationMs)
{
    if (durationMs >= (2U * decoder->dot_time_ms))
    {
        FlushCharacter(decoder);
    }
    if (decoder->message_active &&
        (durationMs >= (decoder->msg_gap_t * decoder->dot_time_ms)))
    {
        QueueEvent(decoder, MORSE_EVENT_IDLE, '\n', MORSE_ERROR_NONE);
        decoder->message_active = false;
    }
}

static void InvalidateCharacter(MorseDecoder *decoder, MorseDecoderError error)
{
    if (!decoder->discard_character)
    {
        QueueEvent(decoder, MORSE_EVENT_INVALID, '?', error);
        decoder->discard_character = true;
        decoder->message_active = true;
        decoder->pending_space = false;
        ClearCode(decoder);
    }
}

void MorseDecoder_Init(MorseDecoder *decoder, uint32_t dotTimeMs,
                       uint32_t dashT, uint32_t wordGapT, uint32_t msgGapT)
{
    if (decoder == NULL)
    {
        return;
    }
    memset(decoder, 0, sizeof(*decoder));
    decoder->dot_time_ms = ClampParam(dotTimeMs, MORSE_DECODER_T_MS_MIN,
                                      MORSE_DECODER_T_MS_MAX,
                                      MORSE_DECODER_DEFAULT_DOT_MS);
    decoder->dash_t = ClampParam(dashT, MORSE_DECODER_DASH_T_MIN,
                                 MORSE_DECODER_DASH_T_MAX,
                                 MORSE_DECODER_DEFAULT_DASH_T);
    decoder->word_gap_t = ClampParam(wordGapT, MORSE_DECODER_WORD_GAP_T_MIN,
                                     MORSE_DECODER_WORD_GAP_T_MAX,
                                     MORSE_DECODER_DEFAULT_WORD_GAP_T);
    decoder->msg_gap_t = ClampParam(msgGapT, MORSE_DECODER_MSG_GAP_T_MIN,
                                    MORSE_DECODER_MSG_GAP_T_MAX,
                                    MORSE_DECODER_DEFAULT_MSG_GAP_T);
}

void MorseDecoder_Reset(MorseDecoder *decoder, uint32_t nowMs)
{
    uint32_t dotTimeMs, dashT, wordGapT, msgGapT;

    if (decoder == NULL)
    {
        return;
    }
    dotTimeMs = decoder->dot_time_ms;
    dashT = decoder->dash_t;
    wordGapT = decoder->word_gap_t;
    msgGapT = decoder->msg_gap_t;
    MorseDecoder_Init(decoder, dotTimeMs, dashT, wordGapT, msgGapT);
    decoder->last_change_ms = nowMs;
    decoder->preceding_dark_ms = nowMs;
}

void MorseDecoder_Feed(MorseDecoder *decoder, bool light, uint32_t nowMs)
{
    uint32_t durationMs;
    uint32_t minimumPulseMs;

    if (decoder == NULL)
    {
        return;
    }
    if (!decoder->sampled)
    {
        decoder->sampled = true;
        decoder->light = light;
        decoder->last_change_ms = nowMs;
        decoder->preceding_dark_ms = nowMs;
        return;
    }

    durationMs = (uint32_t)(nowMs - decoder->last_change_ms);
    if (!decoder->synchronized)
    {
        if (light != decoder->light)
        {
            /* An edge after a sufficiently long dark interval can itself
             * complete synchronization, even if Feed did not hit exact 2T.
             */
            if (!decoder->light &&
                (durationMs >= (2U * decoder->dot_time_ms)))
            {
                decoder->synchronized = true;
            }
            decoder->preceding_dark_ms = decoder->last_change_ms;
            decoder->light = light;
            decoder->last_change_ms = nowMs;
        }
        else if (!light && (durationMs >= (2U * decoder->dot_time_ms)))
        {
            decoder->synchronized = true;
        }
        return;
    }

    if (light == decoder->light)
    {
        if (light)
        {
            if (durationMs > (2U * decoder->dash_t * decoder->dot_time_ms))
            {
                InvalidateCharacter(decoder, MORSE_ERROR_PULSE_TOO_LONG);
            }
        }
        else
        {
            ProcessDarkness(decoder, durationMs);
        }
        return;
    }

    if (light)
    {
        ProcessDarkness(decoder, durationMs);
        decoder->pending_space = decoder->message_active &&
                                 (durationMs >=
                                  (decoder->word_gap_t * decoder->dot_time_ms));
        decoder->preceding_dark_ms = decoder->last_change_ms;
        decoder->last_change_ms = nowMs;
        decoder->light = true;
        return;
    }

    decoder->light = false;
    decoder->last_change_ms = nowMs;
    minimumPulseMs = decoder->dot_time_ms / 4U;
    if (minimumPulseMs == 0U)
    {
        minimumPulseMs = 1U;
    }
    if (durationMs < minimumPulseMs)
    {
        /* A rejected flash must not restart a genuine dark interval, nor
         * insert a word separator. Preserve the preceding dark timestamp.
         */
        decoder->last_change_ms = decoder->preceding_dark_ms;
        decoder->pending_space = false;
        ProcessDarkness(decoder,
                        (uint32_t)(nowMs - decoder->last_change_ms));
        return;
    }
    if (durationMs > (2U * decoder->dash_t * decoder->dot_time_ms))
    {
        InvalidateCharacter(decoder, MORSE_ERROR_PULSE_TOO_LONG);
    }
    if (decoder->discard_character)
    {
        decoder->pending_space = false;
        return;
    }
    if (decoder->pending_space)
    {
        QueueEvent(decoder, MORSE_EVENT_SPACE, ' ', MORSE_ERROR_NONE);
        decoder->pending_space = false;
    }
    if (decoder->code_length >= MORSE_DECODER_MAX_SYMBOLS)
    {
        InvalidateCharacter(decoder, MORSE_ERROR_TOO_MANY_SYMBOLS);
        return;
    }

    /* Dot/dash boundary (1+dash_t)/2*T. The comparison is doubled so odd
     * (1+dash_t) values keep their half-T precision (dash_t=2 -> 1.5T,
     * dash_t=4 -> 2.5T); with the default dash_t=3 this is exactly 2T. */
    decoder->code[decoder->code_length] =
        ((2U * durationMs) <
         ((1U + decoder->dash_t) * decoder->dot_time_ms)) ? '.' : '-';
    decoder->code_length++;
    decoder->code[decoder->code_length] = '\0';
    decoder->message_active = true;
}

bool MorseDecoder_PopEvent(MorseDecoder *decoder, MorseDecoderEvent *event)
{
    if ((decoder == NULL) || (event == NULL))
    {
        return false;
    }
    if (decoder->queue_count > 0U)
    {
        *event = decoder->queue[decoder->queue_read];
        decoder->queue_read = (uint8_t)((decoder->queue_read + 1U) %
                                       MORSE_DECODER_QUEUE_LENGTH);
        decoder->queue_count--;
        return true;
    }
    if (decoder->queue_overflow)
    {
        memset(event, 0, sizeof(*event));
        event->type = MORSE_EVENT_INVALID;
        event->error = MORSE_ERROR_EVENT_OVERFLOW;
        event->character = '?';
        decoder->queue_overflow = false;
        return true;
    }
    return false;
}
