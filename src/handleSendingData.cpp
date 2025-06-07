#include "config.h"

void handleSendingDataState() {
    static unsigned long responseDisplayStartTime = 0;
    
    if (responseDisplayStartTime == 0) {
        responseDisplayStartTime = millis();
    }
    
    // Kembali ke weight display setelah 5 detik
    if (millis() - responseDisplayStartTime > 3000) {
        currentState = STATE_WEIGHT_DISPLAY;
        responseDisplayStartTime = 0; // Reset timer
    }
}