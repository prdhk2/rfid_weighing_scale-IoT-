#include "config.h"

void handleWeightDisplayState() {
    if (!rs485Data.hasData) {
        lastError = ErrorCode::RFID_NO_DATA;
        currentState = SystemState::ERROR;
        lastStateChangeTime = millis();
        return;
    }
    displayWeight();
    
    if(millis() - lastStateChangeTime > 120000) {
        currentState = HOME;
        resetRFIDData(true);
    }
}

void displayRFIDData(float weight) {
    u8g2.clearBuffer();

    weight_in = weight;
    weight_out = weight;
    
    // Header
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 0, 128, 9);
    u8g2.setDrawColor(0);
    u8g2.drawStr(21, 7, "Display Timbangan");
    
    static const char strDriver[] PROGMEM = "Driver";
    static const char strNoPol[] PROGMEM = "No Pol";
    static const char strCust[] PROGMEM = "Cust";
    static const char strBerat[] PROGMEM = "Berat";
    static const char strKg[] PROGMEM = "/Kg";
    static const char strMasuk[] PROGMEM = "Masuk";
    static const char strKeluar[] PROGMEM = "Keluar";
    
    // Driver Info
    u8g2.setDrawColor(1);
    u8g2.drawStr(2, 19, strDriver);
    u8g2.drawStr(40, 19, ":");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(50, 19); 
    u8g2.print(rs485Data.nama_driver.substring(0, 15)); // Limit length
    
    // License Plate
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, 29, strNoPol);
    u8g2.drawStr(40, 29, ":");   
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(50, 29); 
    u8g2.print(rs485Data.no_polisi.substring(0, 10));
    
    // Customer
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, 39, strCust);
    u8g2.drawStr(40, 39, ":");    
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(50, 39); 
    u8g2.print(rs485Data.nama_customer.substring(0, 15));
    
    // Weight
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, 49, strBerat);
    u8g2.drawStr(40, 49, ":");
    u8g2.setFont(u8g2_font_ncenB10_tr);
    
    char weightStr[8];
    if (isnan(weight)) {
        u8g2.drawStr(50, 60, "N/A");
    } else {
        dtostrf(weight, 4, 2, weightStr);
        u8g2.drawStr(50, 53, weightStr);
    }
    
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(105, 49, strKg);

    // Buttons
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 56, 38, 8);
    u8g2.setDrawColor(0);
    u8g2.drawStr(6, 63, strMasuk);

    u8g2.setDrawColor(1);
    u8g2.drawBox(88, 56, 40, 8);
    u8g2.setDrawColor(0);
    u8g2.drawStr(94, 63, strKeluar);

    u8g2.sendBuffer();
}

void displayWeight() {
    static unsigned long lastUpdate = 0;
    constexpr unsigned long updateInterval = 500;

    float weight = readWeight();  

    if (weight != lastWeight || millis() - lastUpdate > updateInterval) {
        lastUpdate = millis();
        lastWeight = weight;
        displayRFIDData(weight);
    }

    if (checkSendInButton() || checkSendOutButton()) {
        currentState = SENDING_DATA;
        return;
    }

    if (checkResetButton()) {
        currentState = HOME;
        resetRFIDData(true);
        return;
    }
}

namespace {
    struct ButtonState {
        uint32_t lastDebounceTime;
        bool lastButtonState;
    };

    ButtonState resetButton = {0, HIGH};
    ButtonState inButton = {0, HIGH};
    ButtonState outButton = {0, HIGH};
}

bool checkResetButton() {
    bool currentRead = digitalRead(RESET_BUTTON);

    if (currentRead != resetButton.lastButtonState) {
        resetButton.lastDebounceTime = millis();
    }

    if ((millis() - resetButton.lastDebounceTime) > 50) {
        if (currentRead == LOW) {
            Serial.println(F("Reset button pressed"));
            resetRFIDData(true);
            currentState = HOME;
            lastStateChangeTime = millis();
            u8g2.clearBuffer();
            HomeDisplay();
            resetButton.lastButtonState = currentRead; // Perbaikan
            return true;
        }
    }

    resetButton.lastButtonState = currentRead; // Perbaikan
    return false;
}

bool checkSendInButton() {
    static unsigned long lastDebounceTime = 0;
    static bool lastStableState = HIGH;
    static bool lastPhysicalRead = HIGH;

    bool currentRead = digitalRead(IN_BUTTON);

    // Jika ada perubahan pembacaan tombol
    if (currentRead != lastPhysicalRead) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > 50) { // debounce 50ms
        if (currentRead == LOW && lastStableState == HIGH && httpState == HTTP_IDLE && !sendingData) {
            // Tombol baru saja ditekan
            Serial.println("Tombol 'Masuk' ditekan, mengirim data IN...");
            sendingData = true;

            String time_in = getCurrentTime();
            int response = sendDataIn(rs485Data.id_driver, rs485Data.id_customer, weight_in, time_in);
            Serial.printf("Response Code: %d\n", response);

            sendingData = false;

            currentState = SENDING_DATA;
            lastStableState = currentRead;
            lastPhysicalRead = currentRead;
            return true;
        }
        lastStableState = currentRead;
    }

    lastPhysicalRead = currentRead;
    return false;
}

bool checkSendOutButton() {
    static unsigned long lastDebounceTime = 0;
    static bool lastStableState = HIGH;
    static bool lastPhysicalRead = HIGH;

    bool currentRead = digitalRead(OUT_BUTTON);

    if (currentRead != lastPhysicalRead) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > 50) {
        if (currentRead == LOW && lastStableState == HIGH && httpState == HTTP_IDLE && !sendingData) {
            Serial.println("Tombol 'Keluar' ditekan, mengirim data OUT...");
            sendingData = true;

            String time_out = getCurrentTime();
            int response = sendDataOut(rs485Data.id_driver, rs485Data.id_customer, weight_out, time_out);
            Serial.printf("Response Code: %d\n", response);

            sendingData = false;

            currentState = SENDING_DATA;
            lastStableState = currentRead;
            lastPhysicalRead = currentRead;
            return true;
        }
        lastStableState = currentRead;
    }

    lastPhysicalRead = currentRead;
    return false;
}