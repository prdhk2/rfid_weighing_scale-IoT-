#pragma once

#include <cstddef>
#include <cstdint>

// RFID reader framing and payload for the truck weighbridge. Pure C++,
// unit-tested natively in test/test_rfid_frame/.
//
// Each reader (slave) on the RS485 bus sends one frame per card tap:
//
//   STX (0x02) "slave_id,id_driver,nama_driver,id_customer,no_polisi,nama_customer" ETX (0x03)
//
// slave_id "1" is the entry reader (data masuk -> POST /api/weightDataIn),
// "2" the exit reader (data keluar -> POST /api/weightDataOut). Bytes outside
// a frame are line noise and ignored; non-printable bytes inside one are
// dropped, as the original firmware did. A frame that overflows is dropped
// whole; the caller times out a frame that never ends.
namespace RfidFrame {

constexpr size_t MAX_PAYLOAD = 128;
constexpr size_t MAX_SLAVE = 7;
constexpr size_t MAX_NAME = 32;
constexpr size_t MAX_PLATE = 15;

class Assembler {
 public:
  enum class Event : uint8_t { NONE, FRAME, OVERFLOW };

  // Feeds one byte. FRAME: a complete payload is in payload() until the next
  // push(). OVERFLOW: a frame was longer than MAX_PAYLOAD and was dropped.
  Event push(uint8_t byte);

  const char *payload() const { return buffer_; }
  bool receiving() const { return receiving_; }
  void reset() {
    receiving_ = false;
    len_ = 0;
  }

 private:
  char buffer_[MAX_PAYLOAD + 1] = {};
  size_t len_ = 0;
  bool receiving_ = false;
};

enum class Direction : uint8_t { IN, OUT };

struct Card {
  Direction direction;
  char slaveId[MAX_SLAVE + 1];
  int32_t driverId;
  char driverName[MAX_NAME + 1];
  int32_t customerId;
  char plate[MAX_PLATE + 1];
  char customerName[MAX_NAME + 1];
};

enum class ParseResult : uint8_t {
  OK,
  BAD_FIELD_COUNT,  // fewer than six fields
  BAD_SLAVE,        // slave id is neither "1" nor "2"
  BAD_DRIVER_ID,    // not a positive integer
  BAD_CUSTOMER_ID,  // not a positive integer
};

// Splits the payload into its six fields. The customer name is the last field
// and keeps any further commas ("PT Maju, Tbk"). Every field is trimmed; the
// names and plate are cut to fit and have '"' and '\' replaced by '\'' so they
// can be embedded verbatim in JSON. The ids must be plain positive integers -
// the server rejects anything else, so a garbled frame is refused here rather
// than sent as id 0.
ParseResult parsePayload(const char *payload, Card &out);

const char *parseResultName(ParseResult result);

}  // namespace RfidFrame
