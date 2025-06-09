#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "RTClib.h"
#include "extractNumericPart.h"
#include "readWeight.h"
#include "esp_task_wdt.h"

// LCD ST7920 128x64 setup
extern U8G2_ST7920_128X64_F_SW_SPI u8g2;

// RFID setup
#define RST_PIN  16
#define SS_PIN   10

// Button pins
#define OUT_BUTTON 4
#define IN_BUTTON 2
#define RESET_BUTTON 17

// Pin RTC
#define RTC_SDA 8
#define RTC_SCL 9
#pragma once

constexpr int TIMEZONE_OFFSET_HOURS = 0;
constexpr int TIME_ADJUST_MINUTES = 0;
constexpr int TIME_ADJUST_SECONDS = 0; 

extern MFRC522 mfrc522;
extern MFRC522::MIFARE_Key key;
extern bool spiEnabled;
extern bool sendingData;

// WiFi & API
extern const char* ssid;
extern const char* password;
// extern const char* apiWeightInUrl;
// extern const char* apiWeightOutUrl;

extern unsigned long lastReconnectAttempt;
extern const unsigned long RECONNECT_INTERVAL;

enum WiFiState { 
    DISCONNECTED, 
    CONNECTING, 
    CONNECTED 
};
extern WiFiState wifiState;

// Enum untuk state sistem
enum SystemState {
    STATE_HOME,
    STATE_READING_RFID,
    STATE_WEIGHT_DISPLAY,
    STATE_ERROR,
    STATE_SENDING_DATA
};

enum HttpStatus { 
    HTTP_IDLE, 
    HTTP_SENDING, 
    HTTP_RECEIVING, 
    HTTP_ERROR 
};

// Variabel global
extern SystemState currentState;
extern SystemState previousState;
// extern HttpRequest currentHttpRequest;
extern unsigned long lastStateChangeTime;
extern HttpStatus httpState;

extern int lastButtonPressed;

extern String currentTime;
extern float weight_in;
extern float weight_out;
extern String time_in;
extern String time_out;
extern int id_driver;
extern int id_customer;
extern String nama_driver,no_polisi, nama_customer;

extern const unsigned long LOOP_INTERVAL_MS;

// Variabel animasi
extern unsigned long lastAnimUpdate;
extern int animFrame;
extern const int animSpeed; // ms
extern int cardPosX;

// Block RFID yang dibaca
extern int blocks[5];
extern const int total_blocks;

extern RTC_DS3231 rtc;

// Deklarasi fungsi

void setupWiFi();

void handleWiFi();

void printWiFiDiagnostics();

void setupDisplay();

void handleMenu();

void loadingDisplay();

void HomeDisplay();

void handleTimeAdjustment();

void handleHomeState();

void displayMessage(String line1, String line2);

void drawBlinkingText();

void drawCompanyLogo();

String getCurrentTime();

void handleSendingDataState();

void handleUnknownState();

bool readRFIDData();

void handleReadingRFIDState();

void handleWeightDisplayState();

void resetRFIDData(bool resetHardware = true);

void checkRFIDPresence();

void displayRFIDData(float weight);

void displayWeight();

void prepareHttpRequest(String url, String payload);

void disableRFID();

void enableRFID();

bool checkResetButton();

bool checkSendInButton();

bool checkSendOutButton();

void resetButtons();

int sendDataIn(int id_driver, int id_customer, float weight_in, String time_in);

int sendDataOut(float weight_out, String time_out);

bool processHttpRequest();

void serverResponseDisplay(int16_t responseCode);

void handleErrorState();

// void handleSendingDataState();

void logSystemStatus();

void rfidTask(void *pvParameters);

void drawPStr(u8g2_uint_t x, u8g2_uint_t y, const char* pstr);

#endif