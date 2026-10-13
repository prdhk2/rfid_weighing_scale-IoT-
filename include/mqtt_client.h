#pragma once

#include <Arduino.h>

// MQTT link to the fleet broker (monitoring.xampah.com:8883, TLS), on its own
// FreeRTOS task pinned to core 0 and deliberately not on the task watchdog:
// an OTA download runs inside this task and may block for up to ~90 s.
//
// Topics (prefix = Config::SITE, id = Config::DEVICE_ID):
//   depok_truk/<id>/status  publish, retained, LWT {"online":false}:
//                           device, scale, RFID, clock, upload and screen state
//                           (see buildStatus())
//   depok_truk/<id>/log     publish, batched debug_log lines; OTA progress;
//                           the previous boot's black box as "[PREV] ..."
//   depok_truk/<id>/cmd     subscribe: {"cmd":"PING"|"STATUS"|"RESTART"|"OTA",...}
//
// Reconnects forever at a fixed interval, like Wi-Fi.
void beginMqtt();

bool mqttConnected();
uint32_t mqttReconnects();
