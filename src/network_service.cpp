#include "network_service.h"

#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "debug_log.h"

namespace {

constexpr time_t WALL_CLOCK_SANITY_EPOCH = 1700000000;  // 2023-11-14

uint32_t lastAttemptMs = 0;
bool wasConnected = false;
bool ntpStarted = false;
volatile uint32_t reconnects = 0;

}  // namespace

void beginNetworkService() {
  if (!Config::wifiConfigured()) {
    logf("[WIFI] DP_WIFI_SSID not set (secrets.ini), staying offline");
    WiFi.mode(WIFI_OFF);
    return;
  }
  WiFi.persistent(false);               // credentials come from the build, not NVS
  WiFi.setHostname(Config::DEVICE_ID);  // before mode(): applied when STA starts
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                 // predictable latency for uploads and MQTT
  WiFi.setAutoReconnect(true);
  WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);
  lastAttemptMs = millis();
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
      logf("[WIFI] disconnected, retrying every %lus",
           static_cast<unsigned long>(Config::WIFI_RETRY_MS / 1000UL));
      lastAttemptMs = now;
    }
  }

  if (!connected) {
    // Auto-reconnect usually brings the link back by itself; a fresh begin()
    // on a fixed interval covers the cases it does not (AP rebooted with a
    // new channel, auth state stuck). Both return immediately.
    if (now - lastAttemptMs >= Config::WIFI_RETRY_MS) {
      WiFi.disconnect();
      WiFi.begin(Config::WIFI_SSID, Config::WIFI_PASSWORD);
      lastAttemptMs = now;
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
