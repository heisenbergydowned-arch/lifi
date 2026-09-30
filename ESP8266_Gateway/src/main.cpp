#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>
#include <memory>
#include <Preferences.h>
#include <ESP8266HTTPServer.h>
#include <ESP8266mDNS.h>
extern "C" {
#include <user_interface.h>
}
#include "GatewayConfig.h"
#include "IotProtocol.h"
#include "ProtocolConfig.h"

static char input[IOT_LINE_SIZE];
static size_t used;
static bool discardLine, hasPending, hasAck;
static IotRecord pending, lastAck;
static uint32_t lastAttempt, retryMs = REPORT_RETRY_MIN_MS, lastSync, lastWifi;
static bool attempted, clockSent;
static bool wifiWasConnected;
static char eventId[128], bootNonce[33];
static uint32_t sessionNumber;
static String previousStatus;
static std::unique_ptr<BearSSL::X509List> trust;
/* Protocol four-tuple shared with the server and the STM32. Persisted in
 * Preferences so a gateway reboot cannot desynchronize the STM32; the
 * consistency token lets every end detect a mismatch without extra traffic. */
static ProtocolConfig cfg;
static bool cfgValid, cfgLoaded, c1Sent;
static char cfgToken[17];
static uint32_t lastC1;
static HTTPServer configServer(nullptr, 80);

