#include "mqtt_client.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstdarg>
#include <cstring>
#include <esp_system.h>
#include <time.h>

#include "api_client.h"
#include "app.h"
#include "clock.h"
#include "config.h"
#include "debug_log.h"
#include "health_monitor.h"
#include "json_extract.h"
#include "mqtt_ca_cert.h"
#include "network_service.h"
#include "ota_manager.h"
#include "peripherals.h"
#include "reset_reason.h"

// ResetReason mirrors esp_reset_reason_t by value (reset_reason.h).
static_assert(static_cast<int>(ESP_RST_POWERON) == ResetReason::POWERON, "reset code drift");
static_assert(static_cast<int>(ESP_RST_SW) == ResetReason::SW, "reset code drift");
static_assert(static_cast<int>(ESP_RST_PANIC) == ResetReason::PANIC, "reset code drift");
static_assert(static_cast<int>(ESP_RST_TASK_WDT) == ResetReason::TASK_WDT, "reset code drift");
static_assert(static_cast<int>(ESP_RST_BROWNOUT) == ResetReason::BROWNOUT, "reset code drift");

namespace {

constexpr size_t MAX_LINES_PER_BATCH = 8;   // 8 x 128 chars fits MQTT_PACKET_SIZE
constexpr size_t PREV_LINES_PER_BATCH = 6;  // "[PREV] " makes each line longer
constexpr uint16_t KEEPALIVE_S = 30;        // survives the pauses inside an OTA download
constexpr size_t CMD_BUFFER_SIZE = 512;     // OTA command: ~150-char URL + 64-char hash + keys
constexpr uint32_t TASK_STACK = 12288;      // TLS + PubSubClient + the OTA download

char statusTopic[64];
char logTopic[64];
char cmdTopic[64];

volatile bool connected = false;
volatile uint32_t reconnects = 0;
bool confirmedHealthy = false;
TaskHandle_t taskHandle = nullptr;

// Used only from the MQTT task.
char payload[Config::MQTT_PACKET_SIZE];
PubSubClient *activeClient = nullptr;

const char *bootResetName() {
  static const char *name = ResetReason::name(static_cast<int>(esp_reset_reason()));
  return name;
}

unsigned long wallClockOrZero() {
  return wallClockReady() ? static_cast<unsigned long>(time(nullptr)) : 0UL;
}

const char *dirName(RfidFrame::Direction d) {
  return d == RfidFrame::Direction::OUT ? "OUT" : "IN";
}

const char *clockSourceName(ClockSource s) {
  switch (s) {
    case ClockSource::RTC: return "RTC";
    case ClockSource::NTP: return "NTP";
    default: return "NONE";
  }
}

// Bounded append into `payload`. Returns false once it would overflow.
bool append(size_t &used, const char *format, ...) __attribute__((format(printf, 2, 3)));
bool append(size_t &used, const char *format, ...) {
  if (used >= sizeof(payload)) return false;
  va_list args;
  va_start(args, format);
  int written = vsnprintf(payload + used, sizeof(payload) - used, format, args);
  va_end(args);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(payload) - used) return false;
  used += static_cast<size_t>(written);
  return true;
}

