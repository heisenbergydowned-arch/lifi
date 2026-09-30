#ifndef GATEWAY_STUB_MDNS_H
#define GATEWAY_STUB_MDNS_H

struct MDNSStub
{
    int beginCount = 0;
    const char *instance = nullptr;
    bool begin(const char *name)
    {
        ++beginCount;
        instance = name;
        return true;
    }
    void update() {}
};

extern MDNSStub MDNS;

#endif
