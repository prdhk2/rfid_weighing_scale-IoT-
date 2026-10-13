#pragma once

#include <Arduino.h>

#include "rfid_frame.h"

// The operator flow, a non-blocking state machine on the loop task:
//
//   HOME ──card tap──> SENDING ──result──> RESULT (3 s) ──> WEIGHT
//     ^                   ^                                   │
//     │                   └──── Masuk / Keluar button ────────┤
//     └────── Reset button, 2 min idle, or a new card tap ────┘
//
//   HOME:    clock; hold Reset 3 s to set it (Masuk = +1, hold Reset = next).
//   SENDING: upload in flight on the API task; the screen stays responsive.
//   RESULT:  server's verdict ("Data Terkirim!", "Sudah Tercatat", ...).
//   WEIGHT:  the card's driver/plate/customer and the live weight; Masuk or
//            Keluar re-sends with the weight now on the scale.
//   ERROR:   bad card frame, no/too low weight, clock invalid (5 s), then
//            back to where it came from.
//
// A card is accepted on HOME and WEIGHT; while SENDING/RESULT/ERROR anything
// on the RFID bus is discarded, so a tap is never acted on late with the
// wrong weight.
void beginApp();
void updateApp(uint32_t now);

// Nothing in progress: home screen, not setting the clock, no upload in
// flight, no card frame half-received. Restarts wait for this.
bool appIsIdle();

struct AppStatus {
  const char *stateName;
  bool hasCard;
  RfidFrame::Direction direction;
  int32_t driverId;
  int32_t customerId;
  char plate[RfidFrame::MAX_PLATE + 1];
  // The last finished upload.
  bool lastSendValid;
  RfidFrame::Direction lastSendDirection;
  float lastSendWeightKg;
  int lastSendHttp;
  uint32_t lastSendAgeMs;
};

// Safe from any task (copied under a lock).
AppStatus appStatus();
