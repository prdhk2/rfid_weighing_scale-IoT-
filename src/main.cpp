#include "config.h"
#include "utils/timeUtils.h"

// LCD ST7920 128x64 setup
U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, /* clock=*/ 7, /* data=*/ 6, /* CS=*/ 5, /* reset=*/ U8X8_PIN_NONE);

// RFID setup
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
bool spiEnabled = true;
bool sendingData = false;

// Variabel global
SystemState currentState = STATE_HOME;
SystemState previousState = STATE_HOME;
// HttpRequest currentHttpRequest;
unsigned long lastStateChangeTime = 0;

WiFiState wifiState = DISCONNECTED;

HttpStatus httpState = HTTP_IDLE;  
unsigned long lastHttpMillis = 0;

int httpResponseCode = 0;

int lastButtonPressed = 0;
float lastWeight = -1;

String currentTime = "";
float weight_in = 0;
float weight_out = 0;
String time_in = "";
String time_out = "";
int id_driver = 0;
int id_customer = 0;
String nama_driver, no_polisi, nama_customer;

const unsigned long LOOP_INTERVAL_MS = 10;

// Variabel animasi
unsigned long lastAnimUpdate = 0;
int animFrame = 0;
const int animSpeed = 500; // ms
int cardPosX = 128;

// Block RFID yang dibaca
int blocks[] = {4, 5, 6, 8, 9};
const int total_blocks = (sizeof(blocks) / sizeof(blocks[0]));

// RTC
RTC_DS3231 rtc;

void setup() {
    esp_task_wdt_init(30, true);

    disableCore0WDT();

    // Optimasi pengaturan WiFi
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, 18, -1, true);

    // WiFi
    setupWiFi();

    
    IPAddress dns(8, 8, 8, 8);  // Google DNS
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns);
    
    // RTC
    Wire.begin(RTC_SDA, RTC_SCL);

    // LCD
    u8g2.begin();
    u8g2.setDrawColor(1);

    // Tombol
    pinMode(IN_BUTTON, INPUT_PULLUP);
    pinMode(OUT_BUTTON, INPUT_PULLUP);
    pinMode(RESET_BUTTON, INPUT_PULLUP);
    
    if (!rtc.begin()) {
        Serial.println("RTC tidak ditemukan!");
        while (1);  // Hentikan eksekusi
    }

    if (rtc.lostPower()) {
        Serial.println("RTC kehilangan daya, menyetel waktu...");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    // RFID
    SPI.begin();
    mfrc522.PCD_Init();
    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

}

void loop() {
    handleWiFi();
    handleTimeAdjustment();

    unsigned long now = millis();
    static unsigned long lastLoopTime = 0;
    if (now - lastLoopTime >= LOOP_INTERVAL_MS) {
        lastLoopTime = now;

        logSystemStatus();
        handleMenu();
    }
}

void handleMenu() {
    switch (currentState) {
        case STATE_HOME:
            handleHomeState();
            break;
        case STATE_READING_RFID:
            handleReadingRFIDState();
            break;
        case STATE_WEIGHT_DISPLAY:
            handleWeightDisplayState();
            break;
        case STATE_ERROR:
            handleErrorState();
            break;
        case STATE_SENDING_DATA:
            handleSendingDataState();
            break;
        default:
            handleUnknownState();
            break;
    }
}

void disableRFID() {
    Serial.println("Matikan SPI/RFID sementara...");
    mfrc522.PCD_Reset();     // Reset RFID
    SPI.end();               // Matikan SPI
    spiEnabled = false;
}

void enableRFID() {
    if (!spiEnabled) {
        Serial.println("Aktifkan kembali SPI/RFID...");
        SPI.begin();
        mfrc522.PCD_Init();  // Inisialisasi ulang RFID
        spiEnabled = true;
    }
}

void handleUnknownState() {
    Serial.println("Unknown state detected");
}

String getCurrentTime() {
    DateTime now = getAdjustedTime(rtc.now());

    char buffer[25];
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", 
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());

    return String(buffer);
}

void displayMessage(String line1, String line2) {
    u8g2.clearBuffer();
    u8g2.setCursor(0, 15); u8g2.print(line1);
    u8g2.setCursor(0, 35); u8g2.print(line2);
    u8g2.sendBuffer();

    yield();
}

void resetButtons() {
    while (digitalRead(IN_BUTTON) == LOW || digitalRead(OUT_BUTTON) == LOW) {
        delay(10);
        yield();
    }
}

void handleHomeState() {
    HomeDisplay();
    
    if (millis() - lastStateChangeTime > 30000) {
        cardPosX = 128;
    }

    checkRFIDPresence();
}

void logSystemStatus() {
    static unsigned long lastLogTime = 0;
    const unsigned long logInterval = 60000;

    if (millis() - lastLogTime > logInterval) {
        lastLogTime = millis();

        Serial.println("\n===== System Status =====");
        Serial.printf("WiFi Status: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
        Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
        Serial.printf("Heap Free: %d bytes\n", ESP.getFreeHeap());
        Serial.printf("Uptime: %lu minutes\n", millis() / 60000);
        Serial.printf("Min Free Heap: %d bytes\n", ESP.getMinFreeHeap());
        Serial.printf("Max Alloc Heap: %d bytes\n", ESP.getMaxAllocHeap());
        Serial.println("=========================");
    }
}