// The retained status: what the fleet dashboard shows as this device's health.
//
//   {"online":true,"uptime_s":..,"fw":"3.0.0","site":"depok_truk","heap":..,
//    "minheap":..,"rssi":..,"ts":..,"reset":"POWERON",
//    "diag":{"kind":"depok_truk","ota_trial":false,"restart_pending":false,
//      "state":"WEIGHT",
//      "card":{"dir":"IN","driver_id":12,"customer_id":3,"nopol":"B 1234 XY"},
//      "scale":{"state":"LIVE","age_s":0,"frames":812,"rejected":0,"weight":12340.00},
//      "rfid":{"cards":5,"rejected":0,"last_age_s":40},
//      "clock":{"source":"RTC","rtc":true,"rtc_ok":true,"ntp_syncs":1,"drift_s":0,
//               "last_sync_ts":..},
//      "api":{"sends_ok":9,"sends_failed":1,"last_http":200,"last_send_ok_ts":..,
//             "last_ms":840,"busy":false},
//      "last_send":{"dir":"IN","weight":12340.00,"http":200,"age_s":40},
//      "maxblk":..,"stk_mqtt":..,"wifi_reconnects":0,"mqtt_reconnects":1,
//      "net_restarts":0,"log_dropped":0}}
//
// scale.state: LIVE (reading within Config::SCALE_STALE_MS), STALE (heard
// before, silent now), OFFLINE (never heard since boot); "weight" is null
// unless LIVE. "card" / "last_send" are null when there is none.
bool buildStatus() {
  const uint32_t now = millis();
  const ScaleSnapshot scale = scaleSnapshot(now);
  const RfidSnapshot rfid = rfidSnapshot(now);
  const ApiStats api = apiStats();
  const AppStatus app = appStatus();
  const ClockStatus clock = clockStatus();

  size_t used = 0;
  bool ok = append(used,
                   "{\"online\":true,\"uptime_s\":%lu,\"fw\":\"%s\",\"site\":\"%s\","
                   "\"heap\":%u,\"minheap\":%u,\"rssi\":%d,\"ts\":%lu,\"reset\":\"%s\","
                   "\"diag\":{\"kind\":\"%s\",\"ota_trial\":%s,\"restart_pending\":%s,"
                   "\"state\":\"%s\"",
                   static_cast<unsigned long>(now / 1000UL), Config::FIRMWARE_VERSION, Config::SITE,
                   static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
                   static_cast<int>(WiFi.RSSI()), wallClockOrZero(), bootResetName(), Config::SITE,
                   OtaManager::onTrial() ? "true" : "false", restartPending() ? "true" : "false",
                   app.stateName);

  // The plate was made JSON-safe by the RFID parser (rfid_frame.h).
  if (app.hasCard) {
    ok = ok && append(used, ",\"card\":{\"dir\":\"%s\",\"driver_id\":%ld,\"customer_id\":%ld,\"nopol\":\"%s\"}",
                      dirName(app.direction), static_cast<long>(app.driverId),
                      static_cast<long>(app.customerId), app.plate);
  } else {
    ok = ok && append(used, ",\"card\":null");
  }

  const char *scaleState = scale.live ? "LIVE" : (scale.everReceived ? "STALE" : "OFFLINE");
  ok = ok && append(used, ",\"scale\":{\"state\":\"%s\",\"age_s\":%ld,\"frames\":%lu,\"rejected\":%lu",
                    scaleState, scale.everReceived ? static_cast<long>(scale.ageMs / 1000UL) : -1L,
                    static_cast<unsigned long>(scale.frames),
                    static_cast<unsigned long>(scale.rejected));
  if (scale.live) {
    ok = ok && append(used, ",\"weight\":%.2f}", static_cast<double>(scale.weightKg));
  } else {
    ok = ok && append(used, ",\"weight\":null}");
  }

  ok = ok && append(used, ",\"rfid\":{\"cards\":%lu,\"rejected\":%lu,\"last_age_s\":%ld}",
                    static_cast<unsigned long>(rfid.cards), static_cast<unsigned long>(rfid.rejected),
                    rfid.lastCardAgeMs == UINT32_MAX ? -1L
                                                     : static_cast<long>(rfid.lastCardAgeMs / 1000UL));

  ok = ok && append(used,
                    ",\"clock\":{\"source\":\"%s\",\"rtc\":%s,\"rtc_ok\":%s,\"ntp_syncs\":%lu,"
                    "\"drift_s\":%ld,\"last_sync_ts\":%lu}",
                    clockSourceName(clock.source), clock.rtcPresent ? "true" : "false",
                    clock.rtcSane ? "true" : "false", static_cast<unsigned long>(clock.ntpSyncs),
                    static_cast<long>(clock.lastDriftS),
                    static_cast<unsigned long>(clock.lastNtpSyncEpoch));

  ok = ok && append(used,
                    ",\"api\":{\"sends_ok\":%lu,\"sends_failed\":%lu,\"last_http\":%d,"
                    "\"last_send_ok_ts\":%lu,\"last_ms\":%lu,\"busy\":%s}",
                    static_cast<unsigned long>(api.sendsOk),
                    static_cast<unsigned long>(api.sendsFailed), api.lastHttpCode,
                    static_cast<unsigned long>(api.lastSendOkEpoch),
                    static_cast<unsigned long>(api.lastDurationMs), api.busy ? "true" : "false");

  if (app.lastSendValid) {
    ok = ok && append(used, ",\"last_send\":{\"dir\":\"%s\",\"weight\":%.2f,\"http\":%d,\"age_s\":%lu}",
                      dirName(app.lastSendDirection), static_cast<double>(app.lastSendWeightKg),
                      app.lastSendHttp, static_cast<unsigned long>(app.lastSendAgeMs / 1000UL));
  } else {
    ok = ok && append(used, ",\"last_send\":null");
  }

  ok = ok && append(used,
                    ",\"maxblk\":%u,\"stk_mqtt\":%u,\"wifi_reconnects\":%lu,\"mqtt_reconnects\":%lu,"
                    "\"net_restarts\":%u,\"log_dropped\":%lu}}",
                    static_cast<unsigned>(ESP.getMaxAllocHeap()),
                    taskHandle ? static_cast<unsigned>(uxTaskGetStackHighWaterMark(taskHandle)) : 0u,
                    static_cast<unsigned long>(wifiReconnects()),
                    static_cast<unsigned long>(reconnects), healthConsecutiveNetRestarts(),
                    static_cast<unsigned long>(logLinesDropped()));
  return ok;
}

