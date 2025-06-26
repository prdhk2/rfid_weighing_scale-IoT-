#include "config.h"

const char *ssid = "Biodigester2025";
const char *password = "z3r0w4sTe###";

unsigned long wifiCheckMillis = 0;
bool wifiConnecting = false;
bool wifiConnected = false;

#define WIFI_RETRY_INTERVAL 10000

void setupWiFi() {
    Serial.println("Starting WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.begin(ssid, password, 6);
    wifiConnecting = true;
}

void handleWiFi() {
    unsigned long now = millis();

    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiConnected) {
        wifiConnected = true;
        wifiConnecting = false;
        Serial.print("WiFi Connected: ");
        Serial.println(WiFi.localIP());
        }
        return;
    }

    if (!wifiConnected && !wifiConnecting && (now - wifiCheckMillis > WIFI_RETRY_INTERVAL)) {
        Serial.println("Not connected. Trying WiFi.begin()...");
        WiFi.setTxPower(WIFI_POWER_19_5dBm);
        WiFi.begin(ssid, password, 6);
        wifiConnecting = true;
        wifiCheckMillis = now;
        return;
    }

    if (wifiConnecting && WiFi.status() != WL_CONNECTED && (now - wifiCheckMillis > WIFI_RETRY_INTERVAL)) {
        Serial.println("WiFi still not connected...");
        wifiConnecting = false;
    }
}