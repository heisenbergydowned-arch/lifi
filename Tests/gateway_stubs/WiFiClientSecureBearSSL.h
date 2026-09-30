#ifndef GATEWAY_STUB_TLS_H
#define GATEWAY_STUB_TLS_H
#include "Arduino.h"
namespace BearSSL
{
class X509List { public: explicit X509List(const char *pem) { assert(strstr(pem, "BEGIN CERTIFICATE")); } };
class WiFiClientSecure
{
public:
    bool trustSet = false;
    void setTrustAnchors(X509List *value) { assert(value); trustSet = true; }
    void setTimeout(unsigned) {}
    void stop() {}
};
}
#endif
