#ifndef MORSE_MESSAGE_H
#define MORSE_MESSAGE_H
#include "MorseDecoder.h"
#include <stddef.h>
#define MORSE_MESSAGE_MAX_TEXT 96U
typedef struct
{
    char text[MORSE_MESSAGE_MAX_TEXT + 1U];
    uint16_t length;
    bool invalid;
    bool truncated;
} MorseMessage;
/* Frame ends at >=10T IDLE; 7T remains a word gap. Caller consumes the
 * completed value then calls Init. Init discards unfinished text. */
void MorseMessage_Init(MorseMessage *message);
bool MorseMessage_Feed(MorseMessage *message, const MorseDecoderEvent *event);
bool MorseMessage_IsDistress(const MorseMessage *message);
#endif
