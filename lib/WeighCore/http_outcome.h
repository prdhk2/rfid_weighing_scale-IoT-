#pragma once

#include <cstdint>

// What an upload's HTTP result means for the operator, and whether it is safe
// to try again automatically. Pure C++, unit-tested natively in
// test/test_http_outcome/.
//
// Server side (API_Integration_IoT-Laravel12, CarDataController):
//   POST /api/weightDataIn   200 {"success":true}
//                            409 same customer weighed in within the last minute
//                            404 driver / customer not found
//                            422 validation (unknown driver_id/customer_id, bad time)
//   POST /api/weightDataOut  200 {"success":true}
//                            404 no open entry today for this driver+customer
//                                (never weighed in, or already weighed out)
//                            422 validation
namespace HttpOutcome {

// Not an HTTP code: the upload was not attempted because Wi-Fi is down.
// Same value the original firmware used.
constexpr int NO_WIFI = -99;

enum class Verdict : uint8_t {
  OK,            // 2xx
  DUPLICATE,     // 409: already recorded a moment ago
  NOT_FOUND,     // 404
  REJECTED,      // 400/422 and any other 4xx not listed here
  AUTH,          // 401/403: API key problem
  SERVER_ERROR,  // 5xx, 3xx (e.g. an http:// URL redirected to https://)
  TRANSPORT,     // no HTTP answer at all (HTTPClient error < 0)
  NO_WIFI,       // NO_WIFI
};

Verdict classify(int code);

// True only for transport failures where the request certainly never reached
// the application (connect failed, headers/body could not be sent). A read
// timeout or a dropped connection after sending is NOT safe: the record may
// already be stored, and a blind retry would duplicate it.
bool retrySafe(int code);

const char *verdictName(Verdict verdict);

// Short operator-facing texts for the 128x64 LCD. detail must stay <= 20
// characters: the result screen draws it inside a frame 122 px wide (6x10 font).
// title: large first line; detail: what it means. outbound: the upload was a
// weigh-out (data keluar) - a 404 means something different there.
const char *lcdTitle(Verdict verdict);
const char *lcdDetail(Verdict verdict, bool outbound);

}  // namespace HttpOutcome