static void sendBody(const char *body)
{
    char frame[IOT_LINE_SIZE];
    if (strlen(body) >= sizeof(frame)) return;
    strcpy(frame, body);
    if (IotProtocol_Seal(frame, sizeof(frame))) Serial.print(frame);
}
static void sendC1(uint32_t now, bool force); /* Defined below; H1 handling needs it. */
static void status(const String &value)
{
    if (previousStatus == value) return;
    previousStatus = value;
    String body = "S1|" + value;
    sendBody(body.c_str());
}
static void acknowledge(const IotRecord &record)
{
    char body[80];
    snprintf(body, sizeof(body), "A1|%lu|%lu|%lu|%u", (unsigned long)record.address,
             (unsigned long)record.boot, (unsigned long)record.sequence, (unsigned)record.wire_crc);
    sendBody(body);
}
static bool same(const IotRecord &a, const IotRecord &b)
{
    return a.address == b.address && a.boot == b.boot && a.sequence == b.sequence &&
           a.uptime_ms == b.uptime_ms && a.dot_ms == b.dot_ms && a.flags == b.flags &&
           a.wire_crc == b.wire_crc && strcmp(a.timestamp, b.timestamp) == 0 && strcmp(a.text, b.text) == 0;
}
static void makeEventId()
{
    if (pending.flags & IOT_FLAG_BOOT_VALID)
    {
        // Stable across ESP resets while the DS1302 boot counter is retained.
        snprintf(eventId, sizeof(eventId), "a%lu-b%lu-m%lu-t%s-u%lu-c%04x",
                 (unsigned long)pending.address, (unsigned long)pending.boot,
                 (unsigned long)pending.sequence, pending.timestamp,
                 (unsigned long)pending.uptime_ms, (unsigned)pending.wire_crc);
    }
    else
    {
        // RTC absent: only this powered gateway/session can deduplicate.
        snprintf(eventId, sizeof(eventId), "a%lu-g%s-s%lu-m%lu-c%04x", (unsigned long)pending.address,
                 bootNonce, (unsigned long)sessionNumber, (unsigned long)pending.sequence, (unsigned)pending.wire_crc);
    }
}
static void handleLine()
{
    IotRecord record;
    if (strncmp(input, "H1|", 3U) == 0)
    {
        if (!IotProtocol_Open(input, nullptr)) { status("BAD_FRAME"); return; }
        // H1 occurs once at STM32 startup and after an ESP BOOT indication.
        // Keep an in-flight upload: the MCU may have restarted, but the old
        // message is still a real event and must finish before accepting more.
        ++sessionNumber; hasAck = false;
        clockSent = false;
        status("UART_READY");
        /* H1 means the STM32 just (re)booted: its RAM config is gone, so push
         * the four-tuple immediately instead of waiting for the resend timer. */
        sendC1(millis(), true);
        return;
    }
    if (!IotProtocol_Decode(input, &record)) { status("BAD_FRAME"); return; }
    if (hasAck && same(record, lastAck)) { acknowledge(lastAck); return; }
    if (hasPending)
    {
        if (!same(record, pending)) status("BUSY_PENDING_UPLOAD");
        return;
    }
    pending = record; hasPending = true; attempted = false;
    retryMs = REPORT_RETRY_MIN_MS; makeEventId();
    status("BUFFERED_NOT_UPLOADED");
}
static void readSerial()
{
    while (Serial.available())
    {
        char ch = static_cast<char>(Serial.read());
        if (ch == '\n')
        {
            if (!discardLine && used) { input[used] = '\0'; handleLine(); }
            used = 0U; discardLine = false;
        }
        else if (!discardLine)
        {
            if (used + 1U < sizeof(input)) input[used++] = ch;
            else { discardLine = true; status("UART_LINE_OVERFLOW"); }
        }
    }
}
static bool timeReady()
{
    return time(nullptr) >= 1700000000;
}
static void sendClock(uint32_t now)
{
    if (!timeReady() || (clockSent && static_cast<uint32_t>(now - lastSync) < RTC_SYNC_INTERVAL_MS)) return;
    time_t localSeconds = time(nullptr) + 8 * 3600;
    struct tm clockValue;
    if (!gmtime_r(&localSeconds, &clockValue) || clockValue.tm_year > 199) return;
    char body[48];
    if (!strftime(body, sizeof(body), "T1|%Y-%m-%dT%H:%M:%S+08:00", &clockValue)) return;
    sendBody(body); clockSent = true; lastSync = now;
}
static void applyConfig(const ProtocolConfig &next)
{
    cfg = next;
    cfgValid = true;
    ProtocolConfig_ComputeToken(&cfg, cfgToken);
}
static bool loadProtocolConfig()
{
    Preferences store;
    if (!store.begin("lifi", true)) return false;
    bool ok = false;
    if (store.containsKey("t") && store.containsKey("d") &&
        store.containsKey("wg") && store.containsKey("mg"))
    {
        ProtocolConfig next;
        next.t_ms = store.getUint32("t");
        next.dash_t = store.getUint32("d");
        next.word_gap_t = store.getUint32("wg");
        next.msg_gap_t = store.getUint32("mg");
        char text[64];
        /* Round-trip through Parse so corrupt flash falls back to defaults
         * using exactly the same range rules as every other end. */
        ok = ProtocolConfig_ToText(&next, text, sizeof(text)) && ProtocolConfig_Parse(text, &next);
        if (ok) applyConfig(next);
    }
    store.end();
    return ok;
}
static bool saveProtocolConfig()
{
    Preferences store;
    if (!store.begin("lifi", false)) return false;
    store.putUint32("t", cfg.t_ms);
    store.putUint32("d", cfg.dash_t);
    store.putUint32("wg", cfg.word_gap_t);
    store.putUint32("mg", cfg.msg_gap_t);
    store.end();
    return true;
}
static void sendC1(uint32_t now, bool force)
{
    if (!cfgValid) return;
    if (!force && c1Sent && static_cast<uint32_t>(now - lastC1) < C1_RESEND_MS) return;
    char text[64];
    char body[128];
    if (!ProtocolConfig_ToText(&cfg, text, sizeof(text))) return;
    snprintf(body, sizeof(body), "C1|%s", text);
    sendBody(body);
    c1Sent = true;
    lastC1 = now;
}
/* Strict unsigned-decimal parse for the web endpoint: every character a
 * digit, at most 10, no overflow. Range validation is left to
 * ProtocolConfig_Parse (single source of truth), not duplicated here. */
