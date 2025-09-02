#include "config.h"

void handleReadingRfidState() {
    static String buffer;
    static bool receiving = false;
    static bool rfidReadingStopped = false;
    static unsigned long lastByteTime = 0;
    static unsigned long responseDisplayTime = 0;
    static bool isShowingResponse = false;

    const unsigned long TIMEOUT = 1000;

    if (rfidReadingStopped) {
        return; // Jangan baca lagi jika sudah stop
    }

    while (rs485Serial.available()) {
        char c = rs485Serial.read();

        if ((c < 0x20 && c != 0x02 && c != 0x03) || c > 0x7E) {
            continue;
        }

        lastByteTime = millis();

        if (c == 0x02) {
            receiving = true;
            buffer = "";
            continue;
        }

        if (c == 0x03 && receiving) {
            receiving = false;
            Serial.println("ETX Received - Parsing data");
            parseRS485Data(buffer);

            if (rs485Data.hasData) {
                float weight = lastWeight;
                Serial.print("Berat terbaca: ");
                Serial.println(weight);

                if (isnan(weight) || weight <= 0) {
                    Serial.println("Berat tidak valid!");
                    lastError = ErrorCode::RFID_PARSE_ERROR;
                    currentState = SystemState::ERROR;
                    lastStateChangeTime = millis();
                    return;
                }

                int response = -1;
                String timestamp = getCurrentTime();

                sendingData = true;

                if (rs485Data.slave_id == "1") {
                    weight_in = weight;
                    response = sendDataIn(rs485Data.id_driver, rs485Data.id_customer, weight_in, timestamp);
                } else if (rs485Data.slave_id == "2") {
                    weight_out = weight;
                    response = sendDataOut(rs485Data.id_driver, rs485Data.id_customer, weight_out, timestamp);
                } else {
                    Serial.println("Slave ID tidak dikenal!");
                }

                
                serverResponseDisplay(response);
                sendingData = false;
                delay(2000);
                responseDisplayTime = millis();
                isShowingResponse = true;

                if (sendingData == true) {
                    rfidReadingStopped = true;
                    Serial.println("RFID reading stopped.");
                }
            }

            continue;
        }

        if (receiving) {
            if (buffer.length() < 128) {
                buffer += c;
            } else {
                receiving = false;
                lastError = ErrorCode::RFID_OVERFLOW;
                currentState = SystemState::ERROR;
                lastStateChangeTime = millis();
            }
        }
    }

    yield();
    
    if (receiving && (millis() - lastByteTime > TIMEOUT)) {
        receiving = false;
        lastError = ErrorCode::RFID_TIMEOUT;
        currentState = SystemState::ERROR;
        lastStateChangeTime = millis();
    }
}

void handleErrorState() {
    static bool messageShown = false;
    static unsigned long errorStartTime = 0;
    const uint32_t ERROR_DISPLAY_DURATION = 5000;

    if (!messageShown) {
        u8g2.clearBuffer();
        u8g2.setDrawColor(1);
        u8g2.drawBox(0, 0, 128, 16);
        u8g2.setDrawColor(0);
        u8g2.setFont(u8g2_font_7x14B_tr);
        u8g2.drawStr((128 - u8g2.getStrWidth("ERROR")) / 2, 13, "ERROR");
        u8g2.drawHLine(0, 14, 128);

        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.setDrawColor(1);

        switch(lastError) {
            case ErrorCode::RFID_TIMEOUT:
                u8g2.drawStr(10, 30, "RFID Timeout");
                u8g2.drawStr(10, 45, "Tempelkan kembali");
                break;
            case ErrorCode::RFID_FORMAT:
            case ErrorCode::RFID_PARSE_ERROR:
                u8g2.drawStr(10, 30, "Format RFID Salah");
                u8g2.drawStr(10, 45, "Gunakan kartu valid");
                break;
            case ErrorCode::RFID_OVERFLOW:
                u8g2.drawStr(10, 30, "Data RFID Terlalu");
                u8g2.drawStr(10, 45, "Panjang");
                break;
            case ErrorCode::WIFI_CONNECT:
                u8g2.drawStr(10, 30, "Gagal Konek WiFi");
                u8g2.drawStr(10, 45, "Cek jaringan");
                break;
            case ErrorCode::HTTP_FAIL:
                u8g2.drawStr(10, 30, "Gagal Kirim Data");
                u8g2.drawStr(10, 45, "Cek server");
                break;
            default:
                u8g2.drawStr(10, 30, "Error Sistem");
                u8g2.drawStr(10, 45, "Reset Perangkat");
        }

        u8g2.drawHLine(0, 63, 128);
        u8g2.sendBuffer();
        errorStartTime = millis();
        messageShown = true;
    }

    if (millis() - errorStartTime >= ERROR_DISPLAY_DURATION) {
        resetSystemForError();
        messageShown = false;
    }
    yield();
}

void resetRFIDData(bool resetHardware) {
    rs485Data.slave_id = "";
    rs485Data.id_driver = 0;
    rs485Data.nama_driver = "";
    rs485Data.id_customer = 0;
    rs485Data.no_polisi = "";
    rs485Data.nama_customer = "";
    rs485Data.hasData = false;

    if (resetHardware) {
        rs485Serial.flush();
        digitalWrite(DE_RE_PIN, LOW);
    }
}

void parseRS485Data(const String &data) {
    int idx1 = data.indexOf(',');
    int idx2 = data.indexOf(',', idx1 + 1);
    int idx3 = data.indexOf(',', idx2 + 1);
    int idx4 = data.indexOf(',', idx3 + 1);
    int idx5 = data.indexOf(',', idx4 + 1);

    if (idx1 == -1 || idx2 == -1 || idx3 == -1 || idx4 == -1 || idx5 == -1) {
        lastError = ErrorCode::RFID_FORMAT;
        currentState = SystemState::ERROR;
        return;
    }

    rs485Data.slave_id          = data.substring(0, idx1);
    rs485Data.id_driver         = data.substring(idx1 + 1, idx2).toInt();
    rs485Data.nama_driver       = data.substring(idx2 + 1, idx3);
    rs485Data.id_customer       = data.substring(idx3 + 1, idx4).toInt();
    rs485Data.no_polisi         = data.substring(idx4 + 1, idx5);
    rs485Data.nama_customer     = data.substring(idx5 + 1);
    rs485Data.hasData           = true;

    Serial.println("Data RFID diterima:");
    Serial.println("Slave ID: " + rs485Data.slave_id);
    Serial.println("Driver: " + rs485Data.nama_driver);
    Serial.println("No Pol: " + rs485Data.no_polisi);
}

void resetSystemForError() {
    resetRFIDData(true);

    switch(lastError) {
        case ErrorCode::RFID_TIMEOUT:
        case ErrorCode::RFID_FORMAT:
        case ErrorCode::RFID_PARSE_ERROR:
        case ErrorCode::RFID_OVERFLOW:
            currentState = SystemState::HOME;
            break;
        case ErrorCode::HTTP_FAIL:
            currentState = SystemState::WEIGHT_DISPLAY;
            break;
        default:
            currentState = SystemState::HOME;
    }

    lastStateChangeTime = millis();
    lastError = ErrorCode::NONE;
}