#include "config.h"

const char* apiWeightInUrl = "http://***/api/weightDataIn";
const char* apiWeightOutUrl = "http://***/api/weightDataOut";

HttpRequestData currentRequest;
HttpRequestType currentRequestType = NONE;
WiFiClient client;
HTTPClient http;

char jsonBuffer[256];

void initHttpRequest() {
    currentRequestType = NONE;
    currentRequest.inProgress = false;
    currentRequest.retryCount = 0;
    currentRequest.httpResponseCode = -1;
}

int startSendDataIn(int id_driver, int id_customer, float weight_in, String time_in) {
    if (currentRequest.inProgress) {
        Serial.println("Request already in progress");
        return -98;
    }

    if (!isWiFiConnected()) {
        Serial.printf("WiFi not connected | Status: %d\n", WiFi.status());
        return -99;
    }

    snprintf(jsonBuffer, sizeof(jsonBuffer),
        "{\"driver_id\":%d,\"customer_id\":%d,\"weight_in\":%.2f,\"time_in\":\"%s\"}",
        id_driver, id_customer, weight_in, time_in.c_str());

    currentRequest.url = apiWeightInUrl;
    currentRequest.jsonData = jsonBuffer;
    currentRequest.retryCount = 0;
    currentRequest.httpResponseCode = -1;
    currentRequest.inProgress = true;
    currentRequestType = WEIGHT_IN;
    currentRequest.lastAttemptTime = 0;

    Serial.println("Starting non-blocking sendDataIn");
    Serial.println("Payload: " + String(jsonBuffer));

    return 0;
}

int startSendDataOut(int id_driver, int id_customer, float weight_out, String time_out) {
    if (currentRequest.inProgress) {
        Serial.println("Request already in progress");
        return -98;
    }

    if (!isWiFiConnected()) {
        Serial.printf("WiFi not connected | Status: %d\n", WiFi.status());
        return -99;
    }

    snprintf(jsonBuffer, sizeof(jsonBuffer),
        "{\"driver_id\":%d,\"customer_id\":%d,\"weight_out\":%.2f,\"time_out\":\"%s\"}",
        id_driver, id_customer, weight_out, time_out.c_str());

    currentRequest.url = apiWeightOutUrl;
    currentRequest.jsonData = jsonBuffer;
    currentRequest.retryCount = 0;
    currentRequest.httpResponseCode = -1;
    currentRequest.inProgress = true;
    currentRequestType = WEIGHT_OUT;
    currentRequest.lastAttemptTime = 0;

    Serial.println("Starting non-blocking sendDataOut");
    Serial.println("Payload: " + String(jsonBuffer));

    return 0;
}

void processHttpRequests() {
    if (!currentRequest.inProgress) return;

    const unsigned long RETRY_DELAY = 300;
    const int MAX_RETRY = 3;

    if (millis() - currentRequest.lastAttemptTime < RETRY_DELAY && currentRequest.lastAttemptTime != 0) {
        return;
    }

    if (currentRequest.lastAttemptTime == 0) {
        http.begin(client, currentRequest.url);
        http.setTimeout(5000);
        http.addHeader("Content-Type", "application/json");
    }

    // request HTTP
    currentRequest.httpResponseCode = http.POST(currentRequest.jsonData);
    currentRequest.lastAttemptTime = millis();
    currentRequest.retryCount++;

    // Handle response
    if (currentRequest.httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("HTTP Success: %d, Response: %s\n", 
                     currentRequest.httpResponseCode, response.c_str());
        finishHttpRequest();
    } else if (currentRequest.retryCount >= MAX_RETRY) {
        Serial.printf("HTTP Failed after %d retries: %s\n", 
                     MAX_RETRY, http.errorToString(currentRequest.httpResponseCode).c_str());
        finishHttpRequest();
    } else {
        Serial.printf("HTTP Attempt %d failed: %s, retrying...\n",
                     currentRequest.retryCount, 
                     http.errorToString(currentRequest.httpResponseCode).c_str());
    }
}

void finishHttpRequest() {
    http.end();
    currentRequest.inProgress = false;
    
    if (currentRequestType != NONE) {
        httpResponseCode = currentRequest.httpResponseCode;
        lastHttpResponseCode = httpResponseCode; // Simpan untuk displa
        currentRequestType = NONE;
        
        // Trigger response display
        responseState = ResponseState::SHOWING_RESPONSE;
        responseStartTime = millis();
    }
}

bool isHttpRequestInProgress() {
    return currentRequest.inProgress;
}

int getCurrentHttpResponse() {
    return currentRequest.httpResponseCode;
}

int sendDataIn(int id_driver, int id_customer, float weight_in, String time_in) {
    int result = startSendDataIn(id_driver, id_customer, weight_in, time_in);
    if (result != 0) return result;

    unsigned long startTime = millis();
    const unsigned long TIMEOUT = 10000;

    while (isHttpRequestInProgress() && (millis() - startTime < TIMEOUT)) {
        processHttpRequests();
        delay(10);
        yield();
    }

    if (isHttpRequestInProgress()) {
        Serial.println("sendDataIn timeout");
        finishHttpRequest();
        return -97;
    }

    return getCurrentHttpResponse();
}

int sendDataOut(int id_driver, int id_customer, float weight_out, String time_out) {
    int result = startSendDataOut(id_driver, id_customer, weight_out, time_out);
    if (result != 0) return result;

    unsigned long startTime = millis();
    const unsigned long TIMEOUT = 10000;

    while (isHttpRequestInProgress() && (millis() - startTime < TIMEOUT)) {
        processHttpRequests();
        delay(10);
        yield();
    }

    if (isHttpRequestInProgress()) {
        Serial.println("sendDataOut timeout");
        finishHttpRequest();
        return -97;
    }

    return getCurrentHttpResponse();
}