#include "net_supervisor_logic.h"

namespace NetSupervisor {

uint32_t offlineThresholdMs(uint8_t consecutiveRestarts) {
  uint8_t shift = consecutiveRestarts < MAX_BACKOFF_SHIFT ? consecutiveRestarts
                                                          : MAX_BACKOFF_SHIFT;
  return BASE_OFFLINE_MS << shift;
}

Action decide(uint32_t offlineMs, bool busy, uint8_t consecutiveRestarts) {
  uint32_t threshold = offlineThresholdMs(consecutiveRestarts);
  if (offlineMs < threshold) return Action::NONE;
  if (!busy) return Action::RESTART;
  if (offlineMs >= threshold + BUSY_GRACE_MS) return Action::RESTART_FORCED;
  return Action::NONE;
}

}  // namespace NetSupervisor