bool publishStatus(PubSubClient &client) {
  if (!buildStatus()) {
    // Never publish truncated JSON. Fall back to the minimal shape so the
    // device still shows online and the boot confirmation below still runs.
    logf("[MQTT] status payload overflow, sending minimal status");
    snprintf(payload, sizeof(payload),
             "{\"online\":true,\"uptime_s\":%lu,\"fw\":\"%s\",\"site\":\"%s\",\"heap\":%u,"
             "\"rssi\":%d,\"ts\":%lu,\"reset\":\"%s\"}",
             static_cast<unsigned long>(millis() / 1000UL), Config::FIRMWARE_VERSION, Config::SITE,
             static_cast<unsigned>(ESP.getFreeHeap()), static_cast<int>(WiFi.RSSI()),
             wallClockOrZero(), bootResetName());
  }
  if (!client.publish(statusTopic, payload, true)) return false;

  healthMonitorMqttHealthy();

  // An image is healthy once it has stayed up a minute AND reached the
  // server; only then does it end an OTA trial (see ota_rollback_logic.h).
  if (!confirmedHealthy && millis() >= Config::BOOT_HEALTHY_AFTER_MS) {
    OtaManager::confirmBootHealthy();
    confirmedHealthy = true;
  }
  return true;
}

void drainAndPublishLogs(PubSubClient &client) {
  static char lines[MAX_LINES_PER_BATCH][LOG_LINE_CAPACITY + 1];
  size_t count = drainLogLines(lines, MAX_LINES_PER_BATCH);
  if (count == 0) return;

  size_t used = 0;
  bool ok = append(used, "{\"lines\":[");
  for (size_t i = 0; i < count && ok; ++i) {
    // Lines were sanitised by logf(): no quotes, backslashes or control chars.
    ok = append(used, "%s\"%s\"", i ? "," : "", lines[i]);
  }
  ok = ok && append(used, "],\"ts\":%lu}", wallClockOrZero());
  if (ok) client.publish(logTopic, payload);
}

