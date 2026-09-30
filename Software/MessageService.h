#ifndef MESSAGE_SERVICE_H
#define MESSAGE_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
#include "IotProtocol.h"
/* Logical receiver location/address, NOT an IP address. Give each receiver
 * its own number before the website is connected. */
#ifndef DEVICE_ADDRESS
#define DEVICE_ADDRESS 1U
#endif
#if DEVICE_ADDRESS < 1U || DEVICE_ADDRESS > 65535U
#error "DEVICE_ADDRESS must be 1..65535"
#endif
void MessageService_Init(bool rtc_present);
uint32_t MessageService_GetBootCounter(void);
void MessageService_SetNetworkTime(const char *iso_time);
typedef struct
{
    char text[IOT_TEXT_SIZE];
    uint32_t sequence, flags;
    bool receiving;
} MessageService_DisplayState;
/* Main-loop snapshot. Partial text takes precedence, otherwise retain the
 * last completed local message, independently of the network queue/ACK. */
void MessageService_GetDisplayState(MessageService_DisplayState *state);
#endif
