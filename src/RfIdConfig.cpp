#include "config.h"

void handleReadingRfidState() {
    static char buffer[129]; // Buffer statis (128 karakter + null terminator)
    static uint8_t bufferIndex = 0;
    static bool receiving = false;
    static unsigned long lastByteTime = 0;
    static unsigned long stopUntil = 0;

    const unsigned long TIMEOUT = 1000;
    const uint8_t MAX_BUFFER_SIZE = 128;

    // Skip jika dalam masa stop
    if (millis() < stopUntil) {
        return;
    }

    // Baca semua data yang tersedia sekaligus
    while (rs485Serial.available() > 0) {
        char c = rs485Serial.read();

        // Filter karakter non-printable (kecuali STX/ETX)
        if ((c < 0x20 && c != 0x02 && c != 0x03) || c > 0x7E) {
            continue;
        }

        lastByteTime = millis();

        // Start of Text
        if (c == 0x02) {
            receiving = true;
            bufferIndex = 0;
            buffer[0] = '\0'; // Reset buffer
            continue;
        }

        // End of Text
        if (c == 0x03 && receiving) {
            receiving = false;
            
            // Null terminate the buffer
            if (bufferIndex < MAX_BUFFER_SIZE) {
                buffer[bufferIndex] = '\0';
            } else {
                buffer[MAX_BUFFER_SIZE] = '\0';
            }
            
            // Parse data
            parseRS485Data(String(buffer));

            if (rs485Data.hasData) {
                // Validasi weight dengan toleransi
                if (isnan(lastWeight) || lastWeight <= 0.1) { // Tolerance 0.1 kg
                    lastError = ErrorCode::RFID_PARSE_ERROR;
                    currentState = SystemState::ERROR;
                    lastStateChangeTime = millis();
                    return;
                }

                String timestamp = getCurrentTime();
                int result = -1;

                // Process berdasarkan slave ID
                if (rs485Data.slave_id == "1") {
                    weight_in = lastWeight;
                    result = startSendDataIn(rs485Data.id_driver, rs485Data.id_customer, weight_in, timestamp);
                } else if (rs485Data.slave_id == "2") {
                    weight_out = lastWeight;
                    result = startSendDataOut(rs485Data.id_driver, rs485Data.id_customer, weight_out, timestamp);
                }

                // Handle response
                if (result == 0) {
                    sendingData = true;
                    currentState = SystemState::SENDING_DATA;
                    stopUntil = millis() + 1500; // Stop membaca 1.5 detik
                } else {
                    startResponseDisplay(result);
                    currentState = SystemState::SENDING_DATA;
                    stopUntil = millis() + 1000; // Stop membaca 1 detik untuk error
                }
            }
            continue;
        }

        // Data receiving
        if (receiving) {
            if (bufferIndex < MAX_BUFFER_SIZE) {
                buffer[bufferIndex++] = c;
            } else {
                // Buffer overflow
                receiving = false;
                lastError = ErrorCode::RFID_OVERFLOW;
                currentState = SystemState::ERROR;
                lastStateChangeTime = millis();
            }
        }
    }

    // Handle timeout
    if (receiving && (millis() - lastByteTime > TIMEOUT)) {
        receiving = false;
        lastError = ErrorCode::RFID_TIMEOUT;
        currentState = SystemState::ERROR;
        lastStateChangeTime = millis();
    }

    yield();
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
    // Reset data pertama
    rs485Data.hasData = false;
    
    // Parsing cepat dengan pointer
    const char* str = data.c_str();
    char* tokens[6]; // slave_id, id_driver, nama_driver, id_customer, no_polisi, nama_customer
    uint8_t tokenCount = 0;
    
    // Temporary buffer untuk parsing
    char tempBuffer[129];
    strncpy(tempBuffer, str, sizeof(tempBuffer) - 1);
    tempBuffer[sizeof(tempBuffer) - 1] = '\0';
    
    // Tokenize
    char* token = strtok(tempBuffer, ",");
    while (token != NULL && tokenCount < 6) {
        tokens[tokenCount++] = token;
        token = strtok(NULL, ",");
    }
    
    // Validasi jumlah token
    if (tokenCount != 6) {
        lastError = ErrorCode::RFID_FORMAT;
        currentState = SystemState::ERROR;
        return;
    }
    
    // Assign data dengan validasi
    rs485Data.slave_id = String(tokens[0]);
    rs485Data.id_driver = atoi(tokens[1]);
    rs485Data.nama_driver = String(tokens[2]);
    rs485Data.id_customer = atoi(tokens[3]);
    rs485Data.no_polisi = String(tokens[4]);
    rs485Data.nama_customer = String(tokens[5]);
    rs485Data.hasData = true;
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

void handleRS485() {
    // Bersihkan buffer RS485 jika penuh
    if (rs485Serial.available() > 64) {
        while (rs485Serial.available() > 0) {
            rs485Serial.read();
        }
    }
    
    // Handle state reading RFID
    if (currentState == SystemState::READING_RFID) {
        handleReadingRfidState();
    }
}