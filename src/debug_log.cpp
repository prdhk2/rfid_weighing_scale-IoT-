#include "debug_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "health_monitor.h"
#include "log_queue_policy.h"

namespace {

struct LogLine {
  char text[LOG_LINE_CAPACITY + 1];
};

QueueHandle_t logQueue = nullptr;
volatile uint32_t queuedCount = 0;
volatile uint32_t droppedCount = 0;

// Several tasks log; Serial output and the drop-oldest step below must not
// interleave between them.
SemaphoreHandle_t logMutex = nullptr;

}  // namespace

void beginDebugLog() {
  logMutex = xSemaphoreCreateMutex();
  logQueue = xQueueCreate(LOG_QUEUE_DEPTH, sizeof(LogLine));
}

void logf(const char *format, ...) {
  LogLine line;
  va_list args;
  va_start(args, format);
  vsnprintf(line.text, sizeof(line.text), format, args);
  va_end(args);

  // The queued copy is embedded verbatim in a JSON string by
  // mqtt_client.cpp, so anything that would need escaping is neutralised.
  for (char *c = line.text; *c; ++c) {
    if (*c == '"' || *c == '\\') *c = '\'';
    else if (static_cast<unsigned char>(*c) < 0x20) *c = ' ';
  }

  const bool locked = logMutex && xSemaphoreTake(logMutex, pdMS_TO_TICKS(50)) == pdTRUE;

  Serial.println(line.text);
  healthMonitorRecord(line.text);  // RTC black box, survives a crash/reboot

  if (logQueue) {
    UBaseType_t occupied = uxQueueMessagesWaiting(logQueue);
    if (LogQueuePolicy::decideEnqueueAction(occupied, LOG_QUEUE_DEPTH) ==
        LogQueuePolicy::Action::DROP_OLDEST_THEN_ENQUEUE) {
      LogLine discarded;
      if (xQueueReceive(logQueue, &discarded, 0) == pdTRUE) ++droppedCount;
    }
    if (xQueueSend(logQueue, &line, 0) == pdPASS) ++queuedCount;
    else ++droppedCount;
  }

  if (locked) xSemaphoreGive(logMutex);
}

size_t drainLogLines(char out[][LOG_LINE_CAPACITY + 1], size_t maxLines) {
  if (!logQueue) return 0;
  size_t copied = 0;
  LogLine line;
  while (copied < maxLines && xQueueReceive(logQueue, &line, 0) == pdTRUE) {
    memcpy(out[copied], line.text, sizeof(line.text));
    ++copied;
  }
  return copied;
}

uint32_t logLinesQueued() { return queuedCount; }
uint32_t logLinesDropped() { return droppedCount; }
