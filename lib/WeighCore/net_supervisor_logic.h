#pragma once

#include <cstdint>

// Pure decision logic for health_monitor.cpp's connectivity supervisor. No
// ESP32 dependency, unit-tested natively in test/test_net_supervisor_logic/.
//
// Why it exists: the task watchdog only guards loop(). A stuck Wi-Fi stack, a
// wedged MQTT/TLS session or a heap too fragmented for another TLS handshake
// leaves loop() perfectly healthy - so nothing ever resets the device, and it
// stays unreachable (no status, no remote command, no OTA) until someone
// drives out and power-cycles it. This answers "has it been unreachable long
// enough that a reboot is the right call".
namespace NetSupervisor {

constexpr uint32_t MINUTE_MS = 60000UL;

// First restart after this long without a healthy MQTT session. Each
// consecutive supervisor restart that still does not bring MQTT back doubles
// it (15 -> 30 -> 60 -> 120 min, then stays), so a site router that is down
// for a day costs a reboot every two hours, not every fifteen minutes.
constexpr uint32_t BASE_OFFLINE_MS = 15UL * MINUTE_MS;
constexpr uint8_t MAX_BACKOFF_SHIFT = 3;

// A reboot in the middle of a weighing (card read, upload in flight, operator
// adjusting the clock) loses that transaction. Past the threshold the
// supervisor waits for an idle moment; this is how much longer it is willing
// to wait before rebooting anyway.
constexpr uint32_t BUSY_GRACE_MS = 30UL * MINUTE_MS;

enum class Action : uint8_t { NONE, RESTART, RESTART_FORCED };

uint32_t offlineThresholdMs(uint8_t consecutiveRestarts);

// offlineMs: how long since the MQTT task last saw a connected session.
// busy: a weighing transaction is in progress right now.
Action decide(uint32_t offlineMs, bool busy, uint8_t consecutiveRestarts);

}  // namespace NetSupervisor
