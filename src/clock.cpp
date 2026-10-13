#include "clock.h"

#include <RTClib.h>
#include <Wire.h>
#include <time.h>

#include "config.h"
#include "debug_log.h"
#include "network_service.h"

namespace {

constexpr uint32_t REFRESH_MS = 200;        // LCD shows seconds
constexpr uint32_t RTC_RETRY_MS = 5000;     // probe a missing RTC this often
constexpr uint8_t INSANE_READS_TO_DROP = 5; // consecutive garbage reads = RTC gone

RTC_DS3231 rtc;

bool rtcPresent = false;
bool rtcSane = false;
uint8_t insaneReads = 0;
uint32_t lastProbeMs = 0;
uint32_t lastRefreshMs = 0;
bool refreshedOnce = false;

bool ntpSyncedOnce = false;
uint32_t lastNtpSyncMs = 0;

LocalTime cached = {};

portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
ClockStatus status = {};

bool plausible(const DateTime &t) {
  return t.year() >= 2024 && t.year() <= 2099 && t.month() >= 1 && t.month() <= 12 &&
         t.day() >= 1 && t.day() <= 31 && t.hour() <= 23 && t.minute() <= 59 && t.second() <= 59;
}

void publishStatus(ClockSource source) {
  portENTER_CRITICAL(&statusMux);
  status.rtcPresent = rtcPresent;
  status.rtcSane = rtcSane;
  status.source = source;
  portEXIT_CRITICAL(&statusMux);
}

bool probeRtc() {
  if (!rtc.begin(&Wire)) return false;
  if (rtc.lostPower()) {
    // Oscillator stopped (dead coin cell): the stored time is meaningless.
    // Leave it marked insane until NTP or the operator sets it; the old
    // firmware set it to the build time, which silently produced wrong dates.
    logf("[RTC] lost power - time invalid until NTP sync or manual setting");
  }
  return true;
}

// System clock in local time, as a naive (timezone-free) epoch like RTClib's.
uint32_t systemLocalEpoch() {
  return static_cast<uint32_t>(time(nullptr) + Config::GMT_OFFSET_SEC);
}

void syncRtcFromNtp(uint32_t now) {
  if (!rtcPresent || !wallClockReady()) return;
  if (ntpSyncedOnce && now - lastNtpSyncMs < Config::RTC_SYNC_INTERVAL_MS) return;
  lastNtpSyncMs = now;
  ntpSyncedOnce = true;

  const uint32_t ntpLocal = systemLocalEpoch();
  const DateTime rtcNow = rtc.now();
  const int32_t drift = rtcSane && !rtc.lostPower()
                            ? static_cast<int32_t>(rtcNow.unixtime() - ntpLocal)
                            : INT32_MAX;

  bool corrected = false;
  if (drift == INT32_MAX || drift >= Config::RTC_SYNC_MIN_DRIFT_S ||
      drift <= -Config::RTC_SYNC_MIN_DRIFT_S) {
    rtc.adjust(DateTime(ntpLocal));  // also clears the lost-power flag
    corrected = true;
    insaneReads = 0;
  }

  portENTER_CRITICAL(&statusMux);
  status.lastDriftS = drift == INT32_MAX ? 0 : drift;
  status.lastNtpSyncEpoch = static_cast<uint32_t>(time(nullptr));
  if (corrected) ++status.ntpSyncs;
  portEXIT_CRITICAL(&statusMux);

  if (corrected) {
    if (drift == INT32_MAX) logf("[RTC] set from NTP (RTC time was invalid)");
    else logf("[RTC] corrected from NTP, drift was %lds", static_cast<long>(drift));
  }
}

void refresh() {
  ClockSource source = ClockSource::NONE;
  LocalTime t = {};

  if (rtcPresent) {
    const DateTime now = rtc.now();
    if (plausible(now)) {
      insaneReads = 0;
      rtcSane = true;
      source = ClockSource::RTC;
      t = {true, static_cast<uint16_t>(now.year()), now.month(), now.day(),
           now.hour(), now.minute(), now.second()};
    } else {
      rtcSane = false;
      if (++insaneReads >= INSANE_READS_TO_DROP) {
        // All-ones reads (165/165/2165 ...) mean the I2C device went away.
        rtcPresent = false;
        insaneReads = 0;
        logf("[RTC] no plausible time from DS3231, will re-probe every %lus",
             static_cast<unsigned long>(RTC_RETRY_MS / 1000UL));
      }
    }
  }

  if (source == ClockSource::NONE && wallClockReady()) {
    const time_t epoch = time(nullptr);
    struct tm local;
    localtime_r(&epoch, &local);  // TZ was set by configTime() (WIB)
    source = ClockSource::NTP;
    t = {true, static_cast<uint16_t>(local.tm_year + 1900), static_cast<uint8_t>(local.tm_mon + 1),
         static_cast<uint8_t>(local.tm_mday), static_cast<uint8_t>(local.tm_hour),
         static_cast<uint8_t>(local.tm_min), static_cast<uint8_t>(local.tm_sec)};
  }

  cached = t;
  publishStatus(source);
}

}  // namespace

void beginClock() {
  Wire.begin(Config::Pins::RTC_SDA, Config::Pins::RTC_SCL, Config::I2C_CLOCK_HZ);
  Wire.setTimeOut(20);  // a wedged bus must not stall the loop
  rtcPresent = probeRtc();
  lastProbeMs = millis();
  logf("[RTC] DS3231 %s", rtcPresent ? "found" : "not found, will retry");
  refresh();
  refreshedOnce = true;
  lastRefreshMs = millis();
}

void updateClock(uint32_t now) {
  if (!rtcPresent && now - lastProbeMs >= RTC_RETRY_MS) {
    lastProbeMs = now;
    if (probeRtc()) {
      rtcPresent = true;
      logf("[RTC] DS3231 found");
    }
  }

  syncRtcFromNtp(now);

  if (!refreshedOnce || now - lastRefreshMs >= REFRESH_MS) {
    lastRefreshMs = now;
    refreshedOnce = true;
    refresh();
  }
}

LocalTime clockNow() { return cached; }

bool clockTimestamp(char *out, size_t outSize) {
  if (outSize) out[0] = '\0';
  // Fresh read, not the cache: the timestamp is what gets stored.
  refresh();
  lastRefreshMs = millis();
  const LocalTime t = cached;
  if (!t.valid) return false;
  const int written = snprintf(out, outSize, "%04u-%02u-%02u %02u:%02u:%02u", t.year, t.month,
                               t.day, t.hour, t.minute, t.second);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

bool clockSetTimeOfDay(uint8_t hour, uint8_t minute, uint8_t second) {
  if (!rtcPresent) return false;
  const LocalTime t = cached;
  if (t.valid) {
    rtc.adjust(DateTime(t.year, t.month, t.day, hour, minute, second));
  } else {
    // No trustworthy date at all: keep the RTC's own date if it is at least
    // a real calendar date, else there is nothing sane to anchor it to.
    const DateTime now = rtc.now();
    if (!plausible(now)) return false;
    rtc.adjust(DateTime(now.year(), now.month(), now.day(), hour, minute, second));
  }
  insaneReads = 0;
  refresh();
  logf("[RTC] set manually to %02u:%02u:%02u", hour, minute, second);
  return true;
}

ClockStatus clockStatus() {
  portENTER_CRITICAL(&statusMux);
  ClockStatus copy = status;
  portEXIT_CRITICAL(&statusMux);
  return copy;
}
