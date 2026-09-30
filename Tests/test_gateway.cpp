#include "../ESP8266_Gateway/src/main.cpp"
SerialStub Serial;
WiFiStub WiFi;
MDNSStub MDNS;
const char *Preferences::names[Preferences::MAX_PARTITIONS];
Preferences::PrefsEntry Preferences::entries[Preferences::MAX_ENTRIES];
int Preferences::count = 0;
int Preferences::nameIndex = 0;
uint32_t fakeMillis = 0U;
time_t fakeEpoch = 1789041600;
int fakeHttpStatus = 200;
bool fakeMatchingAck = false, fakeWrongAck = false;
unsigned httpRequests = 0U;
char httpKeys[20][256], httpBodies[20][2048];
void *operator new(size_t size) { void *p = malloc(size); if (!p) abort(); return p; }
void operator delete(void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
extern "C" uint32_t os_random(void) { static uint32_t n = 100U; return ++n; }
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL gateway line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static IotRecord record()
{
    IotRecord r = {};
    r.address = 7U; r.boot = 20U; r.sequence = 1U; r.uptime_ms = 1000U; r.dot_ms = 60U;
    r.flags = IOT_FLAG_BOOT_VALID | IOT_FLAG_TIME_VALID | IOT_FLAG_DISTRESS;
    strcpy(r.timestamp, "2026-09-10T20:00:00+08:00"); strcpy(r.text, "SOS"); return r;
}
static void inject(const IotRecord &r)
{
    char line[IOT_LINE_SIZE]; CHECK(IotProtocol_Encode(&r, line, sizeof(line)));
    strcat(Serial.input, line); readSerial();
}
static void injectH1()
{
    char h1[IOT_LINE_SIZE];
    snprintf(h1, sizeof(h1), "H1|7|20");
    CHECK(IotProtocol_Seal(h1, sizeof(h1)));
    strcat(Serial.input, h1);
    readSerial();
}
static void later(uint32_t ms) { fakeMillis += ms; loop(); }
int main()
{
    setup(); CHECK(strstr(Serial.output, "S1|BOOT*") != nullptr);
    IotRecord r = record(); inject(r); CHECK(hasPending && !hasAck);
    Serial.output[0] = 0; WiFi.state = 0; later(1U); CHECK(httpRequests == 0U);
    WiFi.state = WL_CONNECTED; fakeEpoch = 0; later(1U); CHECK(httpRequests == 0U && hasPending);
    fakeEpoch = 1789041600; later(1U); CHECK(httpRequests == 1U && hasPending && !hasAck);
    CHECK(strstr(Serial.output, "A1|") == nullptr); // 200 alone is not delivery.
    CHECK(strstr(httpBodies[0], "\"alarm\":true") != nullptr);
    CHECK(strstr(httpBodies[0], "\"address\":7") != nullptr);
    CHECK(strstr(httpBodies[0], "2026-09-10T20:00:00+08:00") != nullptr);
    char key[256]; strcpy(key, httpKeys[0]); inject(r); later(1000U); CHECK(httpRequests == 1U);
    fakeWrongAck = true; later(10000U); CHECK(httpRequests == 2U && hasPending);
    fakeMatchingAck = true; fakeHttpStatus = 500; later(20000U); CHECK(httpRequests == 3U && hasPending);
    fakeHttpStatus = 201; later(40000U); CHECK(httpRequests == 4U && !hasPending && hasAck);
    for (unsigned i = 0; i < httpRequests; ++i) CHECK(strcmp(httpKeys[i], key) == 0);
    CHECK(strstr(Serial.output, "A1|7|20|1|") != nullptr);
    inject(r); later(60001U); CHECK(httpRequests == 4U);
    ++r.sequence; strcpy(r.text, "HELP"); inject(r); CHECK(hasPending);
    IotRecord next = r; ++next.sequence; inject(next); CHECK(pending.sequence == r.sequence);
    later(1U); CHECK(httpRequests == 5U && !hasPending && strcmp(httpKeys[4], key) != 0);
    memset(Serial.input, 'X', 400U); strcpy(Serial.input + 400U, "\n"); readSerial(); CHECK(!hasPending);
    inject(next); later(1U); CHECK(httpRequests == 6U);
    /* ---- Protocol four-tuple: downlink, Web config, persistence, H1 ---- */
    later(1U);
    CHECK(strstr(Serial.output, "C1|60|3|4|10*") != nullptr); // default four-tuple
    configServer.on("/api/config", HTTP_GET, onConfigJson);
    CHECK(configServer.status == 200 &&
          strstr(configServer.content.s, "\"token\":\"e815b5c76073af05\"") != nullptr &&
          strstr(configServer.content.s, "\"persisted\":false") != nullptr);
    configServer.lastArgs = "t_ms=80&dash_t=4&word_gap_t=5&msg_gap_t=12";
    configServer.on("/api/config", HTTP_POST, onConfigPost);
    CHECK(configServer.status == 200 &&
          strstr(Serial.output, "C1|80|4|5|12*") != nullptr &&
          strstr(configServer.content.s, "\"token\":\"d2c417a2abb16cb7\"") != nullptr);
    Serial.output[0] = 0;
    configServer.lastArgs = "t_ms=59&dash_t=3&word_gap_t=4&msg_gap_t=10";
    configServer.on("/api/config", HTTP_POST, onConfigPost);
    CHECK(configServer.status == 400 && strstr(Serial.output, "C1|59") == nullptr);
    configServer.lastArgs = "t_ms=60&dash_t=3&word_gap_t=4&msg_gap_t=";
    configServer.on("/api/config", HTTP_POST, onConfigPost);
    CHECK(configServer.status == 400);
    configServer.on("/api/config", HTTP_GET, onConfigJson);
    CHECK(strstr(configServer.content.s, "\"t_ms\":80") != nullptr && // rejected POST keeps old config
          strstr(configServer.content.s, "\"token\":\"d2c417a2abb16cb7\"") != nullptr);
    Serial.output[0] = 0;
    configServer.lastArgs = "";
    cfgLoaded = false;
    c1Sent = false;
    setup(); // reboot: Preferences statics keep the 80/4/5/12 partition
    CHECK(cfgLoaded && strstr(Serial.output, "CFG_DEFAULT") == nullptr);
    later(1U);
    CHECK(strstr(Serial.output, "C1|80|4|5|12*") != nullptr); // restored from flash
    configServer.on("/api/config", HTTP_GET, onConfigJson);
    CHECK(strstr(configServer.content.s, "\"persisted\":true") != nullptr);
    Serial.output[0] = 0;
    injectH1(); // STM32 reboot: C1 must be pushed immediately, not after the throttle
    CHECK(strstr(Serial.output, "S1|UART_READY*") != nullptr &&
          strstr(Serial.output, "C1|80|4|5|12*") != nullptr);
    fprintf(stderr, "PASS: actual gateway state machine, %u checks (mock HTTPS, no real TLS/network)\n", checks);
}
