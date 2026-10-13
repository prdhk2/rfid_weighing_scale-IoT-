#include "api_client.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <time.h>

#include "config.h"
#include "debug_log.h"
#include "mqtt_ca_cert.h"
#include "network_service.h"

namespace {

struct Job {
  uint32_t seq;
  RfidFrame::Direction direction;
  int32_t driverId;
  int32_t customerId;
  float weightKg;
  char timestamp[20];  // "YYYY-MM-DD HH:MM:SS"
};

QueueHandle_t jobQueue = nullptr;
QueueHandle_t resultQueue = nullptr;
volatile bool busy = false;
uint32_t nextSeq = 1;

portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
ApiStats stats = {};

bool isHttps() { return strncmp(Config::API_BASE_URL, "https://", 8) == 0; }

// Printable ASCII without quotes/backslashes, for embedding a server message
// in a log line (which ends up in JSON).
void copySafe(const char *in, char *out, size_t outSize) {
  size_t n = 0;
  for (; in && *in && n + 1 < outSize; ++in) {
    const unsigned char c = static_cast<unsigned char>(*in);
    out[n++] = (c < 0x20 || c >= 0x7F || c == '"' || c == '\\') ? ' ' : static_cast<char>(c);
  }
  out[n] = '\0';
}

// One POST. Returns the HTTP code or a negative HTTPClient error; body gets
// the start of the response (for the log), if any.
int postOnce(const char *url, const char *payload, char *body, size_t bodySize) {
  body[0] = '\0';
  if (!wifiConnected()) return HttpOutcome::NO_WIFI;

  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;
  http.setConnectTimeout(Config::HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(Config::HTTP_TIMEOUT_MS);
  http.setReuse(false);

  bool begun;
  if (isHttps()) {
    secureClient.setCACert(ISRG_ROOT_X1_PEM);
    // The library default is a 120 s handshake - on a dead link that would
    // hold the operator at "Mengirim..." for two minutes.
    secureClient.setHandshakeTimeout(Config::TLS_HANDSHAKE_TIMEOUT_S);
    begun = http.begin(secureClient, url);
  } else {
    begun = http.begin(plainClient, url);
  }
  if (!begun) {
    logf("[API] bad url: %.80s", url);
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  if (Config::apiKeyConfigured()) http.addHeader("X-API-KEY", Config::API_KEY);

  // POST() takes a non-const pointer but only reads from it.
  const int code =
      http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(payload)), strlen(payload));

  // Only small, known-length (or chunked) bodies are read; this server's
  // answers are tiny JSON documents.
  constexpr int MAX_BODY = 1024;
  if (code > 0 && http.getSize() <= MAX_BODY) {
    const String response = http.getString();
    strncpy(body, response.c_str(), bodySize - 1);
    body[bodySize - 1] = '\0';
  }
  http.end();
  return code;
}

void runJob(const Job &job, ApiResult &result) {
  const bool outbound = job.direction == RfidFrame::Direction::OUT;
  char url[160];
  char payload[160];
  snprintf(url, sizeof(url), "%s%s", Config::API_BASE_URL,
           outbound ? Config::WEIGHT_OUT_PATH : Config::WEIGHT_IN_PATH);
  snprintf(payload, sizeof(payload),
           outbound ? "{\"driver_id\":%ld,\"customer_id\":%ld,\"weight_out\":%.2f,\"time_out\":\"%s\"}"
                    : "{\"driver_id\":%ld,\"customer_id\":%ld,\"weight_in\":%.2f,\"time_in\":\"%s\"}",
           static_cast<long>(job.driverId), static_cast<long>(job.customerId),
           static_cast<double>(job.weightKg), job.timestamp);

  static char body[256];  // this task only
  const uint32_t started = millis();
  int code = 0;
  uint8_t attempts = 0;
  for (;;) {
    ++attempts;
    code = postOnce(url, payload, body, sizeof(body));
    // Retry only what surely never reached the server - see http_outcome.h.
    if (!HttpOutcome::retrySafe(code) || attempts >= Config::HTTP_MAX_ATTEMPTS ||
        millis() - started >= Config::HTTP_RETRY_BUDGET_MS) {
      break;
    }
    logf("[API] attempt %u failed (http=%d), retrying", attempts, code);
    vTaskDelay(pdMS_TO_TICKS(Config::HTTP_RETRY_DELAY_MS));
  }
  const uint32_t duration = millis() - started;

  result.httpCode = code;
  result.attempts = attempts;
  result.verdict = HttpOutcome::classify(code);

  const bool ok = result.verdict == HttpOutcome::Verdict::OK;
  portENTER_CRITICAL(&statsMux);
  stats.lastHttpCode = code;
  stats.lastDurationMs = duration;
  if (ok) {
    ++stats.sendsOk;
    stats.lastSendOkEpoch = wallClockReady() ? static_cast<uint32_t>(time(nullptr)) : 0;
  } else {
    ++stats.sendsFailed;
  }
  portEXIT_CRITICAL(&statsMux);

  const char *dir = outbound ? "OUT" : "IN";
  if (ok) {
    logf("[SEND] %s driver=%ld cust=%ld %.2fkg %s http=%d %lums", dir,
         static_cast<long>(job.driverId), static_cast<long>(job.customerId),
         static_cast<double>(job.weightKg), job.timestamp, code,
         static_cast<unsigned long>(duration));
  } else {
    char reason[64];
    copySafe(body, reason, sizeof(reason));
    logf("[SEND] FAILED %s driver=%ld cust=%ld %.2fkg http=%d (%s) tries=%u %s", dir,
         static_cast<long>(job.driverId), static_cast<long>(job.customerId),
         static_cast<double>(job.weightKg), code, HttpOutcome::verdictName(result.verdict),
         attempts, reason);
  }
}

void workerTask(void *) {
  for (;;) {
    Job job;
    if (xQueueReceive(jobQueue, &job, portMAX_DELAY) != pdTRUE) continue;

    ApiResult result = {};
    result.seq = job.seq;
    result.direction = job.direction;
    runJob(job, result);

    xQueueOverwrite(resultQueue, &result);
    busy = false;
  }
}

}  // namespace

