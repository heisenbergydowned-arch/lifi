#include "WiFiClientSecureBearSSL.h"
#define HTTPC_DISABLE_FOLLOW_REDIRECTS 0
extern int fakeHttpStatus;
extern bool fakeMatchingAck, fakeWrongAck;
extern unsigned httpRequests;
extern char httpKeys[20][256], httpBodies[20][2048];
class HTTPClient
{
    String keyHeader, authHeader, contentHeader;
public:
    void setTimeout(unsigned) {}
    void setReuse(bool value) { assert(!value); }
    void setFollowRedirects(int mode) { assert(mode == HTTPC_DISABLE_FOLLOW_REDIRECTS); }
    bool begin(BearSSL::WiFiClientSecure &client, const char *url) { assert(client.trustSet); return strncmp(url, "https://", 8U) == 0; }
    void collectHeaders(const char **keys, size_t count) { assert(count == 1U && strcmp(keys[0], "X-Morse-Ack") == 0); }
    void addHeader(const String &name, const String &value)
    {
        if (name == "Authorization") authHeader = value;
        else if (name == "Idempotency-Key") keyHeader = value;
        else if (name == "Content-Type") contentHeader = value;
    }
    int POST(const String &body)
    {
        assert(contentHeader == "application/json"); assert(authHeader == "Bearer BUILD_CHECK_NO_TOKEN");
        assert(httpRequests < 20U && strlen(keyHeader.s) < 256U && strlen(body.s) < 2048U);
        strcpy(httpKeys[httpRequests], keyHeader.s); strcpy(httpBodies[httpRequests], body.s);
        ++httpRequests; return fakeHttpStatus;
    }
    String header(const char *) { return fakeMatchingAck ? keyHeader : String(fakeWrongAck ? "WRONG_EVENT" : ""); }
    void end() {}
};
