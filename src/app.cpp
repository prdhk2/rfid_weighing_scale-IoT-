#include "app.h"

#include <cstring>

#include "api_client.h"
#include "clock.h"
#include "config.h"
#include "debug_log.h"
#include "health_monitor.h"
#include "network_service.h"
#include "ota_manager.h"
#include "peripherals.h"
#include "ui.h"

namespace {

enum class State : uint8_t { HOME, SENDING, RESULT, WEIGHT, ERROR };

enum class AdjustField : uint8_t { OFF = 0, HOUR = 1, MINUTE = 2, SECOND = 3 };

enum class ErrorKind : uint8_t {
  RFID_FORMAT,
  RFID_OVERFLOW,
  RFID_TIMEOUT,
  SCALE_OFFLINE,
  WEIGHT_LOW,
  CLOCK_INVALID,
  API_BUSY,
  RTC_MISSING,
};

constexpr uint32_t HOME_POLL_MS = 100;
constexpr uint32_t BLINK_MS = 500;
constexpr uint32_t WEIGHT_REFRESH_MS = 500;
// A changed weight redraws sooner, but never more often than this: an ST7920
// over software SPI takes tens of ms per frame.
constexpr uint32_t WEIGHT_MIN_REDRAW_MS = 250;
constexpr uint32_t SENDING_ANIM_MS = 400;

State state = State::HOME;
uint32_t stateSince = 0;
bool redraw = true;

// Card on screen (WEIGHT) / being uploaded.
RfidFrame::Card card = {};
bool hasCard = false;

// Upload in flight / last result.
uint32_t pendingSeq = 0;
RfidFrame::Direction pendingDirection = RfidFrame::Direction::IN;
float pendingWeight = 0.0f;
HttpOutcome::Verdict resultVerdict = HttpOutcome::Verdict::OK;
int resultCode = 0;

ErrorKind errorKind = ErrorKind::RFID_FORMAT;
State errorReturn = State::HOME;

// Clock setting on the home screen.
AdjustField adjust = AdjustField::OFF;
uint8_t adjHour = 0, adjMinute = 0, adjSecond = 0;
uint32_t adjustLastInputMs = 0;

// Change detection, so the LCD is only redrawn when something visible moved.
uint32_t lastPollMs = 0;
LocalTime shownTime = {};
bool shownBlink = true;
bool shownWifi = false;
int shownOtaPct = -2;
bool shownScaleLive = false;
uint32_t lastWeightDrawMs = 0;
bool shownWeightLive = false;
float shownWeight = 0.0f;
uint8_t sendingDots = 0;
uint32_t lastAnimMs = 0;

// For appStatus(), read by the MQTT task.
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
AppStatus status = {};
uint32_t lastSendAtMs = 0;

const char *stateName(State s) {
  switch (s) {
    case State::HOME: return adjust == AdjustField::OFF ? "HOME" : "CLOCK_SET";
    case State::SENDING: return "SENDING";
    case State::RESULT: return "RESULT";
    case State::WEIGHT: return "WEIGHT";
    case State::ERROR: return "ERROR";
  }
  return "UNKNOWN";
}

void publishStatus() {
  portENTER_CRITICAL(&statusMux);
  status.stateName = stateName(state);
  status.hasCard = hasCard;
  status.direction = card.direction;
  status.driverId = hasCard ? card.driverId : 0;
  status.customerId = hasCard ? card.customerId : 0;
  if (hasCard) memcpy(status.plate, card.plate, sizeof(status.plate));
  else status.plate[0] = '\0';
  portEXIT_CRITICAL(&statusMux);
}

void enter(State next, uint32_t now) {
  if (next != state) {
    logf("[APP] %s -> %s", stateName(state), stateName(next));
  }
  state = next;
  stateSince = now;
  redraw = true;
  // A button held through the change must not act on the new screen.
  suppressButtonsUntilRelease();
  publishStatus();
}

void showError(ErrorKind kind, State returnTo, uint32_t now) {
  errorKind = kind;
  errorReturn = returnTo;
  enter(State::ERROR, now);
}

void cancelAdjust(const char *why) {
  if (adjust == AdjustField::OFF) return;
  adjust = AdjustField::OFF;
  logf("[APP] clock setting cancelled (%s)", why);
}

// Validates and submits one upload with the weight on the scale right now.
// errorReturn: where to go if it cannot even be attempted.
void startSend(RfidFrame::Direction direction, State errorReturnState, uint32_t now) {
  const ScaleSnapshot scale = scaleSnapshot(now);
  if (!scale.live) {
    showError(ErrorKind::SCALE_OFFLINE, errorReturnState, now);
    return;
  }
  if (!(scale.weightKg > Config::MIN_SEND_WEIGHT_KG)) {
    logf("[APP] weight %.2f kg too low to send", static_cast<double>(scale.weightKg));
    showError(ErrorKind::WEIGHT_LOW, errorReturnState, now);
    return;
  }
  char timestamp[24];
  if (!clockTimestamp(timestamp, sizeof(timestamp))) {
    logf("[APP] no valid time (RTC and NTP both unavailable), not sending");
    showError(ErrorKind::CLOCK_INVALID, errorReturnState, now);
    return;
  }

  const uint32_t seq =
      apiSubmitWeight(direction, card.driverId, card.customerId, scale.weightKg, timestamp);
  if (seq == 0) {
    logf("[APP] upload rejected: previous one still in flight");
    showError(ErrorKind::API_BUSY, errorReturnState, now);
    return;
  }
  pendingSeq = seq;
  pendingDirection = direction;
  pendingWeight = scale.weightKg;
  sendingDots = 0;
  lastAnimMs = now;
  enter(State::SENDING, now);
}

void handleCard(uint32_t now) {
  card = rfidCard();
  hasCard = true;
  cancelAdjust("card tapped");
  logf("[RFID] card slave=%s driver=%ld cust=%ld nopol=%s", card.slaveId,
       static_cast<long>(card.driverId), static_cast<long>(card.customerId), card.plate);
  // Like the original: a tap is an upload straight away, direction by reader.
  startSend(card.direction, State::HOME, now);
}

// RFID is only listened to on HOME and WEIGHT.
void pumpRfid(uint32_t now) {
  if (state != State::HOME && state != State::WEIGHT) {
    rfidDiscardPending();
    return;
  }
  const State here = state;
  switch (updateRfid(now)) {
    case RfidEvent::NONE:
    case RfidEvent::REPEAT:
      break;
    case RfidEvent::CARD:
      handleCard(now);
      break;
    case RfidEvent::BAD_FORMAT:
      showError(ErrorKind::RFID_FORMAT, here, now);
      break;
    case RfidEvent::OVERFLOW:
      showError(ErrorKind::RFID_OVERFLOW, here, now);
      break;
    case RfidEvent::TIMEOUT:
      showError(ErrorKind::RFID_TIMEOUT, here, now);
      break;
  }
}

// ------------------------------------------------------------------- HOME

const char *homeFooter(int otaPct, bool scaleLive) {
  static char text[24];
  if (otaPct >= 0) {
    snprintf(text, sizeof(text), "Update FW %d%%", otaPct);
    return text;
  }
  if (OtaManager::updateStaged() || restartPending()) return "Restart sebentar...";
  if (adjust != AdjustField::OFF) return "Adjust Time";
  if (!scaleLive) return "Timbangan Offline";
  return "Waiting Data";
}

void handleClockSetting(const ButtonEvents &buttons, uint32_t now) {
  if (buttons.reset.longPress) {
    adjustLastInputMs = now;
    switch (adjust) {
      case AdjustField::OFF: {
        const LocalTime t = clockNow();
        adjHour = t.valid ? t.hour : 0;
        adjMinute = t.valid ? t.minute : 0;
        adjSecond = t.valid ? t.second : 0;
        adjust = AdjustField::HOUR;
        logf("[APP] clock setting started");
        break;
      }
      case AdjustField::HOUR:
        adjust = AdjustField::MINUTE;
        break;
      case AdjustField::MINUTE:
        adjust = AdjustField::SECOND;
        break;
      case AdjustField::SECOND:
        adjust = AdjustField::OFF;
        if (!clockSetTimeOfDay(adjHour, adjMinute, adjSecond)) {
          showError(ErrorKind::RTC_MISSING, State::HOME, now);
          return;
        }
        break;
    }
    redraw = true;
  }

  if (adjust == AdjustField::OFF) return;

  if (buttons.in.pressed) {
    adjustLastInputMs = now;
    switch (adjust) {
      case AdjustField::HOUR: adjHour = (adjHour + 1) % 24; break;
      case AdjustField::MINUTE: adjMinute = (adjMinute + 1) % 60; break;
      case AdjustField::SECOND: adjSecond = (adjSecond + 1) % 60; break;
      case AdjustField::OFF: break;
    }
    redraw = true;
  }

  if (now - adjustLastInputMs >= Config::CLOCK_ADJUST_IDLE_MS) {
    cancelAdjust("no input");
    redraw = true;
  }
}

void runHome(const ButtonEvents &buttons, uint32_t now) {
  handleClockSetting(buttons, now);
  if (state != State::HOME) return;  // the clock setting may have raised an error

  if (!redraw && now - lastPollMs < HOME_POLL_MS) return;
  lastPollMs = now;

  const LocalTime t = clockNow();
  const bool blink = ((now / BLINK_MS) & 1u) == 0;
  const bool wifi = wifiConnected();
  const int otaPct = OtaManager::progressPercent();
  const bool scaleLive = scaleSnapshot(now).live;

  const bool timeChanged = t.valid != shownTime.valid || t.second != shownTime.second ||
                           t.minute != shownTime.minute || t.day != shownTime.day;
  const bool blinkChanged = adjust != AdjustField::OFF && blink != shownBlink;
  if (!redraw && !timeChanged && !blinkChanged && wifi == shownWifi && otaPct == shownOtaPct &&
      scaleLive == shownScaleLive) {
    return;
  }

  HomeView view = {};
  view.time = t;
  view.adjustField = static_cast<uint8_t>(adjust);
  view.blinkOn = blink;
  view.adjHour = adjHour;
  view.adjMinute = adjMinute;
  view.adjSecond = adjSecond;
  view.wifiOk = wifi;
  view.footer = homeFooter(otaPct, scaleLive);
  uiHome(view);

  shownTime = t;
  shownBlink = blink;
  shownWifi = wifi;
  shownOtaPct = otaPct;
  shownScaleLive = scaleLive;
  redraw = false;
}

// ----------------------------------------------------------------- WEIGHT

void runWeight(const ButtonEvents &buttons, uint32_t now) {
  if (buttons.in.pressed) {
    logf("[APP] Masuk pressed: re-send as weigh-in");
    startSend(RfidFrame::Direction::IN, State::WEIGHT, now);
    return;
  }
  if (buttons.out.pressed) {
    logf("[APP] Keluar pressed: re-send as weigh-out");
    startSend(RfidFrame::Direction::OUT, State::WEIGHT, now);
    return;
  }
  if (buttons.reset.pressed) {
    logf("[APP] Reset pressed: back to home");
    hasCard = false;
    enter(State::HOME, now);
    return;
  }
  if (now - stateSince >= Config::WEIGHT_SCREEN_TIMEOUT_MS) {
    hasCard = false;
    enter(State::HOME, now);
    return;
  }

  const ScaleSnapshot scale = scaleSnapshot(now);
  const bool changed = scale.live != shownWeightLive || (scale.live && scale.weightKg != shownWeight);
  if (redraw || (changed && now - lastWeightDrawMs >= WEIGHT_MIN_REDRAW_MS) ||
      now - lastWeightDrawMs >= WEIGHT_REFRESH_MS) {
    uiWeight(card, scale.live, scale.weightKg);
    lastWeightDrawMs = now;
    shownWeightLive = scale.live;
    shownWeight = scale.weightKg;
    redraw = false;
  }
}

// ---------------------------------------------------------------- SENDING

void finishSend(HttpOutcome::Verdict verdict, int code, uint32_t now) {
  resultVerdict = verdict;
  resultCode = code;
  portENTER_CRITICAL(&statusMux);
  lastSendAtMs = now;
  status.lastSendValid = true;
  status.lastSendDirection = pendingDirection;
  status.lastSendWeightKg = pendingWeight;
  status.lastSendHttp = code;
  portEXIT_CRITICAL(&statusMux);
  pendingSeq = 0;
  enter(State::RESULT, now);
}

void runSending(uint32_t now) {
  ApiResult result;
  if (apiTakeResult(result)) {
    if (result.seq == pendingSeq) {
      finishSend(result.verdict, result.httpCode, now);
      return;
    }
    logf("[APP] ignoring late result of upload #%lu", static_cast<unsigned long>(result.seq));
  }

  if (now - stateSince >= Config::SENDING_TIMEOUT_MS) {
    logf("[APP] upload #%lu gave no answer in %lus, giving up",
         static_cast<unsigned long>(pendingSeq),
         static_cast<unsigned long>(Config::SENDING_TIMEOUT_MS / 1000UL));
    finishSend(HttpOutcome::Verdict::TRANSPORT, -11, now);  // HTTPC_ERROR_READ_TIMEOUT
    return;
  }

  if (redraw || now - lastAnimMs >= SENDING_ANIM_MS) {
    if (!redraw) sendingDots = static_cast<uint8_t>((sendingDots + 1) % 4);
    lastAnimMs = now;
    uiSending(pendingDirection, sendingDots);
    redraw = false;
  }
}

// ----------------------------------------------------------- RESULT/ERROR

void runResult(uint32_t now) {
  if (redraw) {
    uiResult(resultVerdict, resultCode, pendingDirection);
    redraw = false;
  }
  if (now - stateSince >= Config::RESULT_SCREEN_MS) enter(State::WEIGHT, now);
}

void errorText(ErrorKind kind, const char *&line1, const char *&line2) {
  switch (kind) {
    case ErrorKind::RFID_FORMAT: line1 = "Format RFID Salah"; line2 = "Gunakan kartu valid"; return;
    case ErrorKind::RFID_OVERFLOW: line1 = "Data RFID Terlalu"; line2 = "Panjang"; return;
    case ErrorKind::RFID_TIMEOUT: line1 = "RFID Timeout"; line2 = "Tempelkan kembali"; return;
    case ErrorKind::SCALE_OFFLINE: line1 = "Timbangan Offline"; line2 = "Cek kabel indikator"; return;
    case ErrorKind::WEIGHT_LOW: line1 = "Berat Tidak Valid"; line2 = "Truk belum di atas"; return;
    case ErrorKind::CLOCK_INVALID: line1 = "Jam Belum Valid"; line2 = "Cek RTC / WiFi"; return;
    case ErrorKind::API_BUSY: line1 = "Sistem Sibuk"; line2 = "Coba lagi sebentar"; return;
    case ErrorKind::RTC_MISSING: line1 = "Jam Gagal Disimpan"; line2 = "Cek RTC / tanggal"; return;
  }
  line1 = "Error Sistem";
  line2 = "";
}

void runError(uint32_t now) {
  if (redraw) {
    const char *line1;
    const char *line2;
    errorText(errorKind, line1, line2);
    uiError(line1, line2);
    redraw = false;
  }
  if (now - stateSince >= Config::ERROR_SCREEN_MS) {
    const State back = (errorReturn == State::WEIGHT && hasCard) ? State::WEIGHT : State::HOME;
    if (back == State::HOME) hasCard = false;
    enter(back, now);
  }
}

}  // namespace

void beginApp() {
  state = State::HOME;
  stateSince = millis();
  redraw = true;
  status.stateName = stateName(state);
}

void updateApp(uint32_t now) {
  const ButtonEvents buttons = updateButtons(now);
  pumpRfid(now);

  switch (state) {
    case State::HOME: runHome(buttons, now); break;
    case State::WEIGHT: runWeight(buttons, now); break;
    case State::SENDING: runSending(now); break;
    case State::RESULT: runResult(now); break;
    case State::ERROR: runError(now); break;
  }
}

bool appIsIdle() {
  return state == State::HOME && adjust == AdjustField::OFF && !apiBusy() && !rfidReceiving();
}

AppStatus appStatus() {
  portENTER_CRITICAL(&statusMux);
  AppStatus copy = status;
  const uint32_t sentAt = lastSendAtMs;
  portEXIT_CRITICAL(&statusMux);
  copy.lastSendAgeMs = copy.lastSendValid ? millis() - sentAt : UINT32_MAX;
  return copy;
}
