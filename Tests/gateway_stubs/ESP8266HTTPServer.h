#ifndef GATEWAY_STUB_HTTPSERVER_H
#define GATEWAY_STUB_HTTPSERVER_H

#include "Arduino.h"

enum HTTPMethod
{
    HTTP_ANY = -1,
    HTTP_GET = 0,
    HTTP_POST = 1
};

/* Test-oriented HTTP server stub: registering a handler with on() invokes it
 * immediately, so tests trigger endpoints by setting lastArgs ("k1=v1&k2=v2")
 * and calling on(), then assert on status/content and the UART output. */
struct HTTPServer
{
    const char *path = nullptr;
    int method = HTTP_ANY;
    int status = 200;
    String content;
    String lastArgs;

    HTTPServer(const char *p, int) : path(p) {}

    void on(const char *p, int m, void (*handler)())
    {
        path = p;
        method = m;
        if (handler)
            handler();
    }

    void begin() {}

    void send(int s, const char *, const String &body)
    {
        status = s;
        content = body;
    }

    void send(int s, const char *, const char *body)
    {
        status = s;
        content = String(body);
    }

    /* Look up "name" inside lastArgs ("k=v&k=v"). Returns an empty string
     * when the key is absent, matching the real library's contract. */
    String arg(const char *name) const
    {
        const char *cursor = lastArgs.s;
        const size_t nameLen = strlen(name);
        while (cursor[0] != '\0')
        {
            const char *eq = strchr(cursor, '=');
            const char *next = strchr(cursor, '&');
            if (eq != nullptr && (next == nullptr || eq < next))
            {
                const size_t keyLen = static_cast<size_t>(eq - cursor);
                if (keyLen == nameLen && strncmp(cursor, name, nameLen) == 0)
                {
                    const char *value = eq + 1;
                    const size_t valueLen = next == nullptr ? strlen(value) : (size_t)(next - value);
                    char copy[512];
                    if (valueLen >= sizeof(copy))
                        return String();
                    memcpy(copy, value, valueLen);
                    copy[valueLen] = 0;
                    return String(copy);
                }
            }
            if (next == nullptr)
                break;
            cursor = next + 1;
        }
        return String();
    }
};

#endif
