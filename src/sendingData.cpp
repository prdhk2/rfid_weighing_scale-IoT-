#include "config.h"

const char* apiWeightInUrl = "http://103.82.241.190/api/weightDataIn";
const char* apiWeightOutUrl = "http://103.82.241.190/api/weightDataOut";

int sendDataIn(int id_driver, int id_customer, float weight_in, String time_in) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("WiFi status: %d, RSSI: %d\n", WiFi.status(), WiFi.RSSI());
        return -99;
    } else {
        Serial.printf("WiFi OK, status: %d, RSSI: %d, Gateway: %d\n", WiFi.status(), WiFi.RSSI(), WiFi.gatewayIP());
    }
    
    WiFiClient client;
    HTTPClient http;
    http.begin(client, apiWeightInUrl);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{";
    jsonData += "\"driver_id\":" + String(id_driver) + ",";
    jsonData += "\"customer_id\":" + String(id_customer) + ",";
    jsonData += "\"weight_in\":" + String(weight_in, 2) + ",";
    jsonData += "\"time_in\":\"" + time_in + "\"";
    jsonData += "}";

    Serial.println("Payload yang dikirim:");
    Serial.println(jsonData);

    int httpResponseCode = -1;
    int retry = 0;
    const int maxRetry = 3;

    while (retry < maxRetry && httpResponseCode <= 0) {
        httpResponseCode = http.POST(jsonData);
        retry++;
        delay(300); // jeda antar percobaan
    }

    String response = "";
    if (httpResponseCode > 0) {
        response = http.getString();
        Serial.println("Server Response:");
        Serial.println(response);
    } else {
        Serial.printf("HTTP POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();

    serverResponseDisplay(httpResponseCode);
    return httpResponseCode;
}

int sendDataOut(float weight_out, String time_out) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("WiFi status: %d, RSSI: %d\n", WiFi.status(), WiFi.RSSI());
        return -99;
    } else {
        Serial.printf("WiFi OK, status: %d, RSSI: %d\n", WiFi.status(), WiFi.RSSI());
    }
    
    WiFiClient client;
    HTTPClient http;
    http.begin(client, apiWeightOutUrl);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{";
    jsonData += "\"weight_out\":" + String(weight_out, 2) + ",";
    jsonData += "\"time_out\":\"" + time_out + "\"";
    jsonData += "}";

    Serial.println("Payload yang dikirim (OUT):");
    Serial.println(jsonData);

    int httpResponseCode = -1;
    int retry = 0;
    const int maxRetry = 3;

    while (retry < maxRetry && httpResponseCode <= 0) {
        httpResponseCode = http.POST(jsonData);
        retry++;
        delay(300); // jeda antar percobaan
    }

    String response = "";
    if (httpResponseCode > 0) {
        response = http.getString();
        Serial.println("Server Response:");
        Serial.println(response);
    } else {
        Serial.printf("HTTP POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    serverResponseDisplay(httpResponseCode);
    http.end();

    return httpResponseCode;
}