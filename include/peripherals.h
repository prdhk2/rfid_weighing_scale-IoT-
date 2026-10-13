#pragma once

#include <Arduino.h>

#include "button_logic.h"
#include "rfid_frame.h"

// Scale indicator (UART2), RFID readers (RS485 on UART1) and the three front
// panel buttons, all read without blocking from the loop task. The MQTT task
// reads counters and the latest weight through the *Snapshot() calls, which
// copy under a lock.

// ------------------------------------------------------------------ Scale
struct ScaleSnapshot {
  bool everReceived;
  bool live;         // a reading arrived within Config::SCALE_STALE_MS
  float weightKg;    // last reading (meaningful only if everReceived)
  uint32_t ageMs;    // since the last reading; UINT32_MAX if never
  uint32_t frames;   // lines parsed as a weight
  uint32_t rejected; // lines without a usable number
};

void beginScale();
void updateScale(uint32_t now);
ScaleSnapshot scaleSnapshot(uint32_t now);

// ------------------------------------------------------------------- RFID
enum class RfidEvent : uint8_t {
  NONE,
  CARD,        // a valid card frame: see rfidCard()
  REPEAT,      // the same frame again within Config::RFID_REPEAT_IGNORE_MS
  BAD_FORMAT,  // frame that does not parse (see rfid_frame.h)
  OVERFLOW,    // frame too long
  TIMEOUT,     // STX without ETX within Config::RFID_FRAME_TIMEOUT_MS
};

struct RfidSnapshot {
  uint32_t cards;
  uint32_t rejected;
  uint32_t lastCardAgeMs;  // UINT32_MAX if none since boot
};

void beginRfid();
// Pumps the bus; returns at most one event per call.
RfidEvent updateRfid(uint32_t now);
// The card from the last CARD event.
const RfidFrame::Card &rfidCard();
// Drops anything half-received or still buffered - a card tapped while the
// scale is busy with the previous one must not be acted on later.
void rfidDiscardPending();
bool rfidReceiving();
RfidSnapshot rfidSnapshot(uint32_t now);

// ---------------------------------------------------------------- Buttons
struct ButtonEvents {
  DebouncedButton::Events in;
  DebouncedButton::Events out;
  DebouncedButton::Events reset;
};

void beginButtons();
ButtonEvents updateButtons(uint32_t now);
// A button held through a screen change acts on nothing until released.
void suppressButtonsUntilRelease();
