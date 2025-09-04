#include "config.h"
#include <WiFi.h>

const char *ssid = "Biodigester2025";
const char *password = "z3r0w4sTe###";

bool wifiNeedUpdate = false; // Flag untuk update WiFi status
unsigned long lastWifiCheck = 0;
unsigned long lastStatusPrint = 0;
bool wifiConnected = false;
wl_status_t lastWifiStatus = WL_NO_SHIELD;

#define WIFI_CHECK_INTERVAL 1000   // Cek WiFi setiap 1 detik
#define STATUS_PRINT_INTERVAL 5000 // Print status setiap 5 detik

void IRAM_ATTR setWifiNeedUpdate(bool state) {
    wifiNeedUpdate = state;
}

void setupWiFi() {
    WiFi.begin(ssid, password);
    lastWifiCheck = millis();
}

void handleWiFi() {
    unsigned long now = millis();
    
    // Cek WiFi hanya jika perlu atau periodik
    if (wifiNeedUpdate || (now - lastWifiCheck >= WIFI_CHECK_INTERVAL)) {
        wifiNeedUpdate = false;
        lastWifiCheck = now;
        
        wl_status_t currentStatus = WiFi.status();
        
        // Handle status changes
        if (currentStatus != lastWifiStatus) {
            lastWifiStatus = currentStatus;
            
            if (currentStatus == WL_CONNECTED) {
                wifiConnected = true;
                // Hanya print jika status berubah
                // Serial.print("✓ WiFi Connected: ");
                // Serial.println(WiFi.localIP());
            } 
            else if (wifiConnected) {
                wifiConnected = false;
                // Serial.println("✗ WiFi disconnected");
            }
        }
        
        // Print status periodik (non-blocking)
        // if (now - lastStatusPrint >= STATUS_PRINT_INTERVAL) {
        //     lastStatusPrint = now;
            
        //     if (currentStatus == WL_CONNECTED) {
        //         Serial.printf("WiFi OK | RSSI: %ddBm\n", WiFi.RSSI());
        //     }
        // }
        
        // Handle reconnection
        if (!wifiConnected && currentStatus != WL_CONNECTED) {
            if (currentStatus == WL_IDLE_STATUS || currentStatus == WL_DISCONNECTED) {
                WiFi.reconnect();
            }
        }
    }
}

bool isWiFiConnected() {
    return wifiConnected && (WiFi.status() == WL_CONNECTED);
}

// Fungsi cepat untuk mendapatkan status WiFi
const char* getQuickWifiStatus() {
    static unsigned long lastCheck = 0;
    static const char* cachedStatus = "NC";
    
    // Update cache hanya setiap 2 detik
    if (millis() - lastCheck >= 2000) {
        lastCheck = millis();
        cachedStatus = (isWiFiConnected()) ? "OK" : "NC";
        setWifiNeedUpdate(true); // Trigger update di background
    }
    
    return cachedStatus;
}