#pragma once

#include <RTClib.h>
#include "config.h"

// Fungsi untuk menyesuaikan waktu dengan offset
inline DateTime getAdjustedTime(const DateTime& now) {
  return now + TimeSpan(
    0, 
    TIMEZONE_OFFSET_HOURS, 
    TIME_ADJUST_MINUTES, 
    TIME_ADJUST_SECONDS
  );
}

// Enum untuk status mode penyesuaian waktu
enum TimeAdjustState {
    IDLE,
    ADJUST_HOUR,
    ADJUST_MINUTE,
    ADJUST_SECOND,
    SAVE_TIME
};

// Variabel global untuk penyesuaian waktu
extern int adjHour;
extern int adjMinute;
extern int adjSecond;
extern bool blinkState;
extern unsigned long lastBlink;
extern unsigned long resetPressTime;
extern TimeAdjustState adjustState;
