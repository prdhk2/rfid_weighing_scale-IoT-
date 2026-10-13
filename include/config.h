#pragma once

#include <Arduino.h>

// ESP32-S3 truck weighbridge ("timbangan truk") - Depok.
//
//   Scale indicator --RS232 (RX18, inverted, 9600)----> ESP32-S3
//   RFID readers    --RS485 (RX44, 9600, STX..ETX)----> ESP32-S3
//                     slave 1 = entry (masuk), slave 2 = exit (keluar)
//   DS3231 RTC (I2C 8/9), ST7920 128x64 LCD, buttons Masuk/Keluar/Reset
//   ESP32-S3 --Wi-Fi--> monitoring.xampah.com
//            HTTPS: POST /api/weightDataIn, /api/weightDataOut
//            MQTT:  device status, live debug log, remote commands, OTA
//
// Every pin below is the one the board was built and wired with - unchanged
// from the original firmware.
namespace Config {

// ------------------------------------------------------------------- Pins
namespace Pins {

// ST7920 serial mode via U8g2 software SPI. The LCD reset is not connected.
constexpr uint8_t LCD_CLOCK = 7;
constexpr uint8_t LCD_DATA = 6;
constexpr uint8_t LCD_CS = 5;

constexpr int8_t RS485_RX = 44;   // UART1 (free: the console is native USB)
constexpr uint8_t RS485_DE_RE = 35;  // held LOW: receive only - see platformio.ini
constexpr int8_t SCALE_RX = 18;   // UART2, RS232 level, signal inverted

constexpr uint8_t RTC_SDA = 8;
constexpr uint8_t RTC_SCL = 9;

// Active low, internal pull-up.
constexpr uint8_t IN_BUTTON = 2;     // "Masuk": resend as weigh-in / +1 when setting the clock
constexpr uint8_t OUT_BUTTON = 4;    // "Keluar": resend as weigh-out
constexpr uint8_t RESET_BUTTON = 42; // back to home / hold 3 s: set the clock

}  // namespace Pins

constexpr uint32_t CONSOLE_BAUD = 115200;  // native USB CDC
constexpr uint32_t LCD_BUS_CLOCK_HZ = 100000;
constexpr uint32_t SCALE_BAUD = 9600;
constexpr bool SCALE_RX_INVERTED = true;
constexpr uint32_t RS485_BAUD = 9600;
constexpr uint32_t I2C_CLOCK_HZ = 100000;
// Bigger than the 256-byte default: an LCD refresh can hold the loop for a
// few tens of ms, and nothing that arrives meanwhile may be lost.
constexpr size_t UART_RX_BUFFER = 1024;

// ------------------------------------------------------------- Behaviour
// No reading from the scale for this long: the weight shows "N/A" and
// nothing is sent (the last number is no longer what is on the scale).
constexpr uint32_t SCALE_STALE_MS = 5000;
// Uploads need more than this on the scale, as the original firmware did
// (it refused <= 0.1 kg).
constexpr float MIN_SEND_WEIGHT_KG = 0.1f;

constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;
constexpr uint32_t CLOCK_ADJUST_HOLD_MS = 3000;  // hold Reset on the home screen
constexpr uint32_t CLOCK_ADJUST_IDLE_MS = 60000; // no input: leave clock setting unsaved

constexpr uint32_t RFID_FRAME_TIMEOUT_MS = 1000;  // STX without ETX
// The same frame again this soon is the reader repeating one tap, not a
// second weighing.
constexpr uint32_t RFID_REPEAT_IGNORE_MS = 10000;

constexpr uint32_t WEIGHT_SCREEN_TIMEOUT_MS = 120000;  // back to home
constexpr uint32_t RESULT_SCREEN_MS = 3000;
constexpr uint32_t ERROR_SCREEN_MS = 5000;
// Backstop only: the API task bounds each upload itself (see api_client.cpp).
constexpr uint32_t SENDING_TIMEOUT_MS = 45000;

// ----------------------------------------------------------------- Network
// Build secrets come from secrets.ini (see secrets.ini.example and
// scripts/load_secrets.py), never from this file.
#ifdef DP_WIFI_SSID
constexpr char WIFI_SSID[] = DP_WIFI_SSID;
#else
constexpr char WIFI_SSID[] = "";
#endif
#ifdef DP_WIFI_PASS
constexpr char WIFI_PASSWORD[] = DP_WIFI_PASS;
#else
constexpr char WIFI_PASSWORD[] = "";
#endif
constexpr uint32_t WIFI_RETRY_MS = 10000;

// Base of every upload; https:// is verified against ISRG Root X1. The server
// redirects plain http:// to https://, and a redirected POST is a failure.
#ifdef DP_API_BASE_URL
constexpr char API_BASE_URL[] = DP_API_BASE_URL;
#else
constexpr char API_BASE_URL[] = "https://monitoring.xampah.com/api";
#endif
constexpr char WEIGHT_IN_PATH[] = "/weightDataIn";
constexpr char WEIGHT_OUT_PATH[] = "/weightDataOut";

// Sent as X-API-KEY when set. The weight routes do not require one today.
#ifdef DP_API_KEY
constexpr char API_KEY[] = DP_API_KEY;
#else
constexpr char API_KEY[] = "";
#endif

constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t HTTP_TIMEOUT_MS = 10000;
constexpr uint32_t TLS_HANDSHAKE_TIMEOUT_S = 10;
// Automatic retries only for failures where the request surely never reached
// the server (see http_outcome.h), and only within this budget.
constexpr uint8_t HTTP_MAX_ATTEMPTS = 3;
constexpr uint32_t HTTP_RETRY_DELAY_MS = 700;
constexpr uint32_t HTTP_RETRY_BUDGET_MS = 15000;

#ifdef DP_DEVICE_ID
constexpr char DEVICE_ID[] = DP_DEVICE_ID;
#else
constexpr char DEVICE_ID[] = "depok-truk-01";
#endif

// Fleet this firmware belongs to: MQTT topic prefix, and an OTA command for
// any other site is refused. Must match config/fleet.php on the server.
constexpr char SITE[] = "depok_truk";

constexpr char FIRMWARE_VERSION[] = "3.0.1";

constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.google.com";
constexpr long GMT_OFFSET_SEC = 7L * 3600L;  // WIB; the RTC keeps local time
constexpr int DAYLIGHT_OFFSET_SEC = 0;
// The RTC is corrected from NTP at the first sync after boot and then this
// often, whenever it is off by at least RTC_SYNC_MIN_DRIFT_S.
constexpr uint32_t RTC_SYNC_INTERVAL_MS = 6UL * 3600UL * 1000UL;
constexpr long RTC_SYNC_MIN_DRIFT_S = 2;

// -------------------------------------------------------------------- MQTT
#ifdef DP_MQTT_HOST
constexpr char MQTT_BROKER_HOST[] = DP_MQTT_HOST;
#else
constexpr char MQTT_BROKER_HOST[] = "monitoring.xampah.com";
#endif
#ifdef DP_MQTT_PORT
constexpr uint16_t MQTT_BROKER_PORT = DP_MQTT_PORT;
#else
constexpr uint16_t MQTT_BROKER_PORT = 8883;
#endif
#ifdef DP_MQTT_USER
constexpr char MQTT_USERNAME[] = DP_MQTT_USER;
#else
constexpr char MQTT_USERNAME[] = "";
#endif
#ifdef DP_MQTT_PASS
constexpr char MQTT_PASSWORD[] = DP_MQTT_PASS;
#else
constexpr char MQTT_PASSWORD[] = "";
#endif

#ifdef DP_RELEASE_BUILD
#if !defined(DP_MQTT_USER) || !defined(DP_MQTT_PASS)
#error "Release build needs DP_MQTT_USER and DP_MQTT_PASS (secrets.ini) - without them the device loses MQTT and can no longer be updated over the air."
#endif
#if !defined(DP_WIFI_SSID) || !defined(DP_WIFI_PASS)
#error "Release build needs DP_WIFI_SSID and DP_WIFI_PASS (secrets.ini) - without them the device never gets online."
#endif
#endif

constexpr uint32_t MQTT_RECONNECT_MS = 10000;
constexpr uint32_t MQTT_STATUS_INTERVAL_MS = 10000;
constexpr uint32_t MQTT_LOG_DRAIN_INTERVAL_MS = 1500;

#ifdef MQTT_MAX_PACKET_SIZE
constexpr size_t MQTT_PACKET_SIZE = MQTT_MAX_PACKET_SIZE;
#else
constexpr size_t MQTT_PACKET_SIZE = 2048;
#endif

// An OTA image counts as healthy once it published its MQTT status after
// staying up this long (see OtaManager::confirmBootHealthy()).
constexpr uint32_t BOOT_HEALTHY_AFTER_MS = 60000;

// A requested reboot (remote RESTART, finished OTA) waits for the scale to be
// idle - home screen, no upload in flight - for at most this long.
constexpr uint32_t RESTART_IDLE_WAIT_MS = 60000;
constexpr uint32_t OTA_REBOOT_IDLE_WAIT_MS = 180000;

constexpr uint8_t WATCHDOG_TIMEOUT_S = 30;
constexpr uint32_t CONSOLE_SUMMARY_INTERVAL_MS = 60000;

inline bool wifiConfigured() { return WIFI_SSID[0] != '\0'; }
inline bool apiKeyConfigured() { return API_KEY[0] != '\0'; }
inline bool mqttConfigured() {
  return MQTT_BROKER_HOST[0] != '\0' && MQTT_USERNAME[0] != '\0' && MQTT_PASSWORD[0] != '\0';
}

}  // namespace Config
