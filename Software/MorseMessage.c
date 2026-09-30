#include "MorseMessage.h"
#include <string.h>
static bool IsWord(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}
void MorseMessage_Init(MorseMessage *message)
{
    if (message != NULL) memset(message, 0, sizeof(*message));
}
static void Append(MorseMessage *message, char ch)
{
    if (message->length >= MORSE_MESSAGE_MAX_TEXT) { message->truncated = true; return; }
    message->text[message->length++] = ch;
    message->text[message->length] = '\0';
}
bool MorseMessage_Feed(MorseMessage *message, const MorseDecoderEvent *event)
{
    char ch;
    if (message == NULL || event == NULL) return false;
    switch (event->type)
    {
    case MORSE_EVENT_CHARACTER:
        ch = event->character;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        Append(message, ch); break;
    case MORSE_EVENT_SPACE:
        if (message->length && message->text[message->length - 1U] != ' ') Append(message, ' ');
        break;
    case MORSE_EVENT_INVALID: message->invalid = true; Append(message, '?'); break;
    case MORSE_EVENT_IDLE:
        while (message->length && message->text[message->length - 1U] == ' ')
            message->text[--message->length] = '\0';
        return message->length != 0U;
    default: break;
    }
    return false;
}
bool MorseMessage_IsDistress(const MorseMessage *message)
{
    size_t i = 0U;
    if (message == NULL || message->invalid || message->truncated) return false;
    while (i < message->length)
    {
        size_t start, length;
        while (i < message->length && !IsWord(message->text[i])) ++i;
        start = i;
        while (i < message->length && IsWord(message->text[i])) ++i;
        length = i - start;
        if ((length == 3U && memcmp(message->text + start, "SOS", 3U) == 0) ||
            (length == 4U && memcmp(message->text + start, "HELP", 4U) == 0)) return true;
    }
    return false;
}
