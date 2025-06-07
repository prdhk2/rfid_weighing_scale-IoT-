#include "config.h"

const char *ssid = "Biodigester2025";
const char *password = "z3r0w4sTe###";
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 15000;

void setupWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    
    Serial.println("Menghubungkan ke WiFi...");
    WiFi.begin(ssid, password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nTerhubung! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nGagal terhubung!");
    }
}

void handleWiFi() {
    static unsigned long lastCheckTime = 0;
    const unsigned long checkInterval = 1000;
    
    if (millis() - lastCheckTime >= checkInterval) {
        lastCheckTime = millis();
        
        if (WiFi.status() != WL_CONNECTED) {
            if (wifiState != DISCONNECTED) {
                Serial.println("WiFi terputus!");
                wifiState = DISCONNECTED;
            }
            
            if (millis() - lastReconnectAttempt >= RECONNECT_INTERVAL) {
                lastReconnectAttempt = millis();
                Serial.println("Mencoba menghubungkan ulang...");
                
                WiFi.disconnect(true);
                delay(1000);
                WiFi.begin(ssid, password);
            }
        } else {
            if (wifiState != CONNECTED) {
                wifiState = CONNECTED;
                Serial.println("WiFi tersambung kembali!");
                Serial.print("IP Address: ");
                Serial.println(WiFi.localIP());
            }
        }
    }
}