#include "config.h"

WiFiState wifiState = WIFI_DISCONNECTED;
unsigned long lastWifiAttempt = 0;
unsigned long lastWifiCheck = 0;
unsigned long wifiConnectStartTime = 0;
int wifiRetryCount = 0;
const int MAX_RETRIES = 3;

const char* ssid = "#";
const char* password = "###";

void setupWiFi() {
    Serial.println("Initializing WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false); 
    WiFi.persistent(false); 
    
    WiFi.begin(ssid, password);
    
    wifiState = WIFI_CONNECTING;
    lastWifiAttempt = millis();
    lastWifiCheck = millis();
}

void handleWiFi() {
    unsigned long now = millis();
    wl_status_t currentStatus = WiFi.status();

    switch (wifiState) {
        case WIFI_CONNECTING:
            if (currentStatus == WL_CONNECTED) {
                wifiState = WIFI_CONNECTED;
                Serial.print("WiFi connected! IP: ");
                Serial.println(WiFi.localIP());
            } else if (now - wifiConnectStartTime > WIFI_CONNECT_TIMEOUT) {
                // Serial.println("WiFi connection timeout. Retrying...");
                WiFi.disconnect(true);
                wifiState = WIFI_DISCONNECTED;
                lastWifiAttempt = now;
            }
            break;
            
        case WIFI_DISCONNECTED:
        
            if (now - lastWifiAttempt >= WIFI_RECONNECT_INTERVAL) {
                // Serial.println("Attempting WiFi connection...");
                WiFi.begin(ssid, password);
                wifiState = WIFI_CONNECTING;
                wifiConnectStartTime = now;
                lastWifiAttempt = now;
            }
            break;
            
        case WIFI_CONNECTED:
        
            if (currentStatus != WL_CONNECTED) {
                // Serial.println("WiFi disconnected unexpectedly. Reconnecting...");
                wifiState = WIFI_DISCONNECTED;
                lastWifiAttempt = now;
            } else {
                
                static unsigned long lastStatusPrint = 0;
                if (now - lastStatusPrint >= 10000) {
                    lastStatusPrint = now;
                    Serial.printf("WiFi OK | RSSI: %ddBm\n", WiFi.RSSI());
                }
            }
            break;
    }
}

bool isWiFiConnected() {
    return (wifiState == WIFI_CONNECTED) && (WiFi.status() == WL_CONNECTED);
}

const char* getQuickWifiStatus() {
    static unsigned long lastUpdate = 0;
    static char status[3] = "NC";
    
    if (millis() - lastUpdate >= 2000) {
        lastUpdate = millis();
        strcpy(status, isWiFiConnected() ? "OK" : "NC");
    }
    
    return status;
}