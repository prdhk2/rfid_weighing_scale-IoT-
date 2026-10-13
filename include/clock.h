#pragma once

#include <Arduino.h>

// Wall-clock time for the LCD and for time_in/time_out, in local time (WIB).
//
// Source, in order: the DS3231 RTC when it answers with a sane date, else the
// NTP-synced system clock. When neither is trustworthy the time is reported
// invalid - the original firmware then made up "2024-01-.." timestamps, which
// the server happily stored. Whenever NTP is available the RTC is corrected
// from it (see Config::RTC_SYNC_INTERVAL_MS), so a drifting or reset RTC heals
// itself as soon as the device is online.
//
// All RTC (I2C) access happens on the loop task; clockStatus() is the only
// call meant for other tasks.

struct LocalTime {
  bool valid;
  uint16_t year;
  uint8_t month, day, hour, minute, second;
};

enum class ClockSource : uint8_t { NONE, RTC, NTP };

struct ClockStatus {
  bool rtcPresent;      // DS3231 answers on I2C
  bool rtcSane;         // ... and returns a plausible date
  ClockSource source;
  uint32_t ntpSyncs;    // times the RTC was corrected from NTP
  uint32_t lastNtpSyncEpoch;  // 0 if never
  int32_t lastDriftS;   // RTC - NTP at the last check (0 if never)
};

void beginClock();
void updateClock(uint32_t now);

// Cached, refreshed a few times per second by updateClock().
LocalTime clockNow();

// "YYYY-MM-DD HH:MM:SS" (the format the server's time_in/time_out expect).
// Returns false, leaving out empty, if the time is not valid.
bool clockTimestamp(char *out, size_t outSize);

// Manual setting from the front panel: today's date with a new time of day.
// Returns false if there is no RTC to write to.
bool clockSetTimeOfDay(uint8_t hour, uint8_t minute, uint8_t second);

ClockStatus clockStatus();