// The previous boot's black box (health_monitor.h), one batch per call, so
// the last lines before a crash, watchdog, brownout or supervisor reboot are
// visible remotely. Returns true once everything has gone out; a failed
// publish is retried on the next call.
bool publishPrevBootReport(PubSubClient &client) {
  static size_t cursor = 0;  // 0 = summary line, then 1..N = black-box lines
  if (prevBootSummary()[0] == '\0') return true;
  const size_t total = prevBootLineCount();

  size_t used = 0;
  bool ok = append(used, "{\"lines\":[");
  size_t next = cursor;
  for (size_t n = 0; n < PREV_LINES_PER_BATCH && next <= total && ok; ++n, ++next) {
    char line[LOG_LINE_CAPACITY + 16];
    if (next == 0) snprintf(line, sizeof(line), "%s", prevBootSummary());
    else snprintf(line, sizeof(line), "[PREV] %s", prevBootLine(next - 1));
    // Black-box lines were sanitised by logf() before they were recorded.
    ok = append(used, "%s\"%s\"", n ? "," : "", line);
  }
  ok = ok && append(used, "],\"ts\":%lu}", wallClockOrZero());
  if (!ok || !client.publish(logTopic, payload)) return false;

  cursor = next;
  if (cursor <= total) return false;
  releasePrevBoot();
  return true;
}

// OTA progress goes straight to the log topic - the log queue cannot be
// drained while this task is busy downloading - as the one fixed-format line
// the server parses (DeviceTelemetryIngestor::OTA_PROGRESS_PATTERN):
//   [OTA] progress stage=downloading pct=42 detail=
void publishOtaProgress(const char *stage, int pct, const char *detail) {
  char line[LOG_LINE_CAPACITY + 1];
  snprintf(line, sizeof(line), "[OTA] progress stage=%s pct=%d detail=%s", stage, pct,
           detail ? detail : "");
  for (char *c = line; *c; ++c) {
    if (*c == '"' || *c == '\\') *c = '\'';
    else if (static_cast<unsigned char>(*c) < 0x20) *c = ' ';
  }
  Serial.println(line);
  if (!activeClient) return;

  char message[LOG_LINE_CAPACITY + 64];
  snprintf(message, sizeof(message), "{\"lines\":[\"%s\"],\"ts\":%lu}", line, wallClockOrZero());
  activeClient->publish(logTopic, message);
}

void handleOta(const char *json) {
  OtaManager::Command command = {};
  char site[24] = {0};
  bool fields = JsonExtract::string(json, "url", command.url, sizeof(command.url)) &&
                JsonExtract::string(json, "sha256", command.sha256, sizeof(command.sha256)) &&
                JsonExtract::string(json, "version", command.version, sizeof(command.version));
  if (!fields) {
    logf("[MQTT] OTA command missing url/sha256/version");
    publishOtaProgress("failed", -1, "OTA command missing url/sha256/version");
    return;
  }
  // The image must be built for this hardware: an OTA meant for another
  // site's devices would flash firmware that drives these pins wrongly.
  if (!JsonExtract::string(json, "site", site, sizeof(site)) || strcmp(site, Config::SITE) != 0) {
    logf("[OTA] rejected: release is for site '%s', this device is '%s'", site, Config::SITE);
    publishOtaProgress("failed", -1, "release is for a different site");
    return;
  }
  if (OtaManager::updateStaged() || restartPending()) {
    logf("[OTA] rejected: a restart is already pending");
    publishOtaProgress("failed", -1, "restart already pending, retry after reboot");
    return;
  }

  // Blocks this task for up to ~90 s by design; the loop task (weighing,
  // display, uploads on their own task) keeps running.
  if (!OtaManager::applyUpdate(command, publishOtaProgress)) return;

  // Flashed and verified. Reboot once nobody is weighing; the "rebooting"
  // progress line is sent by health_monitor right before it happens.
  publishOtaProgress("verifying", 100, "flashed, reboot when scale idle");
  requestRestart("OTA update applied", Config::OTA_REBOOT_IDLE_WAIT_MS, command.version);
}

void handleCommand(const char *json, PubSubClient &client) {
  char cmd[16] = {0};
  if (!JsonExtract::string(json, "cmd", cmd, sizeof(cmd))) {
    logf("[MQTT] cmd payload missing 'cmd' field");
    return;
  }

  if (strcmp(cmd, "PING") == 0 || strcmp(cmd, "STATUS") == 0) {
    logf("[MQTT] %s received", cmd);
    publishStatus(client);
  } else if (strcmp(cmd, "RESTART") == 0) {
    requestRestart("remote RESTART command", Config::RESTART_IDLE_WAIT_MS);
  } else if (strcmp(cmd, "OTA") == 0) {
    handleOta(json);
  } else {
    logf("[MQTT] unrecognised cmd: %s", cmd);
  }
}

