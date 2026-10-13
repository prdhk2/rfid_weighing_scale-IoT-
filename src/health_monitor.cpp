#include "health_monitor.h"

#include <WiFi.h>
#include <cstring>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "mqtt_client.h"
#include "net_supervisor_logic.h"
#include "network_service.h"
#include "reset_reason.h"
#include "ui.h"

namespace {

constexpr uint32_t BOX_MAGIC = 0x54524B42u;  // "TRKB"
constexpr size_t BOX_LINES = 24;
constexpr size_t REASON_CAPACITY = 64;
constexpr uint32_t HEALTH_LINE_INTERVAL_MS = 60000;
constexpr uint32_t OFFLINE_WARN_INTERVAL_MS = 60000;
// Long enough for the MQTT task (log drain every 1.5 s) to get the last lines
// out before the reboot.
constexpr uint32_t RESTART_FLUSH_MS = 2500;

struct BlackBox {
  uint32_t magic;
  uint32_t bootSeq;
  uint32_t head;      // total lines ever written this boot; slot = head % BOX_LINES
  uint32_t uptimeS;   // refreshed every loop pass, so a crash leaves the last value
  uint32_t minHeap;
  uint8_t netRestarts;  // consecutive supervisor restarts without MQTT coming back
  char reason[REASON_CAPACITY];
  char lines[BOX_LINES][LOG_LINE_CAPACITY + 1];
};

RTC_NOINIT_ATTR BlackBox box;

portMUX_TYPE boxLock = portMUX_INITIALIZER_UNLOCKED;
bool boxReady = false;

BlackBox *prev = nullptr;  // heap copy of the previous boot's box, until published
char prevSummary[LOG_LINE_CAPACITY + 1] = "";

volatile uint32_t lastMqttHealthyMs = 0;
uint32_t lastHealthLineMs = 0;
uint32_t lastOfflineWarnMs = 0;

// Deferred restart, written by any task under restartLock.
portMUX_TYPE restartLock = portMUX_INITIALIZER_UNLOCKED;
volatile bool restartRequested = false;
uint32_t restartRequestedAtMs = 0;
uint32_t restartMaxWaitMs = 0;
char restartReason[REASON_CAPACITY] = "";
char restartOtaVersion[32] = "";

bool boxLooksValid(const BlackBox &b) {
  return b.magic == BOX_MAGIC && b.netRestarts <= 250 && b.bootSeq < 0x7FFFFFFFu;
}

void setBoxReason(const char *reason) {
  if (!boxReady) return;
  portENTER_CRITICAL(&boxLock);
  strncpy(box.reason, reason, REASON_CAPACITY - 1);
  box.reason[REASON_CAPACITY - 1] = '\0';
  portEXIT_CRITICAL(&boxLock);
}

// Loop task only. Never returns.
void restartNow(const char *reason, const char *otaVersion, bool forced) {
  uiShowRestarting(otaVersion && otaVersion[0] ? "Update firmware" : "Restart sistem");
  if (otaVersion && otaVersion[0]) {
    // The one line the server's OTA tracker waits for (DeviceTelemetryIngestor
    // OTA_PROGRESS_PATTERN). Sent only now, not when the flash finished: a
    // device still reporting its old version 20 s after "rebooting" is marked
    // rolled back, and the wait for an idle scale can be longer than that.
    logf("[OTA] progress stage=rebooting pct=100 detail=%s", otaVersion);
  }
  setBoxReason(reason);
  logf("[HEALTH] rebooting: %s%s", reason, forced ? " (forced, scale was busy)" : "");

  const uint32_t start = millis();
  while (millis() - start < RESTART_FLUSH_MS) {
    esp_task_wdt_reset();
    delay(50);
  }
  Serial.flush();
  ESP.restart();
}

}  // namespace

void beginHealthMonitor() {
  const int resetCode = static_cast<int>(esp_reset_reason());
  uint32_t bootSeq = 1;
  uint8_t netRestarts = 0;

  // Power-on leaves RTC memory as garbage; the magic check catches that.
  if (resetCode != ResetReason::POWERON && boxLooksValid(box)) {
    bootSeq = box.bootSeq + 1;
    netRestarts = box.netRestarts;
    box.reason[REASON_CAPACITY - 1] = '\0';
    for (auto &line : box.lines) line[LOG_LINE_CAPACITY] = '\0';

    prev = static_cast<BlackBox *>(malloc(sizeof(BlackBox)));
    if (prev) memcpy(prev, &box, sizeof(BlackBox));
    snprintf(prevSummary, sizeof(prevSummary),
             "[PREV] boot #%lu ended after %lus, reset=%.10s, reason=%.32s, minheap=%lu",
             static_cast<unsigned long>(box.bootSeq), static_cast<unsigned long>(box.uptimeS),
             ResetReason::name(resetCode), box.reason[0] ? box.reason : "none recorded",
             static_cast<unsigned long>(box.minHeap));
    for (char *c = prevSummary; *c; ++c) {
      if (*c == '"' || *c == '\\') *c = '\'';
      else if (static_cast<unsigned char>(*c) < 0x20) *c = ' ';
    }
    Serial.println(prevSummary);
  }

  memset(&box, 0, sizeof(box));
  box.magic = BOX_MAGIC;
  box.bootSeq = bootSeq;
  box.netRestarts = netRestarts;
  boxReady = true;

  lastMqttHealthyMs = millis();
}

