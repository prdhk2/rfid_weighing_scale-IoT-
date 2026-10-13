#pragma once

#include <Arduino.h>

#include "http_outcome.h"
#include "rfid_frame.h"

// Weighing uploads to the monitoring server, run one at a time on a worker
// task (core 0) so a slow TLS handshake never freezes the LCD, the buttons or
// the RFID bus. The original firmware called http.POST() from loop(): up to
// 3 x 5 s with everything else frozen.
//
// The loop submits a request and polls for its result; every result carries
// the sequence number of its request, so an answer that arrives after the app
// has given up on it is recognised and ignored.
//
//   Weigh-in:  POST {API_BASE_URL}/weightDataIn
//              {"driver_id":N,"customer_id":N,"weight_in":12.34,"time_in":"YYYY-MM-DD HH:MM:SS"}
//   Weigh-out: POST {API_BASE_URL}/weightDataOut
//              {"driver_id":N,"customer_id":N,"weight_out":12.34,"time_out":"..."}
//   Headers:   Content-Type + Accept: application/json (without Accept, a
//              Laravel validation error is a 302 redirect, not a 422), and
//              X-API-KEY when one is configured.

struct ApiResult {
  uint32_t seq;
  RfidFrame::Direction direction;
  int httpCode;  // < 0: transport error (HTTPClient code); HttpOutcome::NO_WIFI
  HttpOutcome::Verdict verdict;
  uint8_t attempts;
};

struct ApiStats {
  uint32_t sendsOk;
  uint32_t sendsFailed;
  int lastHttpCode;          // 0 before the first upload
  uint32_t lastSendOkEpoch;  // 0 if none (or before NTP)
  uint32_t lastDurationMs;   // of the last upload, all attempts included
  bool busy;
};

void beginApiClient();

// Returns the request's sequence number, or 0 if one is already in flight or
// the worker is not running.
uint32_t apiSubmitWeight(RfidFrame::Direction direction, int32_t driverId, int32_t customerId,
                         float weightKg, const char *timestamp);

// True (and fills out) when a result is ready.
bool apiTakeResult(ApiResult &out);

bool apiBusy();
ApiStats apiStats();
