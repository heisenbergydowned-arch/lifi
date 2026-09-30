#ifndef GATEWAY_STUB_PREFERENCES_H
#define GATEWAY_STUB_PREFERENCES_H

#include "Arduino.h"

/* In-memory NVS replacement. Partitions are stored in a static pool keyed by
 * partition name, so they survive object destruction (mirroring flash) and
 * test code can inspect or reset them. Static members are defined in
 * test_gateway.cpp (main.cpp is #included into that translation unit). */
struct Preferences
{
    static const int MAX_PARTITIONS = 2;
    static const int MAX_ENTRIES = 16;
    static const char *names[MAX_PARTITIONS];
    static struct PrefsEntry
    {
        const char *partition;
        const char *key;
        uint32_t value;
    }
    entries[MAX_ENTRIES];
    static int count;
    static int nameIndex;

    const char *current = nullptr;

    Preferences() = default;

    bool begin(const char *name, bool readOnly)
    {
        for (int i = 0; i < nameIndex; ++i)
        {
            if (strcmp(names[i], name) == 0)
            {
                current = names[i];
                return true;
            }
        }
        if (readOnly || nameIndex >= MAX_PARTITIONS || count >= MAX_ENTRIES)
            return false;
        names[nameIndex] = name;
        ++nameIndex;
        current = name;
        return true;
    }

    void end() { current = nullptr; }

    bool containsKey(const char *key) const
    {
        for (int i = 0; i < count; ++i)
        {
            if (entries[i].partition == current && strcmp(entries[i].key, key) == 0)
                return true;
        }
        return false;
    }

    uint32_t getUint32(const char *key, uint32_t defaultValue = 0) const
    {
        for (int i = 0; i < count; ++i)
        {
            if (entries[i].partition == current && strcmp(entries[i].key, key) == 0)
                return entries[i].value;
        }
        return defaultValue;
    }

    void putUint32(const char *key, uint32_t value)
    {
        for (int i = 0; i < count; ++i)
        {
            if (entries[i].partition == current && strcmp(entries[i].key, key) == 0)
            {
                entries[i].value = value;
                return;
            }
        }
        entries[count].partition = current;
        entries[count].key = key;
        entries[count].value = value;
        ++count;
    }
};

#endif
