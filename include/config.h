#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "RTClib.h"
#include "esp_task_wdt.h"
#include <atomic>
#include "WiFi.h"


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
constexpr unsigned long MENU_INTERVAL = 10;
extern unsigned long lastReconnectAttempt;
extern int wifiRetryCount;
extern int wifiRetryLimit;
constexpr int MAX_RECONNECT_ATTEMPTS = 5;
constexpr unsigned long ERROR_DISPLAY_DURATION = 2000;

#define WIFI_SCAN_INTERVAL 30000

// Watchdog timeout
#define WATCHDOG_TIMEOUT 35 

extern int lastButtonPressed;

// Struktur Data
struct RS485Data {
    String slave_id;
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

constexpr int TIMEZONE_OFFSET_HOURS = 0;
constexpr int TIME_ADJUST_MINUTES = 0;
constexpr int TIME_ADJUST_SECONDS = 0; 

enum class ErrorCode : uint8_t {
    NONE,
    RFID_TIMEOUT,
    RFID_FORMAT,
    RFID_PARSE_ERROR,
    RFID_OVERFLOW,
    WIFI_CONNECT,
    HTTP_FAIL,
    WEIGHT_INVALID,
    RFID_COMM_ERROR
};

enum HttpStatus { 
    HTTP_IDLE, 
    HTTP_SENDING, 
    HTTP_RECEIVING, 
    HTTP_ERROR 
};

struct HttpRequestData {
    String url;
    String jsonData;
    int retryCount;
    unsigned long lastAttemptTime;
    int httpResponseCode;
    bool inProgress;
};

extern int httpResponseCode;

enum class ResponseState {
    NONE,
    SHOWING_RESPONSE,
    COMPLETED
};

extern ResponseState responseState;
extern unsigned long responseStartTime;
extern int16_t lastHttpResponseCode;

enum HttpRequestType {
    NONE,
    WEIGHT_IN,
    WEIGHT_OUT
};

extern HttpStatus httpState;
extern bool sendingData;

enum WiFiState {
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_ERROR
};

extern WiFiState wifiState;

extern unsigned long lastWifiAttempt;
extern unsigned long lastWifiCheck;
extern unsigned long wifiConnectStartTime;

#define WIFI_CHECK_INTERVAL 2000   // Cek setiap 2 detik
#define WIFI_RECONNECT_INTERVAL 10000 // Coba reconnect setiap 10 detik
#define WIFI_CONNECT_TIMEOUT 30000

// Deklarasi objek eksternal
extern bool wifiNeedUpdate;
extern U8G2_ST7920_128X64_F_SW_SPI u8g2;
extern RTC_DS3231 rtc;
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

extern bool rtcInitialized;
extern bool rtcAvailable;
extern unsigned long lastRtcCheck;

// Deklarasi fungsi
void setupRTC();
void updateRTC();
String getCurrentTime();
bool isRTCReady();

bool isWiFiConnected();

void setupWiFi();

void handleWiFi();

void handleMenu();

void handleRS485();

void resetRFIDData(bool resetHardware);

void parseRS485Data(const String &data);

void resetSystemForError();

// void setWifiNeedUpdate(bool state);

const char *getQuickWifiStatus();

void handleHomeState();

void HomeDisplay();

void handleTimeAdjustment();

void handleReadingRfidState();

float readWeight();

void handleWeightDisplayState();

void displayRFIDData(float weight);

void displayWeight();

int startSendDataIn(int id_driver, int id_customer, float weight_in, String time_in);

int startSendDataOut(int id_driver, int id_customer, float weight_out, String time_out);

void handleSendingDataState();

void processHttpRequests();

bool isHttpRequestInProgress();

void finishHttpRequest();

void handleResponseDisplay();

void startResponseDisplay(int16_t responseCode);

void showResponseScreen(int16_t responseCode);

void handleErrorState();

void displayMessage(const String& line1, const String& line2);

String getCurrentTime();

// void logSystemStatus();

bool checkResetButton();

bool checkSendInButton();

bool checkSendOutButton();

#endif