void beginApiClient() {
  jobQueue = xQueueCreate(1, sizeof(Job));
  resultQueue = xQueueCreate(1, sizeof(ApiResult));
  if (!jobQueue || !resultQueue) {
    logf("[API] queue allocation failed, uploads disabled");
    jobQueue = nullptr;
    return;
  }
  // HTTPClient over TLS needs a deep stack; 10 KB leaves ample headroom.
  if (xTaskCreatePinnedToCore(workerTask, "api", 10240, nullptr, 1, nullptr, 0) != pdPASS) {
    logf("[API] task creation failed, uploads disabled");
    jobQueue = nullptr;
  }
}

uint32_t apiSubmitWeight(RfidFrame::Direction direction, int32_t driverId, int32_t customerId,
                         float weightKg, const char *timestamp) {
  if (!jobQueue || busy) return 0;

  Job job = {};
  job.direction = direction;
  job.driverId = driverId;
  job.customerId = customerId;
  job.weightKg = weightKg;
  strncpy(job.timestamp, timestamp, sizeof(job.timestamp) - 1);

  job.seq = nextSeq++;
  if (nextSeq == 0) nextSeq = 1;  // 0 means "rejected"

  // A result nobody collected (the app gave up waiting) must not be taken
  // for this job's.
  ApiResult stale;
  xQueueReceive(resultQueue, &stale, 0);

  busy = true;
  if (xQueueSend(jobQueue, &job, 0) != pdTRUE) {
    busy = false;
    return 0;
  }
  return job.seq;
}

bool apiTakeResult(ApiResult &out) {
  return resultQueue && xQueueReceive(resultQueue, &out, 0) == pdTRUE;
}

bool apiBusy() { return busy; }

ApiStats apiStats() {
  portENTER_CRITICAL(&statsMux);
  ApiStats copy = stats;
  portEXIT_CRITICAL(&statsMux);
  copy.busy = busy;
  return copy;
}
