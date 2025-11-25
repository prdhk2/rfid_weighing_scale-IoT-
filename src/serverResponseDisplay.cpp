#include "config.h"

// Variabel untuk response display
ResponseState responseState = ResponseState::NONE;
unsigned long responseStartTime = 0;
int16_t lastHttpResponseCode = 0;

// Fungsi untuk mengambil teks dari PROGMEM
void drawCenteredPStr(u8g2_uint_t y, const char* pstr) {
    char buffer[32];
    strncpy_P(buffer, pstr, sizeof(buffer));
    buffer[sizeof(buffer)-1] = '\0';

    u8g2_uint_t x = (128 - u8g2.getStrWidth(buffer)) / 2;
    u8g2.setCursor(x, y);
    u8g2.print(buffer);
}

void handleResponseDisplay() {
    if (responseState != ResponseState::SHOWING_RESPONSE) {
        return;
    }

    const unsigned long DISPLAY_DURATION = 3000; // 3 detik

    if (responseStartTime > 0 && millis() - responseStartTime == 0) {
        showResponseScreen(lastHttpResponseCode);
    }

    if (millis() - responseStartTime >= DISPLAY_DURATION) {
        responseState = ResponseState::COMPLETED;
        responseStartTime = 0;
        
        // Kembali ke weight display
        currentState = SystemState::WEIGHT_DISPLAY;
        return;
    }

    static unsigned long lastRefresh = 0;
    if (millis() - lastRefresh >= 500) {
        lastRefresh = millis();
        showResponseScreen(lastHttpResponseCode);
    }
}

void showResponseScreen(int16_t responseCode) {
    static const char successMsg[] PROGMEM = "Data Terkirim!";
    static const char failMsg[] PROGMEM = "Gagal!";
    static const char codePrefix[] PROGMEM = "Kode: ";

    u8g2.clearBuffer();
    // box response
    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 20, 128, 30);
    u8g2.setDrawColor(1);
    u8g2.drawFrame(2, 18, 124, 34);

    u8g2.setFont(u8g2_font_helvB12_tr);

    if (responseCode == HTTP_CODE_OK) {
        drawCenteredPStr(40, successMsg);
    } else {
        drawCenteredPStr(30, failMsg);

        char codeBuffer[32];
        char prefixBuffer[16];
        strncpy_P(prefixBuffer, codePrefix, sizeof(prefixBuffer));
        prefixBuffer[sizeof(prefixBuffer)-1] = '\0';

        snprintf(codeBuffer, sizeof(codeBuffer), "%s%d", prefixBuffer, responseCode);
        
        u8g2.setCursor((128 - u8g2.getStrWidth(codeBuffer)) / 2, 45);
        u8g2.print(codeBuffer);
    }

    u8g2.sendBuffer();
    yield();
}

// Fungsi untuk memulai response display
void startResponseDisplay(int16_t responseCode) {
    lastHttpResponseCode = responseCode;
    responseState = ResponseState::SHOWING_RESPONSE;
    responseStartTime = millis();
    showResponseScreen(responseCode); // Tampilkan segera
}

// Handle sending data state (non-blocking)
void handleSendingDataState() {
    // Jika HTTP request masih berjalan, tunggu
    if (isHttpRequestInProgress()) {
        processHttpRequests(); // Process HTTP requests
        return;
    }

    // Jika HTTP selesai dan belum menampilkan response
    if (responseState == ResponseState::NONE && !isHttpRequestInProgress()) {
        startResponseDisplay(httpResponseCode);
        return;
    }

    // Jika sedang menampilkan response
    if (responseState == ResponseState::SHOWING_RESPONSE) {
        handleResponseDisplay();
        return;
    }

    if (responseState == ResponseState::COMPLETED) {
        currentState = SystemState::WEIGHT_DISPLAY;
        responseState = ResponseState::NONE;
    }
}