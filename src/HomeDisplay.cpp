#include "config.h"
#include "utils/timeUtils.h"

// Global variables
TimeAdjustState adjustState = IDLE;
int adjHour, adjMinute, adjSecond;
bool blinkState = true;
unsigned long lastBlink = 0;

// Reset button handling
unsigned long resetPressStart = 0;
bool resetButtonHandled = false;
const unsigned long holdThreshold = 3000; // 3 seconds hold time

// IN button handling
unsigned long lastInPress = 0;
bool inButtonPressed = false;
bool inButtonHandled = false;
const unsigned long debounceDelay = 50; // Debounce time in ms

void HomeDisplay() {
    DateTime now = rtc.now();

    // Use adjusted values when in adjustment mode
    int hour = (adjustState != IDLE) ? adjHour : now.hour();
    int minute = (adjustState != IDLE) ? adjMinute : now.minute();
    int second = (adjustState != IDLE) ? adjSecond : now.second();

    // Blink logic for active editing field
    if (millis() - lastBlink > 500) {
        blinkState = !blinkState;
        lastBlink = millis();
    }

    // Format time string with blinking for active field
    char timeStr[9];
    if (adjustState == ADJUST_HOUR && !blinkState)
        snprintf(timeStr, sizeof(timeStr), "  :%02d:%02d", minute, second);
    else if (adjustState == ADJUST_MINUTE && !blinkState)
        snprintf(timeStr, sizeof(timeStr), "%02d:  :%02d", hour, second);
    else if (adjustState == ADJUST_SECOND && !blinkState)
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:  ", hour, minute);
    else
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", hour, minute, second);

    // Format date
    char dateStr[12];
    snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", now.day(), now.month(), now.year());

    // WiFi status
    const char* wifiStatus = (WiFi.status() == WL_CONNECTED) ? "OK" : "NC";

    // OLED Display
    u8g2.clearBuffer();
    u8g2.setDrawColor(1);

    // Top row (date and WiFi status)
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 7, dateStr);
    u8g2.drawStr(128 - u8g2.getStrWidth(wifiStatus), 7, wifiStatus);

    // Middle (time)
    u8g2.setFont(u8g2_font_logisoso24_tr);
    int timeWidth = u8g2.getStrWidth(timeStr);
    u8g2.drawStr((128 - timeWidth) / 2, 40, timeStr);

    // Bottom (message)
    u8g2.setFont(u8g2_font_6x10_tr);
    const char* msg = (adjustState == IDLE) ? "Tap Your Card" : "Adjust Time";
    u8g2.drawStr((128 - u8g2.getStrWidth(msg)) / 2, 60, msg);

    u8g2.sendBuffer();
}

void handleTimeAdjustment() {
    DateTime now = rtc.now();
    
    // RESET button handling
    bool currentResetState = digitalRead(RESET_BUTTON) == LOW;
    
    if (currentResetState && !resetButtonHandled) {
        if (resetPressStart == 0) {
            resetPressStart = millis();
        }
        else if (millis() - resetPressStart >= holdThreshold) {
            if (adjustState == IDLE) {
                // Enter time adjustment mode
                adjHour = now.hour();
                adjMinute = now.minute();
                adjSecond = now.second();
                adjustState = ADJUST_HOUR;
            } else {
                // Cycle through adjustment states
                switch (adjustState) {
                    case ADJUST_HOUR:
                        adjustState = ADJUST_MINUTE;
                        break;
                    case ADJUST_MINUTE:
                        adjustState = ADJUST_SECOND;
                        break;
                    case ADJUST_SECOND:
                        // Save and exit
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
        // Button released
        resetPressStart = 0;
        resetButtonHandled = false;
    }

    // Improved IN button handling
    bool currentInState = digitalRead(IN_BUTTON) == HIGH;
    
    // Only process button state after debounce period
    if (millis() - lastInPress > debounceDelay) {
        // Detect button press (transition from not pressed to pressed)
        if (currentInState && !inButtonPressed) {
            inButtonPressed = true;
            inButtonHandled = false;
            lastInPress = millis();
        }
        // Detect button release
        else if (!currentInState && inButtonPressed) {
            inButtonPressed = false;
            lastInPress = millis();
        }
    }
    
    // Only increment once per press when button is first pressed
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
        inButtonHandled = true; // Mark as handled to prevent multiple increments
        lastInPress = millis();
    }
}