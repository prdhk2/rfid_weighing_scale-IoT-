#include "rfid_frame.h"

#include <cstring>

namespace RfidFrame {

namespace {

constexpr uint8_t STX = 0x02;
constexpr uint8_t ETX = 0x03;
constexpr size_t FIELD_COUNT = 6;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

void trim(const char *&begin, const char *&end) {
  while (begin < end && isSpace(*begin)) ++begin;
  while (end > begin && isSpace(*(end - 1))) --end;
}

// Copies [begin, end) trimmed into out (capacity outSize incl. NUL), cutting
// anything that does not fit and neutralising JSON-special characters.
void copyText(const char *begin, const char *end, char *out, size_t outSize) {
  trim(begin, end);
  size_t len = static_cast<size_t>(end - begin);
  if (len >= outSize) len = outSize - 1;
  for (size_t i = 0; i < len; ++i) {
    const char c = begin[i];
    out[i] = (c == '"' || c == '\\') ? '\'' : c;
  }
  out[len] = '\0';
}

bool parsePositiveInt(const char *begin, const char *end, int32_t &out) {
  trim(begin, end);
  if (begin == end) return false;
  int64_t value = 0;
  for (const char *p = begin; p < end; ++p) {
    if (*p < '0' || *p > '9') return false;
    value = value * 10 + (*p - '0');
    if (value > INT32_MAX) return false;
  }
  if (value <= 0) return false;
  out = static_cast<int32_t>(value);
  return true;
}

}  // namespace

Assembler::Event Assembler::push(uint8_t byte) {
  if (byte == STX) {
    receiving_ = true;  // a new STX restarts any half-received frame
    len_ = 0;
    return Event::NONE;
  }
  if (!receiving_) return Event::NONE;

  if (byte == ETX) {
    receiving_ = false;
    buffer_[len_] = '\0';
    len_ = 0;
    return Event::FRAME;
  }
  if (byte < 0x20 || byte > 0x7E) return Event::NONE;  // line noise inside a frame
  if (len_ >= MAX_PAYLOAD) {
    receiving_ = false;
    len_ = 0;
    return Event::OVERFLOW;
  }
  buffer_[len_++] = static_cast<char>(byte);
  return Event::NONE;
}

ParseResult parsePayload(const char *payload, Card &out) {
  if (!payload) return ParseResult::BAD_FIELD_COUNT;

  // Field i spans [start[i], stop[i]). The last field runs to the end.
  const char *start[FIELD_COUNT];
  const char *stop[FIELD_COUNT];
  const char *p = payload;
  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    start[i] = p;
    if (i + 1 == FIELD_COUNT) {
      stop[i] = p + strlen(p);
      break;
    }
    const char *comma = strchr(p, ',');
    if (!comma) return ParseResult::BAD_FIELD_COUNT;
    stop[i] = comma;
    p = comma + 1;
  }

  Card card = {};
  copyText(start[0], stop[0], card.slaveId, sizeof(card.slaveId));
  if (strcmp(card.slaveId, "1") == 0) {
    card.direction = Direction::IN;
  } else if (strcmp(card.slaveId, "2") == 0) {
    card.direction = Direction::OUT;
  } else {
    return ParseResult::BAD_SLAVE;
  }
  if (!parsePositiveInt(start[1], stop[1], card.driverId)) return ParseResult::BAD_DRIVER_ID;
  copyText(start[2], stop[2], card.driverName, sizeof(card.driverName));
  if (!parsePositiveInt(start[3], stop[3], card.customerId)) return ParseResult::BAD_CUSTOMER_ID;
  copyText(start[4], stop[4], card.plate, sizeof(card.plate));
  copyText(start[5], stop[5], card.customerName, sizeof(card.customerName));

  out = card;
  return ParseResult::OK;
}

const char *parseResultName(ParseResult result) {
  switch (result) {
    case ParseResult::OK: return "OK";
    case ParseResult::BAD_FIELD_COUNT: return "BAD_FIELD_COUNT";
    case ParseResult::BAD_SLAVE: return "BAD_SLAVE";
    case ParseResult::BAD_DRIVER_ID: return "BAD_DRIVER_ID";
    case ParseResult::BAD_CUSTOMER_ID: return "BAD_CUSTOMER_ID";
  }
  return "UNKNOWN";
}

}  // namespace RfidFrame
