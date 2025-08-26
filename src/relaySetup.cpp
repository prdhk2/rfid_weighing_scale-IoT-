#include "config.h"

static RelayState relayState = RelayState::IDLE;
static unsigned long relayStartTime = 0;
static int bipCount = 0;
static int currentResponseCode = -1;

void setupRelay() {
    pinMode(RELAY_CONTROL, OUTPUT);
    digitalWrite(RELAY_CONTROL, LOW);
}

void handleRelay(int responseCode) {
    if (relayState != RelayState::IDLE) {
        Serial.println("Relay masih busy, tidak bisa memproses request baru");
        return;
    }
    
    currentResponseCode = responseCode;
    relayStartTime = millis();
    bipCount = 0;
    
    if (responseCode == 200) {
        // Success - nyalakan relay 1 detik
        Serial.println("Alarm: SUCCESS - 1 detik");
        relayState = RelayState::SUCCESS_ON;
        digitalWrite(RELAY_CONTROL, HIGH);
    } else {
        // Gagal
        Serial.println("Alarm: FAILED - 3x bip 500ms");
        relayState = RelayState::FAILURE_ON;
        digitalWrite(RELAY_CONTROL, HIGH);
        bipCount = 1;
    }
}

void updateRelay() {
    unsigned long currentTime = millis();
    unsigned long elapsedTime = currentTime - relayStartTime;
    
    switch (relayState) {
        case RelayState::IDLE:
            // Tidak ada yang perlu dilakukan
            break;
            
        case RelayState::SUCCESS_ON:
            if (elapsedTime >= 1000) {
                digitalWrite(RELAY_CONTROL, LOW);
                relayState = RelayState::COMPLETED;
                Serial.println("Alarm success completed");
            }
            break;
            
        case RelayState::FAILURE_ON:
            if (elapsedTime >= 500) {
                digitalWrite(RELAY_CONTROL, LOW);
                relayState = RelayState::FAILURE_OFF;
                relayStartTime = currentTime;
            }
            break;
            
        case RelayState::FAILURE_OFF:
            if (elapsedTime >= 500) {
                if (bipCount < 3) {
                    bipCount++;
                    digitalWrite(RELAY_CONTROL, HIGH);
                    relayState = RelayState::FAILURE_ON;
                    relayStartTime = currentTime;
                    Serial.print("Bip ke-");
                    Serial.println(bipCount);
                } else {
                    relayState = RelayState::COMPLETED;
                    Serial.println("Alarm failure completed");
                }
            }
            break;
            
        case RelayState::COMPLETED:
            // Reset ke idle setelah completed
            relayState = RelayState::IDLE;
            currentResponseCode = -1;
            break;
    }
}

bool isRelayBusy() {
    return relayState != RelayState::IDLE;
}