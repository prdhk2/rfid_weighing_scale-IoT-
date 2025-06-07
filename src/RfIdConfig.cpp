#include "config.h"

void handleErrorState() {
    static bool messageShown = false;
    static unsigned long errorStartTime = 0;
    const uint32_t ERROR_DISPLAY_DURATION = 2000;

    auto centerText = [](const char* text) {
        return (128 - u8g2.getStrWidth(text)) / 2;
    };

    if (!messageShown) {
        u8g2.clearBuffer();
        
        u8g2.setDrawColor(1);
        u8g2.drawBox(0, 0, 128, 16);
        u8g2.setDrawColor(0);
        u8g2.setFont(u8g2_font_7x14B_tr);
        u8g2.drawStr(centerText("ERROR"), 13, "ERROR");
        
        u8g2.drawHLine(0, 14, 128);
        
        // 4. Pesan error
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.setDrawColor(1);
        u8g2.drawStr(centerText("Gagal Baca RFID"), 35, "Gagal Baca RFID");
        u8g2.drawStr(centerText("Tempelkan Ulang"), 50, "Tempelkan Ulang");
        
        u8g2.drawHLine(0, 63, 128);
        
        u8g2.sendBuffer();
        errorStartTime = millis();
        messageShown = true;

    }

    if (millis() - errorStartTime >= ERROR_DISPLAY_DURATION) {
        resetRFIDData();
        currentState = STATE_HOME;
        lastStateChangeTime = millis();
        messageShown = false;
    }
    yield();
}

bool readRFIDData() {
    char temp_id_driver[17] = "";
    char temp_nama_driver[17] = "";
    char temp_id_customer[17] = "";
    char temp_no_polisi[17] = "";
    char temp_nama_customer[17] = "";

    for (byte i = 0; i < total_blocks; i++) {
        byte buffer[18] = {0};
        byte block = blocks[i];
        byte len = 18;

        MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
        if (status != MFRC522::STATUS_OK) continue;

        status = mfrc522.MIFARE_Read(block, buffer, &len);
        if (status != MFRC522::STATUS_OK) continue;

        buffer[16] = 0;  // Null terminator
        switch (block) {
            case 4: strncpy(temp_id_driver, (char*)buffer, 16); break;
            case 5: strncpy(temp_nama_driver, (char*)buffer, 16); break;
            case 6: strncpy(temp_id_customer, (char*)buffer, 16); break;
            case 8: strncpy(temp_no_polisi, (char*)buffer, 16); break;
            case 9: strncpy(temp_nama_customer, (char*)buffer, 16); break;
        }
    }

    if (strlen(temp_id_driver) == 0 || strlen(temp_nama_driver) == 0 ||
        strlen(temp_id_customer) == 0 || strlen(temp_no_polisi) == 0 ||
        strlen(temp_nama_customer) == 0) {
        Serial.println("Data RFID tidak lengkap!");
        return false;
    }

    id_driver = atoi(temp_id_driver);
    nama_driver = String(temp_nama_driver);     nama_driver.trim();
    id_customer = atoi(temp_id_customer);
    no_polisi = String(temp_no_polisi);         no_polisi.trim();
    nama_customer = String(temp_nama_customer); nama_customer.trim();


    yield();
    return true;

}

void resetRFIDData(bool resetHardware) {
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    id_driver = 0;
    nama_driver = "";
    id_customer = 0;
    no_polisi = "";
    nama_customer = "";

    cardPosX = 128;
    animFrame = 0;
    lastAnimUpdate = 0;

    if (resetHardware) {
        mfrc522.PCD_Init();
        for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
        Serial.println("RFID hardware direset");
    }
}

void checkRFIDPresence() {
    static unsigned long lastRfidCheck = 0;
    const unsigned long rfidCheckInterval = 200; // ms

    if (millis() - lastRfidCheck > rfidCheckInterval) {
        lastRfidCheck = millis();

        if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
            if (currentState != STATE_READING_RFID) {
                previousState = currentState;
                currentState = STATE_READING_RFID;
                lastStateChangeTime = millis();
            }
        }
    }
}

void handleReadingRFIDState() {
    static bool readingStarted = false;

    if (!readingStarted) {
        readingStarted = true;

        Serial.println("Membaca data RFID...");
        if (readRFIDData()) {
            Serial.println("RFID berhasil dibaca");
            currentState = STATE_WEIGHT_DISPLAY;
        } else {
            Serial.println("Data RFID tidak valid");
            currentState = STATE_ERROR;
        }

        readingStarted = false;
    }

    yield();
}
