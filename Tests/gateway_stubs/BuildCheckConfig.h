/* Compile/test only: reserved .invalid host, no real Wi-Fi or credentials.
 * Dummy PEM is NOT a valid trust anchor and must not be flashed as a config. */
#define WIFI_SSID "BUILD_CHECK_NO_WIFI"
#define WIFI_PASSWORD "BUILD_CHECK_NO_PASSWORD"
#define REPORT_HTTPS_URL "https://compile-check.invalid/api/v1/messages"
#define REPORT_DEVICE_TOKEN "BUILD_CHECK_NO_TOKEN"
#define REPORT_ROOT_CA_PEM "-----BEGIN CERTIFICATE-----\nTEST_ONLY\n-----END CERTIFICATE-----\n"
