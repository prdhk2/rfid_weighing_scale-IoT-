#include "config.h"

// Variabel global untuk status RTC
bool rtcInitialized = false;
bool rtcAvailable = false;
unsigned long lastRtcCheck = 0;
const unsigned long RTC_RETRY_INTERVAL = 5000; // Coba ulang setiap 5 detik

void setupRTC() {
    Serial.println("Initializing RTC...");
    Wire.begin(RTC_SDA, RTC_SCL);
    Wire.setClock(100000); // Set clock speed untuk efisiensi
    
    // First attempt
    rtcAvailable = rtc.begin();
    if (rtcAvailable) {
        Serial.println("RTC found and initialized");
        rtcInitialized = true;
        
        if (rtc.lostPower()) {
            Serial.println("RTC lost power, setting default time");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
    } else {
        Serial.println("RTC not found, will retry...");
    }
    
    lastRtcCheck = millis();
}

void updateRTC() {
    unsigned long currentMillis = millis();
    
    if (rtcInitialized) return;
    
    if (currentMillis - lastRtcCheck >= RTC_RETRY_INTERVAL) {
        lastRtcCheck = currentMillis;
        
        rtcAvailable = rtc.begin();
        if (rtcAvailable) {
            Serial.println("RTC found on retry!");
            rtcInitialized = true;
        } else {
            Serial.println("RTC still not available");
        }
    }
}

bool isRTCReady() {
    return rtcInitialized && rtcAvailable;
}

String getCurrentTime() {
    if (isRTCReady()) {
        DateTime now = rtc.now();
        char buffer[25];
        sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", 
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
        return String(buffer);
    } else {
        static unsigned long startupTime = millis();
        unsigned long elapsed = millis() - startupTime;
        
        unsigned long seconds = elapsed / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        unsigned long days = hours / 24;
        
        char buffer[25];
        sprintf(buffer, "2024-01-%02lu %02lu:%02lu:%02lu", 
                days % 30 + 1, hours % 24, minutes % 60, seconds % 60);
        return String(buffer);
    }
}