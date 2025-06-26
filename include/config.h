#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "RTClib.h"
#include "esp_task_wdt.h"
#include <atomic>
// rs485
#define DE_RE_PIN 35
#define RS485_RX_PIN 44

// rs232
#define WEIGHT_RX_PIN 18

// rtc
#define RTC_SDA 8
#define RTC_SCL 9

// bttons
#define OUT_BUTTON 4
#define IN_BUTTON 2
#define RESET_BUTTON 42

// Konstanta
constexpr unsigned long RFID_TIMEOUT = 5000;
constexpr unsigned long STATE_TIMEOUT = 60000;
constexpr unsigned long LOOP_INTERVAL_MS = 10;
extern unsigned long lastReconnectAttempt;
extern int wifiRetryCount;
extern int wifiRetryLimit;
constexpr unsigned long WIFI_CHECK_INTERVAL = 5000;
constexpr unsigned long RECONNECT_INTERVAL = 30000;
const unsigned long WIFI_CONNECTION_TIMEOUT = 30000;
constexpr int MAX_RECONNECT_ATTEMPTS = 5;
constexpr unsigned long ERROR_DISPLAY_DURATION = 2000;

extern int lastButtonPressed;

// Struktur Data
struct RS485Data {
  int id_driver = 0;
  String nama_driver;
  int id_customer = 0;
  String no_polisi;
  String nama_customer;
  bool hasData = false;
};

// Enum State
enum SystemState {
  HOME,
  READING_RFID,
  WEIGHT_DISPLAY,
  ERROR,
  SENDING_DATA
};

enum class WiFiState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    FAILED
};

extern std::atomic<WiFiState> wifiState;

constexpr int TIMEZONE_OFFSET_HOURS = 0;
constexpr int TIME_ADJUST_MINUTES = 0;
constexpr int TIME_ADJUST_SECONDS = 0; 

enum class ErrorCode : uint8_t {
  NONE,               // 0
  RFID_TIMEOUT,       // 1
  RFID_FORMAT,        // 2
  RFID_PARSE_ERROR,   // 3 (tambahkan ini)
  RFID_OVERFLOW,      // 4 (tambahkan ini)
  RFID_NO_DATA,       // 5 (tambahkan ini)
  WIFI_CONNECT,       // 6
  HTTP_FAIL,          // 7
  MEMORY_ERROR,       // 8 (tambahkan ini)
  SYSTEM_FAULT        // 9 (tambahkan ini)
};

enum HttpStatus { 
    HTTP_IDLE, 
    HTTP_SENDING, 
    HTTP_RECEIVING, 
    HTTP_ERROR 
};

extern HttpStatus httpState;
extern bool sendingData;

// Deklarasi objek eksternal
extern U8G2_ST7920_128X64_F_SW_SPI u8g2;
extern RTC_DS3231 rtc;
extern const char* ssid;
extern const char* password;
extern HardwareSerial rs485Serial;
extern HardwareSerial rs232Serial;
extern RS485Data rs485Data;
extern SystemState currentState;
extern ErrorCode lastError;
extern unsigned long lastStateChangeTime;
extern float lastWeight;
extern float weight_in;
extern float weight_out;
extern String time_in;
extern String time_out;

// Deklarasi fungsi utama
void setupWiFi();

void handleWiFi();

void handleMenu();

void resetRFIDData(bool resetHardware);

void parseRS485Data(const String &data);

void resetSystemForError();

void handleHomeState();

void HomeDisplay();

void handleTimeAdjustment();

void handleReadingRfidState();

float readWeight();

void handleWeightDisplayState();

void displayRFIDData(float weight);

void displayWeight();

int sendDataIn(int id_driver, int id_customer, float weight_in, String time_in);

int sendDataOut(int id_driver, int id_customer, float weight_out, String time_out);

void handleSendingDataState();

void serverResponseDisplay(int16_t responseCode);

void handleErrorState();

void displayMessage(const String& line1, const String& line2);

String getCurrentTime();

void logSystemStatus();

bool checkResetButton();

bool checkSendInButton();

bool checkSendOutButton();

#endif