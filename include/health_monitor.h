#pragma once

#include <Arduino.h>

#include "debug_log.h"

// Three jobs, all about a device that is out of reach in the field:
//
// 1. Black box. Every logf() line is also copied into a small ring kept in
//    RTC memory (RTC_NOINIT), which survives a panic, a watchdog reset, a
//    brownout and ESP.restart() - everything except a full power loss. On the
//    next boot the previous boot's last lines are kept aside and published on
//    the MQTT log topic as "[PREV] ..." once the broker is reachable again, so
//    the last thing the device saw before it went down is visible remotely.
//
// 2. Connectivity supervisor. The task watchdog only guards loop(); a stuck
//    Wi-Fi stack or wedged MQTT/TLS task leaves loop() healthy and the device
//    unreachable forever. updateHealthMonitor() reboots it once MQTT has been
//    down long enough (see net_supervisor_logic.h for the policy) - while the
//    scale is idle, and logging why first so the reason lands in the black box.
//
// 3. Deferred restarts. A remote RESTART or a finished OTA never reboots in
//    the middle of a weighing: requestRestart() only records the wish, and
//    the loop task carries it out once the scale is idle (or the wait runs
//    out). The LCD shows why it is restarting.
//
// beginHealthMonitor() must be the first thing setup() calls after
// Serial.begin(): any logf() before it would overwrite the previous boot's
// record.
void beginHealthMonitor();

// Loop task only. idle: no weighing in progress (see appIsIdle()).
void updateHealthMonitor(uint32_t now, bool idle);

// Called by logf() for every line. Safe from any task/core.
void healthMonitorRecord(const char *line);

// Safe from any task. The first request wins; later ones are ignored.
// otaVersion: set when the restart activates a freshly flashed OTA image, so
// the "[OTA] progress stage=rebooting" line goes out right before the reboot.
void requestRestart(const char *reason, uint32_t maxIdleWaitMs, const char *otaVersion = nullptr);
bool restartPending();

// The MQTT task calls this whenever it has a healthy broker session; it
// resets the supervisor's offline clock and its restart back-off.
void healthMonitorMqttHealthy();

// Previous boot's record, for mqtt_client.cpp to publish. Lines are oldest
// first. prevBootSummary() is empty when there was nothing to recover (first
// power-on, or RTC memory lost with the supply).
size_t prevBootLineCount();
const char *prevBootLine(size_t index);
const char *prevBootSummary();
void releasePrevBoot();  // frees the copy once it has been published

uint8_t healthConsecutiveNetRestarts();
