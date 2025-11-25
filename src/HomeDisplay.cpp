#include "config.h"
#include "utils/timeUtils.h"

TimeAdjustState adjustState = IDLE;
int adjHour, adjMinute, adjSecond;
bool blinkState = true;
unsigned long lastBlink = 0;
unsigned long resetPressStart = 0;
bool resetButtonHandled = false;
const unsigned long holdThreshold = 3000;
unsigned long lastInPress = 0;
bool inButtonPressed = false;
bool inButtonHandled = false;
const unsigned long debounceDelay = 50;

unsigned long lastDisplayUpdate = 0;
#define DISPLAY_UPDATE_INTERVAL 100 // Update display setiap 100ms

void handleHomeState() {
    if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
        lastDisplayUpdate = millis();
        HomeDisplay();
    }
    
    handleTimeAdjustment();
    
    static unsigned long lastSerialCheck = 0;
    if (millis() - lastSerialCheck > 5000) { // 5 detik
        lastSerialCheck = millis();
        Serial.printf("Free Heap: %d bytes | RS485: %d\n", 
                     ESP.getFreeHeap(), rs485Serial.available());
    }
    
    if (rs485Serial.available() > 0) {
        currentState = SystemState::READING_RFID;
        lastStateChangeTime = millis();
        Serial.println("Data RS485 terdeteksi!");
    }
}

void HomeDisplay() {
    DateTime now = rtc.now();

    int hour = (adjustState != IDLE) ? adjHour : now.hour();
    int minute = (adjustState != IDLE) ? adjMinute : now.minute();
    int second = (adjustState != IDLE) ? adjSecond : now.second();

    // Blink handling
    if (millis() - lastBlink > 500) {
        blinkState = !blinkState;
        lastBlink = millis();
    }

    // Format waktu
    char timeStr[9];
    if (adjustState == ADJUST_HOUR && !blinkState)
        snprintf(timeStr, sizeof(timeStr), "  :%02d:%02d", minute, second);
    else if (adjustState == ADJUST_MINUTE && !blinkState)
        snprintf(timeStr, sizeof(timeStr), "%02d:  :%02d", hour, second);
    else if (adjustState == ADJUST_SECOND && !blinkState)
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:  ", hour, minute);
    else
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", hour, minute, second);

    // Format tanggal
    char dateStr[12];
    snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", now.day(), now.month(), now.year());

    const char* wifiStatus = getQuickWifiStatus();

    u8g2.clearBuffer();
    u8g2.setDrawColor(1);

    // Header (tanggal dan WiFi status)
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 7, dateStr);
    u8g2.drawStr(128 - u8g2.getStrWidth(wifiStatus), 7, wifiStatus);

    // Waktu utama
    u8g2.setFont(u8g2_font_logisoso24_tr);
    int timeWidth = u8g2.getStrWidth(timeStr);
    u8g2.drawStr((128 - timeWidth) / 2, 45, timeStr);

    // Footer message
    u8g2.setFont(u8g2_font_6x10_tr);
    const char* msg = (adjustState == IDLE) ? "Waiting Data" : "Adjust Time";
    u8g2.drawStr((128 - u8g2.getStrWidth(msg)) / 2, 60, msg);

    u8g2.sendBuffer();
}

void handleTimeAdjustment() {
    DateTime now = rtc.now();
    
    bool currentResetState = digitalRead(RESET_BUTTON) == LOW;
    
    if (currentResetState && !resetButtonHandled) {
        if (resetPressStart == 0) {
            resetPressStart = millis();
        }
        else if (millis() - resetPressStart >= holdThreshold) {
            if (adjustState == IDLE) {
        
                adjHour = now.hour();
                adjMinute = now.minute();
                adjSecond = now.second();
                adjustState = ADJUST_HOUR;
            } else {
        
                switch (adjustState) {
                    case ADJUST_HOUR:
                        adjustState = ADJUST_MINUTE;
                        break;
                    case ADJUST_MINUTE:
                        adjustState = ADJUST_SECOND;
                        break;
                    case ADJUST_SECOND:
                
                        rtc.adjust(DateTime(now.year(), now.month(), now.day(), adjHour, adjMinute, adjSecond));
                        adjustState = IDLE;
                        break;
                }
            }
            resetButtonHandled = true;
            blinkState = true;
            lastBlink = millis();
        }
    }
    else if (!currentResetState) {

        resetPressStart = 0;
        resetButtonHandled = false;
    }

    bool currentInState = digitalRead(IN_BUTTON) == HIGH;
    
    if (millis() - lastInPress > debounceDelay) {

        if (currentInState && !inButtonPressed) {
            inButtonPressed = true;
            inButtonHandled = false;
            lastInPress = millis();
        }

        else if (!currentInState && inButtonPressed) {
            inButtonPressed = false;
            lastInPress = millis();
        }
    }
    
    if (inButtonPressed && !inButtonHandled) {
        switch (adjustState) {
            case ADJUST_HOUR:
                adjHour = (adjHour + 1) % 24;
                break;
            case ADJUST_MINUTE:
                adjMinute = (adjMinute + 1) % 60;
                break;
            case ADJUST_SECOND:
                adjSecond = (adjSecond + 1) % 60;
                break;
            default:
                break;
        }
        inButtonHandled = true;
        lastInPress = millis();
    }
}