void healthMonitorRecord(const char *line) {
  if (!boxReady) return;
  portENTER_CRITICAL(&boxLock);
  char *slot = box.lines[box.head % BOX_LINES];
  strncpy(slot, line, LOG_LINE_CAPACITY);
  slot[LOG_LINE_CAPACITY] = '\0';
  ++box.head;
  portEXIT_CRITICAL(&boxLock);
}

void requestRestart(const char *reason, uint32_t maxIdleWaitMs, const char *otaVersion) {
  bool accepted = false;
  portENTER_CRITICAL(&restartLock);
  if (!restartRequested) {
    strncpy(restartReason, reason, sizeof(restartReason) - 1);
    restartReason[sizeof(restartReason) - 1] = '\0';
    if (otaVersion) {
      strncpy(restartOtaVersion, otaVersion, sizeof(restartOtaVersion) - 1);
      restartOtaVersion[sizeof(restartOtaVersion) - 1] = '\0';
    } else {
      restartOtaVersion[0] = '\0';
    }
    restartRequestedAtMs = millis();
    restartMaxWaitMs = maxIdleWaitMs;
    restartRequested = true;
    accepted = true;
  }
  portEXIT_CRITICAL(&restartLock);

  if (accepted) {
    logf("[HEALTH] restart requested (%s), waiting up to %lus for the scale to be idle", reason,
         static_cast<unsigned long>(maxIdleWaitMs / 1000UL));
  }
}

bool restartPending() { return restartRequested; }

void healthMonitorMqttHealthy() {
  lastMqttHealthyMs = millis();
  box.netRestarts = 0;
}

size_t prevBootLineCount() {
  if (!prev) return 0;
  return prev->head < BOX_LINES ? prev->head : BOX_LINES;
}

const char *prevBootLine(size_t index) {
  size_t count = prevBootLineCount();
  if (index >= count) return "";
  size_t first = prev->head - count;  // oldest line still in the ring
  return prev->lines[(first + index) % BOX_LINES];
}

const char *prevBootSummary() { return prevSummary; }

void releasePrevBoot() {
  free(prev);
  prev = nullptr;
  prevSummary[0] = '\0';
}

uint8_t healthConsecutiveNetRestarts() { return box.netRestarts; }

void updateHealthMonitor(uint32_t now, bool idle) {
  box.uptimeS = now / 1000UL;
  box.minHeap = ESP.getMinFreeHeap();

  if (restartRequested) {
    char reason[REASON_CAPACITY];
    char otaVersion[sizeof(restartOtaVersion)];
    uint32_t requestedAt;
    uint32_t maxWait;
    portENTER_CRITICAL(&restartLock);
    memcpy(reason, restartReason, sizeof(reason));
    memcpy(otaVersion, restartOtaVersion, sizeof(otaVersion));
    requestedAt = restartRequestedAtMs;
    maxWait = restartMaxWaitMs;
    portEXIT_CRITICAL(&restartLock);

    const bool timedOut = now - requestedAt >= maxWait;
    if (idle || timedOut) restartNow(reason, otaVersion, !idle);
    return;  // the supervisor has nothing to add while a restart is pending
  }

  if (!Config::mqttConfigured() || !Config::wifiConfigured()) return;

  const uint32_t offlineMs = now - lastMqttHealthyMs;

  // Early warning, once a minute: the numbers that predict the next failure
  // (shrinking largest heap block = TLS will soon fail; low stack headroom =
  // a crash on the next long log line or OTA). Ends up in the black box too.
  if (now - lastHealthLineMs >= HEALTH_LINE_INTERVAL_MS) {
    lastHealthLineMs = now;
    logf("[HEALTH] mqtt=%s off=%lus wifi=%s rssi=%d heap=%u minheap=%u maxblk=%u",
         mqttConnected() ? "UP" : "DOWN", static_cast<unsigned long>(offlineMs / 1000UL),
         wifiConnected() ? "UP" : "DOWN", static_cast<int>(WiFi.RSSI()),
         static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
         static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }

  const uint8_t restarts = box.netRestarts;
  const uint32_t thresholdMs = NetSupervisor::offlineThresholdMs(restarts);

  if (offlineMs >= 2 * OFFLINE_WARN_INTERVAL_MS &&
      now - lastOfflineWarnMs >= OFFLINE_WARN_INTERVAL_MS) {
    lastOfflineWarnMs = now;
    logf("[HEALTH] MQTT unreachable %lus (wifi=%s); reboot at %lus%s",
         static_cast<unsigned long>(offlineMs / 1000UL), wifiConnected() ? "UP" : "DOWN",
         static_cast<unsigned long>(thresholdMs / 1000UL), idle ? "" : " (waiting for idle)");
  }

  switch (NetSupervisor::decide(offlineMs, !idle, restarts)) {
    case NetSupervisor::Action::NONE:
      return;
    case NetSupervisor::Action::RESTART:
    case NetSupervisor::Action::RESTART_FORCED: {
      if (box.netRestarts < 250) ++box.netRestarts;
      char reason[REASON_CAPACITY];
      snprintf(reason, sizeof(reason), "supervisor: mqtt down %lumin, wifi=%s",
               static_cast<unsigned long>(offlineMs / NetSupervisor::MINUTE_MS),
               wifiConnected() ? "UP" : "DOWN");
      restartNow(reason, nullptr, !idle);
    }
  }
}
