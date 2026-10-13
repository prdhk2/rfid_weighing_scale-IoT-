#include "peripherals.h"

#include <cstring>
#include <esp32-hal-psram.h>

#include "config.h"
#include "debug_log.h"
#include "elapsed.h"
#include "line_assembler.h"
#include "weight_parser.h"

namespace {

// Per loop pass per UART, so a noisy line can never starve the loop.
constexpr int MAX_BYTES_PER_PASS = 256;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ------------------------------------------------------------------ Scale
HardwareSerial scalePort(2);
LineAssembler<32> scaleLine;
float scaleWeight = 0.0f;
bool scaleEver = false;
uint32_t scaleLastMs = 0;
uint32_t scaleFrames = 0;
uint32_t scaleRejected = 0;
bool scaleAnnounced = false;
bool scaleWasLive = false;

// ------------------------------------------------------------------- RFID
HardwareSerial rfidPort(1);
RfidFrame::Assembler rfidAssembler;
RfidFrame::Card lastCard = {};
uint32_t rfidLastByteMs = 0;
uint32_t rfidCards = 0;
uint32_t rfidRejected = 0;
bool rfidEverCard = false;
uint32_t rfidLastCardMs = 0;

// The last accepted frame, to recognise a reader repeating one tap.
char lastFrame[RfidFrame::MAX_PAYLOAD + 1] = "";
uint32_t lastFrameMs = 0;

void countRfidReject() {
  portENTER_CRITICAL(&mux);
  ++rfidRejected;
  portEXIT_CRITICAL(&mux);
}

// ---------------------------------------------------------------- Buttons
DebouncedButton inButton(Config::BUTTON_DEBOUNCE_MS, Config::CLOCK_ADJUST_HOLD_MS);
DebouncedButton outButton(Config::BUTTON_DEBOUNCE_MS, Config::CLOCK_ADJUST_HOLD_MS);
DebouncedButton resetButton(Config::BUTTON_DEBOUNCE_MS, Config::CLOCK_ADJUST_HOLD_MS);

}  // namespace

// ================================================================== Scale

void beginScale() {
  scalePort.setRxBufferSize(Config::UART_RX_BUFFER);  // before begin()
  scalePort.begin(Config::SCALE_BAUD, SERIAL_8N1, Config::Pins::SCALE_RX, -1,
                  Config::SCALE_RX_INVERTED);
}

void updateScale(uint32_t now) {
  for (int i = 0; i < MAX_BYTES_PER_PASS && scalePort.available() > 0; ++i) {
    const int c = scalePort.read();
    if (c < 0) break;
    if (!scaleLine.push(static_cast<char>(c))) continue;

    float kg = 0.0f;
    const bool ok = WeightParser::parse(scaleLine.line(), kg);
    portENTER_CRITICAL(&mux);
    if (ok) {
      scaleWeight = kg;
      scaleEver = true;
      scaleLastMs = now;
      ++scaleFrames;
    } else {
      ++scaleRejected;
    }
    portEXIT_CRITICAL(&mux);

    if (ok && !scaleAnnounced) {
      scaleAnnounced = true;
      scaleWasLive = true;  // the first reading is not a "resumed" event
      logf("[SCALE] first reading: %.2f kg", static_cast<double>(kg));
    }
  }

  // Log the transitions, not the state: one line when the indicator goes
  // quiet, one when it comes back.
  const ScaleSnapshot s = scaleSnapshot(now);
  if (s.everReceived && s.live != scaleWasLive) {
    scaleWasLive = s.live;
    if (s.live) logf("[SCALE] readings resumed: %.2f kg", static_cast<double>(s.weightKg));
    else logf("[SCALE] no reading for %lums - weight unavailable", static_cast<unsigned long>(s.ageMs));
  }
}

ScaleSnapshot scaleSnapshot(uint32_t now) {
  ScaleSnapshot s;
  portENTER_CRITICAL(&mux);
  s.everReceived = scaleEver;
  s.weightKg = scaleWeight;
  s.ageMs = scaleEver ? elapsedMs(now, scaleLastMs) : UINT32_MAX;
  s.frames = scaleFrames;
  s.rejected = scaleRejected;
  portEXIT_CRITICAL(&mux);
  s.live = s.everReceived && s.ageMs < Config::SCALE_STALE_MS;
  return s;
}

// =================================================================== RFID

