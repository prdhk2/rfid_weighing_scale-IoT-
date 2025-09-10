#include "config.h"

// Inisialisasi objek
U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, /* clock=*/ 7, /* data=*/ 6, /* CS=*/ 5, /* reset=*/ U8X8_PIN_NONE);
RTC_DS3231 rtc;
HardwareSerial rs485Serial(1);
HardwareSerial rs232Serial(2);
RS485Data rs485Data;

HttpStatus httpState = HTTP_IDLE;  
unsigned long lastHttpMillis = 0;

int httpResponseCode = 0;

// Variabel state
SystemState currentState = SystemState::HOME;
ErrorCode lastError = ErrorCode::NONE;

bool sendingData = false;
unsigned long lastStateChangeTime = 0;
int lastButtonPressed = 0;

//weight
float lastWeight = -1;
float weight_in = 0;
float weight_out = 0;

const unsigned long RTC_RETRY_INTERVAL = 5000;

static unsigned long lastMenuTime = 0;

void setup() {
    
    Serial.begin(115200);

    // Inisialisasi WiFi
    setupWiFi();

    // Inisialisasi Display
    u8g2.begin();
    u8g2.setBusClock(100000);

    // Serial RS232 & RS485
    rs232Serial.begin(9600, SERIAL_8N1, WEIGHT_RX_PIN, -1, true);
    rs485Serial.begin(9600, SERIAL_8N1, RS485_RX_PIN, -1);
    pinMode(DE_RE_PIN, OUTPUT);
    digitalWrite(DE_RE_PIN, LOW);

    // // RTC
    setupRTC();

    // Tombol
    pinMode(IN_BUTTON, INPUT_PULLUP);
    pinMode(OUT_BUTTON, INPUT_PULLUP);
    pinMode(RESET_BUTTON, INPUT_PULLUP);

    lastStateChangeTime = millis();

    esp_task_wdt_init(35, true);
    esp_task_wdt_add(NULL); // Tambahkan task utama ke WDT
}

void loop() {
    // Task prioritas tinggi
    esp_task_wdt_reset();
    
    readWeight();

    handleRS485();
    
    // WiFi handling (ringan)
    handleWiFi();
    
    // HTTP processing
    processHttpRequests();
    
    // Menu handling (jarang)
    if (millis() - lastMenuTime >= MENU_INTERVAL) {
        lastMenuTime = millis();
        handleMenu();
    }

    updateRTC(); // Non-blocking RTC update
    
    // Yield efficient
    static unsigned long lastYield = 0;
    if (millis() - lastYield >= 2) {
        lastYield = millis();
        yield();
    }
}

void handleMenu() {
    switch (currentState) {
        case SystemState::HOME: 
            handleHomeState(); 
            break;
        case SystemState::READING_RFID: 
            handleReadingRfidState(); 
            break;
        case SystemState::WEIGHT_DISPLAY:
            handleWeightDisplayState();
            break;
        case SystemState::SENDING_DATA:
            handleSendingDataState();
            break;
        case SystemState::ERROR:
            handleErrorState();
            break;
        default:
            currentState = SystemState::HOME;
            break;
    }
}

void displayMessage(const String& line1, const String& line2) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 15, line1.c_str());
    u8g2.drawStr(0, 35, line2.c_str());
    u8g2.sendBuffer();
}