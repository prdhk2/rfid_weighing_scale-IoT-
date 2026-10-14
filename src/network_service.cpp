#include "network_service.h"

#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "debug_log.h"
#include "elapsed.h"

namespace {

constexpr time_t WALL_CLOCK_SANITY_EPOCH = 1700000000;  // 2023-11-14
constexpr uint32_t NOT_CONNECTED_LOG_MS = 30000;

uint32_t lastAttemptMs = 0;
uint32_t lastNotConnectedLogMs = 0;
bool wasConnected = false;
bool ntpStarted = false;
volatile uint32_t reconnects = 0;

// Written by the Wi-Fi event task, read by the loop.
volatile uint8_t lastDisconnectReason = 0;
volatile uint32_t disconnectEvents = 0;

// The reasons that actually occur in the field, in words a technician can act on.
const char *reasonText(uint8_t reason) {
  switch (reason) {
    case 0: return "none yet";
    case 2: return "AUTH_EXPIRE";
    case 8: return "ASSOC_LEAVE";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT (wrong password?)";
    case 200: return "BEACON_TIMEOUT (signal lost)";
    case 201: return "NO_AP_FOUND (SSID not visible / too far)";
    case 202: return "AUTH_FAIL (wrong password?)";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT (wrong password / weak signal)";
    case 205: return "CONNECTION_FAIL";
    default: return "other";
  }
}

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastDisconnectReason = info.wifi_sta_disconnected.reason;
    ++disconnectEvents;
  }
}

void startConnect(uint32_t now) {
  WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);
  lastAttemptMs = now;
}

}  // namespace

void beginNetworkService() {
  if (!Config::wifiConfigured()) {
    logf("[WIFI] no SSID configured (secrets.ini / config.h), staying offline");
    WiFi.mode(WIFI_OFF);
    return;
  }
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.persistent(false);               // credentials come from the build, not NVS
  WiFi.setHostname(Config::DEVICE_ID);  // before mode(): applied when STA starts
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                 // predictable latency for uploads and MQTT
  // Strongest TX power: the weighbridge sits at the edge of the site's Wi-Fi.
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setAutoReconnect(true);
  logf("[WIFI] connecting to '%s'", Config::WIFI_SSID);
  startConnect(millis());
}

void updateNetworkService(uint32_t now) {
  if (!Config::wifiConfigured()) return;

  const bool connected = WiFi.status() == WL_CONNECTED;

  if (connected != wasConnected) {
    wasConnected = connected;
    if (connected) {
      logf("[WIFI] connected, ip=%s rssi=%d", WiFi.localIP().toString().c_str(),
           static_cast<int>(WiFi.RSSI()));
    } else {
      logf("[WIFI] disconnected, reason=%u %s", lastDisconnectReason,
           reasonText(lastDisconnectReason));
      // Give the core's own auto-reconnect a full window before stepping in.
      lastAttemptMs = now;
      lastNotConnectedLogMs = now;
    }
  }

  if (!connected) {
    if (elapsedMs(now, lastNotConnectedLogMs) >= NOT_CONNECTED_LOG_MS) {
      lastNotConnectedLogMs = now;
      logf("[WIFI] still not connected to '%s': status=%d last_reason=%u %s, attempts=%lu",
           Config::WIFI_SSID, static_cast<int>(WiFi.status()), lastDisconnectReason,
           reasonText(lastDisconnectReason), static_cast<unsigned long>(disconnectEvents));
    }
    // A fresh begin() only after a whole window without success. Restarting
    // the attempt more often than one association + DHCP can take (several
    // seconds at -75 dBm) aborts every attempt half-way and the device never
    // gets back on. Both calls return immediately.
    if (elapsedMs(now, lastAttemptMs) >= Config::WIFI_RETRY_MS) {
      WiFi.disconnect();
      startConnect(now);
      ++reconnects;
    }
    return;
  }

  // SNTP starts once, the first time Wi-Fi is up, and keeps syncing in the
  // background (lwIP's SNTP client re-syncs on its own interval).
  if (!ntpStarted) {
    configTime(Config::GMT_OFFSET_SEC, Config::DAYLIGHT_OFFSET_SEC, Config::NTP_SERVER_1,
               Config::NTP_SERVER_2);
    ntpStarted = true;
  }
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
bool wallClockReady() { return time(nullptr) >= WALL_CLOCK_SANITY_EPOCH; }
uint32_t wifiReconnects() { return reconnects; }