void beginRfid() {
  // DE/RE low = receive only, as in the original firmware. Only safe because
  // PSRAM is off (platformio.ini): GPIO35 is an octal-PSRAM data line, and
  // driving it while PSRAM is in use corrupts memory. If someone builds with
  // PSRAM back on, leave the pin alone and say so loudly.
  if (psramFound()) {
    logf("[RS485] WARNING: PSRAM is active - GPIO%u (DE/RE) NOT driven to protect it. "
         "Build with board_build.arduino.memory_type = qio_qspi.",
         Config::Pins::RS485_DE_RE);
  } else {
    pinMode(Config::Pins::RS485_DE_RE, OUTPUT);
    digitalWrite(Config::Pins::RS485_DE_RE, LOW);
  }

  rfidPort.setRxBufferSize(Config::UART_RX_BUFFER);
  rfidPort.begin(Config::RS485_BAUD, SERIAL_8N1, Config::Pins::RS485_RX, -1);
}

RfidEvent updateRfid(uint32_t now) {
  for (int i = 0; i < MAX_BYTES_PER_PASS && rfidPort.available() > 0; ++i) {
    const int c = rfidPort.read();
    if (c < 0) break;
    rfidLastByteMs = now;

    switch (rfidAssembler.push(static_cast<uint8_t>(c))) {
      case RfidFrame::Assembler::Event::NONE:
        break;
      case RfidFrame::Assembler::Event::OVERFLOW:
        countRfidReject();
        logf("[RFID] frame too long, dropped");
        return RfidEvent::OVERFLOW;
      case RfidFrame::Assembler::Event::FRAME: {
        const char *payload = rfidAssembler.payload();
        if (lastFrame[0] != '\0' && now - lastFrameMs < Config::RFID_REPEAT_IGNORE_MS &&
            strcmp(payload, lastFrame) == 0) {
          return RfidEvent::REPEAT;
        }
        RfidFrame::Card card;
        const RfidFrame::ParseResult result = RfidFrame::parsePayload(payload, card);
        if (result != RfidFrame::ParseResult::OK) {
          countRfidReject();
          logf("[RFID] bad frame (%s): %.60s", RfidFrame::parseResultName(result), payload);
          return RfidEvent::BAD_FORMAT;
        }
        strncpy(lastFrame, payload, sizeof(lastFrame) - 1);
        lastFrame[sizeof(lastFrame) - 1] = '\0';
        lastFrameMs = now;
        lastCard = card;
        portENTER_CRITICAL(&mux);
        ++rfidCards;
        rfidEverCard = true;
        rfidLastCardMs = now;
        portEXIT_CRITICAL(&mux);
        return RfidEvent::CARD;
      }
    }
  }

  if (rfidAssembler.receiving() && now - rfidLastByteMs > Config::RFID_FRAME_TIMEOUT_MS) {
    rfidAssembler.reset();
    countRfidReject();
    logf("[RFID] frame timeout (STX without ETX)");
    return RfidEvent::TIMEOUT;
  }
  return RfidEvent::NONE;
}

const RfidFrame::Card &rfidCard() { return lastCard; }

void rfidDiscardPending() {
  for (int i = 0; i < MAX_BYTES_PER_PASS && rfidPort.available() > 0; ++i) rfidPort.read();
  rfidAssembler.reset();
}

bool rfidReceiving() { return rfidAssembler.receiving(); }

RfidSnapshot rfidSnapshot(uint32_t now) {
  RfidSnapshot s;
  portENTER_CRITICAL(&mux);
  s.cards = rfidCards;
  s.rejected = rfidRejected;
  s.lastCardAgeMs = rfidEverCard ? elapsedMs(now, rfidLastCardMs) : UINT32_MAX;
  portEXIT_CRITICAL(&mux);
  return s;
}

// ================================================================ Buttons

void beginButtons() {
  pinMode(Config::Pins::IN_BUTTON, INPUT_PULLUP);
  pinMode(Config::Pins::OUT_BUTTON, INPUT_PULLUP);
  pinMode(Config::Pins::RESET_BUTTON, INPUT_PULLUP);
}

ButtonEvents updateButtons(uint32_t now) {
  ButtonEvents e;
  e.in = inButton.update(digitalRead(Config::Pins::IN_BUTTON) == LOW, now);
  e.out = outButton.update(digitalRead(Config::Pins::OUT_BUTTON) == LOW, now);
  e.reset = resetButton.update(digitalRead(Config::Pins::RESET_BUTTON) == LOW, now);
  return e;
}

void suppressButtonsUntilRelease() {
  inButton.suppressUntilRelease();
  outButton.suppressUntilRelease();
  resetButton.suppressUntilRelease();
}
