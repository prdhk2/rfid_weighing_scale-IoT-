#pragma once

#include <RTClib.h>
#include "config.h"

inline DateTime getAdjustedTime(const DateTime& now) {
  return now + TimeSpan(
    0, 
    TIMEZONE_OFFSET_HOURS, 
    TIME_ADJUST_MINUTES, 
    TIME_ADJUST_SECONDS
  );
}