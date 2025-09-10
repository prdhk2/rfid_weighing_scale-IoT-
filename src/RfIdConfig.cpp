#include "config.h"

// Struktur untuk mengelola state pembacaan RFID
struct RFIDReaderState {
    char buffer[129]; // Buffer statis (128 karakter + null terminator)
    uint8_t bufferIndex;
    bool receiving;
    unsigned long lastByteTime;
    unsigned long stopUntil;
    
    void reset() {
        bufferIndex = 0;
        receiving = false;
        lastByteTime = 0;
        stopUntil = 0;
        buffer[0] = '\0';
    }
};

static RFIDReaderState rfidState;

void handleReadingRfidState() {
    const unsigned long TIMEOUT = 1000;
    const uint8_t MAX_BUFFER_SIZE = 128;

    // Skip jika dalam masa stop
    if (millis() < rfidState.stopUntil) {
        return;
    }

    // Baca data yang tersedia (tanpa while loop blocking)
    int available = rs485Serial.available();
    if (available <= 0) {
        // Handle timeout hanya jika tidak ada data
        if (rfidState.receiving && (millis() - rfidState.lastByteTime > TIMEOUT)) {
            rfidState.receiving = false;
            lastError = ErrorCode::RFID_TIMEOUT;
            currentState = SystemState::ERROR;
            lastStateChangeTime = millis();
        }
        return;
    }

    // Batasi jumlah data yang diproses dalam satu iterasi
    int toRead = min(available, 16); // Baca maksimal 16 byte per iterasi
    
    for (int i = 0; i < toRead; i++) {
        char c = rs485Serial.read();

        // Filter karakter non-printable (kecuali STX/ETX)
        if ((c < 0x20 && c != 0x02 && c != 0x03) || c > 0x7E) {
            continue;
        }

        rfidState.lastByteTime = millis();

        // Start of Text
        if (c == 0x02) {
            rfidState.receiving = true;
            rfidState.bufferIndex = 0;
            rfidState.buffer[0] = '\0'; // Reset buffer
            continue;
        }

        // End of Text
        if (c == 0x03 && rfidState.receiving) {
            rfidState.receiving = false;
            
            // Null terminate the buffer
            if (rfidState.bufferIndex < MAX_BUFFER_SIZE) {
                rfidState.buffer[rfidState.bufferIndex] = '\0';
            } else {
                rfidState.buffer[MAX_BUFFER_SIZE] = '\0';
            }
            
            // Parse data
            parseRS485Data(String(rfidState.buffer));

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
                    rfidState.stopUntil = millis() + 1500; // Stop membaca 1.5 detik
                } else {
                    startResponseDisplay(result);
                    currentState = SystemState::SENDING_DATA;
                    rfidState.stopUntil = millis() + 1000; // Stop membaca 1 detik untuk error
                }
            }
            continue;
        }

        // Data receiving
        if (rfidState.receiving) {
            if (rfidState.bufferIndex < MAX_BUFFER_SIZE) {
                rfidState.buffer[rfidState.bufferIndex++] = c;
            } else {
                // Buffer overflow
                rfidState.receiving = false;
                lastError = ErrorCode::RFID_OVERFLOW;
                currentState = SystemState::ERROR;
                lastStateChangeTime = millis();
            }
        }
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
    
    // Reset state reader
    rfidState.reset();
}

// Optimasi parsing dengan menghindari copy string
void parseRS485Data(const String &data) {
    // Reset data pertama
    rs485Data.hasData = false;
    
    // Parsing langsung dari String tanpa copy
    int startIndex = 0;
    int commaIndex = data.indexOf(',');
    int tokenCount = 0;
    String tokens[6];
    
    while (commaIndex >= 0 && tokenCount < 6) {
        tokens[tokenCount++] = data.substring(startIndex, commaIndex);
        startIndex = commaIndex + 1;
        commaIndex = data.indexOf(',', startIndex);
    }
    
    // Ambil token terakhir
    if (tokenCount < 6 && startIndex < data.length()) {
        tokens[tokenCount++] = data.substring(startIndex);
    }
    
    // Validasi jumlah token
    if (tokenCount != 6) {
        lastError = ErrorCode::RFID_FORMAT;
        currentState = SystemState::ERROR;
        return;
    }
    
    // Assign data dengan validasi
    rs485Data.slave_id = tokens[0];
    rs485Data.id_driver = tokens[1].toInt();
    rs485Data.nama_driver = tokens[2];
    rs485Data.id_customer = tokens[3].toInt();
    rs485Data.no_polisi = tokens[4];
    rs485Data.nama_customer = tokens[5];
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
    // Bersihkan buffer RS485 jika penuh (non-blocking)
    if (rs485Serial.available() > 64) {
        int toClear = min(rs485Serial.available(), 32); // Bersihkan maksimal 32 byte per iterasi
        for (int i = 0; i < toClear; i++) {
            rs485Serial.read();
        }
    }
    
    // Handle state reading RFID
    if (currentState == SystemState::READING_RFID) {
        handleReadingRfidState();
    }
}