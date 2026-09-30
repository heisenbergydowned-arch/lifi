#ifndef GATEWAY_STUB_ARDUINO_H
#define GATEWAY_STUB_ARDUINO_H
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
extern time_t fakeEpoch;
inline time_t gateway_test_time(time_t *value) { if (value) *value = fakeEpoch; return fakeEpoch; }
inline struct tm *gmtime_r(const time_t *value, struct tm *result)
{
    struct tm *converted = gmtime(value);
    if (!converted) return nullptr;
    *result = *converted; return result;
}
#define time gateway_test_time
/* Fixed host-only strings allow testing with the installed C/C++ compiler
 * without a separate libstdc++ SDK. The real ESP build uses Arduino String. */
class String
{
public:
    char s[4096];
    String() { s[0] = 0; }
    String(const char *v) { assert(strlen(v) < sizeof(s)); strcpy(s, v); }
    String(int v) { snprintf(s, sizeof(s), "%d", v); }
    void reserve(size_t) {}
    const char *c_str() const { return s; }
    String &operator=(const char *v) { assert(strlen(v) < sizeof(s)); strcpy(s, v); return *this; }
    String &operator+=(const char *v) { assert(strlen(s) + strlen(v) < sizeof(s)); strcat(s, v); return *this; }
    String &operator+=(const String &v) { return *this += v.s; }
    String &operator+=(char v) { char text[2] = {v, 0}; return *this += text; }
    String &operator+=(uint32_t v) { char text[20]; snprintf(text, sizeof(text), "%lu", (unsigned long)v); return *this += text; }
    bool operator==(const String &v) const { return strcmp(s, v.s) == 0; }
    bool operator==(const char *v) const { return strcmp(s, v) == 0; }
    friend String operator+(const String &a, const String &b) { String result(a); result += b; return result; }
};
struct SerialStub
{
    char input[8192] = {}, output[65536] = {};
    void setRxBufferSize(unsigned) {}
    void begin(unsigned) {}
    void setDebugOutput(bool) {}
    int available() const { return (int)strlen(input); }
    int read() { char ch = input[0]; memmove(input, input + 1U, strlen(input)); return ch; }
    void print(const char *text) { assert(strlen(output) + strlen(text) < sizeof(output)); strcat(output, text); }
};
extern SerialStub Serial;
extern uint32_t fakeMillis;
inline uint32_t millis() { return fakeMillis; }
inline void delay(unsigned ms) { fakeMillis += ms; }
inline void configTime(long, int, const char *, const char *) {}
#endif
