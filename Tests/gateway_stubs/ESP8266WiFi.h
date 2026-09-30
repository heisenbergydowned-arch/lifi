#include "Arduino.h"
#define WIFI_STA 1
#define WL_CONNECTED 3
struct WiFiStub
{
    int state = WL_CONNECTED;
    void persistent(bool) {}
    void mode(int) {}
    void setAutoReconnect(bool) {}
    void begin(const char *, const char *) {}
    void softAP(const char *, const char *) {}
    int status() const { return state; }
    void reconnect() {}
};
extern WiFiStub WiFi;
