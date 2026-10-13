#pragma once

#include <cstdint>

// Milliseconds from `since` to `now`, both millis() values, wrap-safe.
//
// `since` may have been stamped by another task a moment AFTER the caller
// captured `now` (the loop takes `now` once per pass; the MQTT task on core 0
// keeps stamping its own millis() meanwhile). Plain `now - since` then wraps
// to ~4.29e9 - "MQTT down 71582 min" - and a supervisor acting on it reboots a
// perfectly healthy device mid-weighing. Such a timestamp counts as 0 here.
// Unit-tested natively in test/test_elapsed/.
//
// Valid for intervals up to ~24.8 days (INT32_MAX ms); every caller here
// measures far shorter spans.
inline uint32_t elapsedMs(uint32_t now, uint32_t since) {
  const int32_t delta = static_cast<int32_t>(now - since);
  return delta < 0 ? 0u : static_cast<uint32_t>(delta);
}