void mqttTask(void *) {
  WiFiClientSecure secureClient;
  secureClient.setCACert(ISRG_ROOT_X1_PEM);
  // Library defaults are a 120 s handshake and 30 s socket timeout - on a
  // lossy link that kept the task inside connect() for minutes at a time.
  secureClient.setHandshakeTimeout(20);
  secureClient.setTimeout(15);

  PubSubClient client(secureClient);
  client.setServer(Config::MQTT_BROKER_HOST, Config::MQTT_BROKER_PORT);
  client.setBufferSize(Config::MQTT_PACKET_SIZE);
  client.setKeepAlive(KEEPALIVE_S);
  client.setSocketTimeout(10);
  activeClient = &client;

  client.setCallback([](char *topic, uint8_t *data, unsigned int length) {
    (void)topic;
    static char buffer[CMD_BUFFER_SIZE];
    if (length >= sizeof(buffer)) {
      logf("[MQTT] cmd payload too large (%u bytes), ignored", length);
      return;
    }
    memcpy(buffer, data, length);
    buffer[length] = '\0';
    handleCommand(buffer, *activeClient);
  });

  uint32_t lastConnectAttempt = 0;
  uint32_t lastStatusPublish = 0;
  uint32_t lastLogDrain = 0;
  bool firstAttempt = true;
  bool bootAnnounced = false;
  bool prevBootReported = false;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connected = false;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (!client.connected()) {
      connected = false;
      uint32_t now = millis();
      if (firstAttempt || now - lastConnectAttempt >= Config::MQTT_RECONNECT_MS) {
        firstAttempt = false;
        lastConnectAttempt = now;
        bool ok = client.connect(Config::DEVICE_ID, Config::MQTT_USERNAME, Config::MQTT_PASSWORD,
                                 statusTopic, 1, true, "{\"online\":false}");
        if (ok) {
          ++reconnects;
          client.subscribe(cmdTopic, 1);
          connected = true;
          logf("[MQTT] connected to %s", Config::MQTT_BROKER_HOST);
          if (!bootAnnounced) {
            bootAnnounced = true;
            logf("[BOOT] fw %s reset=%s, online after %lus", Config::FIRMWARE_VERSION,
                 bootResetName(), static_cast<unsigned long>(now / 1000UL));
          }
          publishStatus(client);
          lastStatusPublish = millis();
        } else {
          logf("[MQTT] connect failed, rc=%d, heap=%u maxblk=%u, retry in %lus", client.state(),
               static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
               static_cast<unsigned long>(Config::MQTT_RECONNECT_MS / 1000UL));
        }
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    connected = true;
    client.loop();

    uint32_t now = millis();
    if (now - lastStatusPublish >= Config::MQTT_STATUS_INTERVAL_MS) {
      publishStatus(client);
      lastStatusPublish = now;
    }
    if (now - lastLogDrain >= Config::MQTT_LOG_DRAIN_INTERVAL_MS) {
      if (!prevBootReported) prevBootReported = publishPrevBootReport(client);
      drainAndPublishLogs(client);
      lastLogDrain = now;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

}  // namespace

void beginMqtt() {
  snprintf(statusTopic, sizeof(statusTopic), "%s/%s/status", Config::SITE, Config::DEVICE_ID);
  snprintf(logTopic, sizeof(logTopic), "%s/%s/log", Config::SITE, Config::DEVICE_ID);
  snprintf(cmdTopic, sizeof(cmdTopic), "%s/%s/cmd", Config::SITE, Config::DEVICE_ID);

  if (!Config::mqttConfigured()) {
    logf("[MQTT] broker credentials not set, remote status/OTA disabled");
    return;
  }
  if (xTaskCreatePinnedToCore(mqttTask, "mqtt", TASK_STACK, nullptr, 1, &taskHandle, 0) != pdPASS) {
    logf("[MQTT] task creation failed, remote status/OTA disabled");
    taskHandle = nullptr;
  }
}

bool mqttConnected() { return connected; }
uint32_t mqttReconnects() { return reconnects; }