static bool parseU32Field(const char *text, uint32_t *out)
{
    if (!text || !text[0]) return false;
    const char *end = text + strlen(text);
    uint32_t result = 0U;
    while (text < end)
    {
        if (*text < '0' || *text > '9') return false;
        uint32_t digit = (uint32_t)(*text - '0');
        if (result > 0xFFFFFFFFU / 10U || (result == 0xFFFFFFFFU / 10U && digit > 5U)) return false;
        result = result * 10U + digit;
        ++text;
    }
    *out = result;
    return true;
}
static bool configFromArgs(uint32_t t, uint32_t d, uint32_t wg, uint32_t mg, ProtocolConfig *out)
{
    char text[64];
    snprintf(text, sizeof(text), "%lu|%lu|%lu|%lu", (unsigned long)t, (unsigned long)d,
             (unsigned long)wg, (unsigned long)mg);
    return ProtocolConfig_Parse(text, out);
}
static String configJson()
{
    String body;
    body.reserve(160);
    body = "{\"t_ms\":"; body += cfg.t_ms;
    body += ",\"dash_t\":"; body += cfg.dash_t;
    body += ",\"word_gap_t\":"; body += cfg.word_gap_t;
    body += ",\"msg_gap_t\":"; body += cfg.msg_gap_t;
    body += ",\"persisted\":"; body += cfgLoaded ? "true" : "false";
    body += ",\"token\":\""; body += cfgToken; body += "\"}";
    return body;
}
static const char CONFIG_PAGE_HTML[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>LiFi gateway</title></head><body><h2>LiFi gateway config</h2>"
    "<form id='f'>"
    "<label>T ms (60-2000) <input name='t_ms' id='a' type='number' min='60' max='2000'></label><br>"
    "<label>dash (2-5) <input name='dash_t' id='b' type='number' min='2' max='5'></label><br>"
    "<label>word gap (2-8) <input name='word_gap_t' id='c' type='number' min='2' max='8'></label><br>"
    "<label>message gap (4-20) <input name='msg_gap_t' id='d' type='number' min='4' max='20'></label><br>"
    "<button>Save</button></form><pre id='s'></pre><script>"
    "fetch('/api/config').then(function(r){return r.json()}).then(function(j){"
    "document.getElementById('a').value=j.t_ms;document.getElementById('b').value=j.dash_t;"
    "document.getElementById('c').value=j.word_gap_t;document.getElementById('d').value=j.msg_gap_t;"
    "document.getElementById('s').textContent=JSON.stringify(j)});"
    "document.getElementById('f').onsubmit=function(e){"
    "e.preventDefault();var f=new FormData(e.target);"
    "fetch('/api/config?'+new URLSearchParams(f),{method:'POST'}).then(function(r){return r.text()})"
    ".then(function(t){document.getElementById('s').textContent=t})};</script></body></html>";
static void onConfigPage()
{
    configServer.send(200, "text/html", CONFIG_PAGE_HTML);
}
static void onConfigJson()
{
    configServer.send(200, "application/json", configJson());
}
static void onConfigPost()
{
    uint32_t t, d, wg, mg;
    if (!parseU32Field(configServer.arg("t_ms").c_str(), &t) ||
        !parseU32Field(configServer.arg("dash_t").c_str(), &d) ||
        !parseU32Field(configServer.arg("word_gap_t").c_str(), &wg) ||
        !parseU32Field(configServer.arg("msg_gap_t").c_str(), &mg) ||
        !configFromArgs(t, d, wg, mg, &cfg))
    {
        configServer.send(400, "text/plain", "bad config");
        return;
    }
    applyConfig(cfg);
    if (!saveProtocolConfig())
    {
        configServer.send(500, "text/plain", "save failed");
        return;
    }
    cfgLoaded = true;
    sendC1(millis(), true); /* Force: the STM32 may be waiting on new config. */
    configServer.send(200, "application/json", configJson());
}
static bool reportingConfigured()
{
    return strncmp(REPORT_HTTPS_URL, "https://", 8U) == 0 && strlen(REPORT_DEVICE_TOKEN) > 0U &&
           strstr(REPORT_ROOT_CA_PEM, "-----BEGIN CERTIFICATE-----") != nullptr;
}
static String makeJson()
{
    // Codec accepts only Morse ASCII. No quotes/backslashes/control bytes can
    // enter text; timestamp is strictly parsed before this serializer.
    String json; json.reserve(600);
    json = "{\"schema\":1,\"event_id\":\""; json += eventId;
    json += "\",\"address\":"; json += pending.address;
    json += ",\"boot\":"; json += pending.boot;
    json += ",\"message_id\":"; json += pending.sequence;
    json += ",\"uptime_ms\":"; json += pending.uptime_ms;
    json += ",\"t_ms\":"; json += pending.dot_ms;
    json += ",\"time_valid\":"; json += (pending.flags & IOT_FLAG_TIME_VALID) ? "true" : "false";
    json += ",\"received_at\":";
    if (pending.flags & IOT_FLAG_TIME_VALID) { json += '"'; json += pending.timestamp; json += '"'; }
    else json += "null";
    json += ",\"boot_counter_valid\":"; json += (pending.flags & IOT_FLAG_BOOT_VALID) ? "true" : "false";
    json += ",\"decode_valid\":"; json += (pending.flags & IOT_FLAG_DECODE_INVALID) ? "false" : "true";
    json += ",\"truncated\":"; json += (pending.flags & IOT_FLAG_TRUNCATED) ? "true" : "false";
    json += ",\"alarm\":"; json += (pending.flags & IOT_FLAG_DISTRESS) ? "true" : "false";
    json += ",\"text\":\""; json += pending.text; json += "\"}";
    return json;
}
static void upload(uint32_t now)
{
    if (!hasPending) return;
    if (!reportingConfigured()) { status("REPORT_CONFIG_MISSING"); return; }
    if (!timeReady()) { status("WAIT_NTP_FOR_TLS"); return; }
    if (attempted && static_cast<uint32_t>(now - lastAttempt) < retryMs) return;
    attempted = true;
    if (!trust) trust.reset(new BearSSL::X509List(REPORT_ROOT_CA_PEM));
    BearSSL::WiFiClientSecure client;
    client.setTrustAnchors(trust.get());
    client.setTimeout(5000);
    HTTPClient http;
    http.setTimeout(5000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(client, REPORT_HTTPS_URL))
    { status("HTTPS_URL_ERROR"); lastAttempt = millis(); return; }
    const char *headers[] = {"X-Morse-Ack"};
    http.collectHeaders(headers, 1U);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + REPORT_DEVICE_TOKEN);
    http.addHeader("Idempotency-Key", eventId);
    String body = makeJson();
    int result = http.POST(body);
    bool confirmed = (result == 200 || result == 201) && http.header("X-Morse-Ack") == eventId;
    http.end(); client.stop();
    lastAttempt = millis();
    if (confirmed)
    {
        lastAck = pending; hasAck = true; hasPending = false;
        acknowledge(lastAck); status("VPS_CONFIRMED");
    }
    else
    {
        status(String("HTTPS_NOT_CONFIRMED_") + result);
        retryMs = retryMs >= REPORT_RETRY_MAX_MS / 2U ? REPORT_RETRY_MAX_MS : retryMs * 2U;
    }
}
void setup()
{
    Serial.setRxBufferSize(1024);
    Serial.begin(115200);
    Serial.setDebugOutput(false);
    snprintf(bootNonce, sizeof(bootNonce), "%08lx%08lx%08lx%08lx",
             (unsigned long)os_random(), (unsigned long)os_random(),
             (unsigned long)os_random(), (unsigned long)os_random());
    status("BOOT");
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    if (strlen(WIFI_SSID)) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    configTime(0, 0, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);
    /* Always-on configuration access point + Web UI (http://192.168.4.1). */
    WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASSWORD);
    MDNS.begin(MDNS_INSTANCE);
    cfgLoaded = loadProtocolConfig();
    if (!cfgLoaded)
    {
        ProtocolConfig_Default(&cfg);
        applyConfig(cfg);
        status("CFG_DEFAULT");
    }
    configServer.on("/", HTTP_GET, onConfigPage);
    configServer.on("/api/config", HTTP_GET, onConfigJson);
    configServer.on("/api/config", HTTP_POST, onConfigPost);
    configServer.begin();
}
void loop()
{
    readSerial();
    uint32_t now = millis();
    if (!strlen(WIFI_SSID)) status("WIFI_CONFIG_MISSING");
    else if (WiFi.status() != WL_CONNECTED)
    {
        wifiWasConnected = false;
        status("WIFI_DISCONNECTED");
        if (static_cast<uint32_t>(now - lastWifi) >= 15000U)
        { lastWifi = now; WiFi.reconnect(); }
    }
    else
    {
        if (!wifiWasConnected) { wifiWasConnected = true; status("WIFI_CONNECTED"); }
        sendClock(now);
        sendC1(now, false); /* 15 s throttle; self-heals a dropped C1 frame. */
        upload(now);
    }
    delay(1); // Yields to ESP Wi-Fi/watchdog; STM32 ADC runs independently.
}
