#pragma once

#include <Arduino.h>

#include "clock.h"
#include "http_outcome.h"
#include "rfid_frame.h"

// The 128x64 ST7920 LCD. Every call draws one complete screen; app.cpp decides
// when (only on change, so a refresh never costs loop time it does not need).
// Loop task only. Layouts follow the original firmware's screens.

void beginUi();

void uiBoot();

struct HomeView {
  LocalTime time;
  // Clock setting: 0 = off, 1 = hour, 2 = minute, 3 = second blinking.
  uint8_t adjustField;
  bool blinkOn;
  uint8_t adjHour, adjMinute, adjSecond;
  bool wifiOk;
  const char *footer;
};
void uiHome(const HomeView &view);

void uiWeight(const RfidFrame::Card &card, bool weightLive, float weightKg);

void uiSending(RfidFrame::Direction direction, uint8_t dots);

void uiResult(HttpOutcome::Verdict verdict, int httpCode, RfidFrame::Direction direction);

void uiError(const char *line1, const char *line2);

void uiShowRestarting(const char *what);
