#include "http_outcome.h"

namespace HttpOutcome {

namespace {

// HTTPClient's negative codes (HTTPClient.h), duplicated as plain numbers so
// this file stays free of Arduino headers.
constexpr int ERR_CONNECTION_REFUSED = -1;  // TCP or TLS connect failed
constexpr int ERR_SEND_HEADER_FAILED = -2;
constexpr int ERR_SEND_PAYLOAD_FAILED = -3; // body incomplete: never dispatched
constexpr int ERR_NOT_CONNECTED = -4;
constexpr int ERR_NO_HTTP_SERVER = -7;
constexpr int ERR_TOO_LESS_RAM = -8;

}  // namespace

Verdict classify(int code) {
  if (code == NO_WIFI) return Verdict::NO_WIFI;
  if (code < 0 || code == 0) return Verdict::TRANSPORT;
  if (code >= 200 && code < 300) return Verdict::OK;
  if (code == 409) return Verdict::DUPLICATE;
  if (code == 404) return Verdict::NOT_FOUND;
  if (code == 401 || code == 403) return Verdict::AUTH;
  if (code >= 400 && code < 500) return Verdict::REJECTED;
  return Verdict::SERVER_ERROR;
}

bool retrySafe(int code) {
  switch (code) {
    case ERR_CONNECTION_REFUSED:
    case ERR_SEND_HEADER_FAILED:
    case ERR_SEND_PAYLOAD_FAILED:
    case ERR_NOT_CONNECTED:
    case ERR_NO_HTTP_SERVER:
    case ERR_TOO_LESS_RAM:
      return true;
    default:
      return false;
  }
}

const char *verdictName(Verdict verdict) {
  switch (verdict) {
    case Verdict::OK: return "OK";
    case Verdict::DUPLICATE: return "DUPLICATE";
    case Verdict::NOT_FOUND: return "NOT_FOUND";
    case Verdict::REJECTED: return "REJECTED";
    case Verdict::AUTH: return "AUTH";
    case Verdict::SERVER_ERROR: return "SERVER_ERROR";
    case Verdict::TRANSPORT: return "TRANSPORT";
    case Verdict::NO_WIFI: return "NO_WIFI";
  }
  return "UNKNOWN";
}

const char *lcdTitle(Verdict verdict) {
  switch (verdict) {
    case Verdict::OK: return "Data Terkirim!";
    case Verdict::DUPLICATE: return "Sudah Tercatat";
    default: return "Gagal!";
  }
}

const char *lcdDetail(Verdict verdict, bool outbound) {
  switch (verdict) {
    case Verdict::OK: return outbound ? "Keluar tersimpan" : "Masuk tersimpan";
    case Verdict::DUPLICATE: return "Data sama < 1 menit";
    case Verdict::NOT_FOUND: return outbound ? "Data masuk tdk ada" : "Driver/cust tdk ada";
    case Verdict::REJECTED: return "Data ditolak server";
    case Verdict::AUTH: return "Akses API ditolak";
    case Verdict::SERVER_ERROR: return "Server error";
    case Verdict::TRANSPORT: return "Server tdk merespon";
    case Verdict::NO_WIFI: return "WiFi terputus";
  }
  return "";
}

}  // namespace HttpOutcome
