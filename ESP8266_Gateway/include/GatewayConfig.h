#ifndef GATEWAY_CONFIG_H
#define GATEWAY_CONFIG_H

#ifdef GATEWAY_BUILD_CHECK
#include "../../Tests/gateway_stubs/BuildCheckConfig.h"
#endif

// EDIT include/GatewayConfig.local.h for this device's Wi-Fi and VPS settings.
// This machine already has WIFI_SSID, WIFI_PASSWORD, REPORT_HTTPS_URL and
// REPORT_DEVICE_TOKEN filled there; it is loaded BEFORE the fallbacks below.
// Local Wi-Fi/device credentials are excluded from source-control and web releases.
// Host tests predefine WIFI_SSID, so they never read the real local credentials.
#if !defined(GATEWAY_BUILD_CHECK) && !defined(WIFI_SSID)
#if __has_include("GatewayConfig.local.h")
#include "GatewayConfig.local.h"
#endif
#endif

// Fallbacks only when GatewayConfig.local.h did not define the value.
// Change the local file, then rebuild/flash ESP-01S (not the STM32 HEX).
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// The local file uses https://lifi.hsn811.top/api/v1/messages and its device token.
#ifndef REPORT_HTTPS_URL
#define REPORT_HTTPS_URL ""
#endif
#ifndef REPORT_DEVICE_TOKEN
#define REPORT_DEVICE_TOKEN ""
#endif

// PEM ROOT CA that validates your VPS HTTPS certificate. Leave empty until
// your domain/certificate is chosen. No setInsecure() fallback is used.
#ifndef REPORT_ROOT_CA_PEM
static const char REPORT_ROOT_CA_PEM[] = R"PEM(
)PEM";
#endif

/* Always-on configuration access point + mDNS name for the Web UI.
 * Defaults are safe public values; override in GatewayConfig.local.h. */
#ifndef CONFIG_AP_SSID
#define CONFIG_AP_SSID "LiFi-GW"
#endif
#ifndef CONFIG_AP_PASSWORD
#define CONFIG_AP_PASSWORD "lifi1234"
#endif
#ifndef MDNS_INSTANCE
#define MDNS_INSTANCE "lifi-gw"
#endif

/* Self-healing resend of the C1 config frame while the link is online. */
#define C1_RESEND_MS 15000UL

#define NTP_SERVER_PRIMARY "ntp.aliyun.com"
#define NTP_SERVER_SECONDARY "ntp.tencent.com"
#define REPORT_RETRY_MIN_MS 5000UL
#define REPORT_RETRY_MAX_MS 60000UL
#define RTC_SYNC_INTERVAL_MS 3600000UL
#endif
