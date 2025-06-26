#include "config.h"

void handleReadingRfidState() {
    static String buffer;
    static bool receiving = false;
    static unsigned long lastByteTime = 0;
    const unsigned long TIMEOUT = 1000;

    // Handle incoming data
    while (rs485Serial.available()) {
        char c = rs485Serial.read();
        lastByteTime = millis();
        
        if (c == 0x02) { // STX
            receiving = true;
            buffer.reserve(128); // Alokasikan memory terlebih dahulu
            buffer = "";
            Serial.println("STX Received");
            continue;
        }
        
        if (c == 0x03) { // ETX
            if (receiving) {
                Serial.println("ETX Received - Parsing data");
                parseRS485Data(buffer);
                receiving = false;
                
                // Tambahkan pengecekan sebelum ganti state
                if (rs485Data.hasData) {
                    currentState = SystemState::WEIGHT_DISPLAY;
                    lastStateChangeTime = millis();
                } else {
                    lastError = ErrorCode::RFID_PARSE_ERROR;
                    currentState = SystemState::ERROR;
                    lastStateChangeTime = millis();
                }
            }
            continue;
        }
        
        if (receiving) {
            // Batasi panjang buffer untuk menghindari overflow
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

    // Timeout handling
    if (receiving && (millis() - lastByteTime > TIMEOUT)) {
        Serial.println("RFID Timeout Occurred");
        receiving = false;
        buffer = "";
        lastError = ErrorCode::RFID_TIMEOUT;
        currentState = SystemState::ERROR;
        lastStateChangeTime = millis();
    }
}

void handleErrorState() {
    static bool messageShown = false;
    static unsigned long errorStartTime = 0;
    const uint32_t ERROR_DISPLAY_DURATION = 5000; // Perpanjang durasi
    
    if (!messageShown) {
        u8g2.clearBuffer();
        u8g2.setDrawColor(1);
        u8g2.drawBox(0, 0, 128, 16);
        u8g2.setDrawColor(0);
        u8g2.setFont(u8g2_font_7x14B_tr);
        
        // Center error header
        u8g2.drawStr((128 - u8g2.getStrWidth("ERROR")) / 2, 13, "ERROR");
        u8g2.drawHLine(0, 14, 128);
        
        // Tampilkan pesan error sesuai kode
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

    // Handle timeout dan reset
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
    // Contoh format: "1,1023,Budi,301,AB1234CD,IndahLog"
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

    int slave_id = data.substring(0, idx1).toInt();
    int id_driver = data.substring(idx1 + 1, idx2).toInt();
    String nama_driver = data.substring(idx2 + 1, idx3);
    int id_customer = data.substring(idx3 + 1, idx4).toInt();
    String no_polisi = data.substring(idx4 + 1, idx5);
    String nama_customer = data.substring(idx5 + 1);

    rs485Data.slave_id = slave_id;
    rs485Data.id_driver = id_driver;
    rs485Data.nama_driver = nama_driver;
    rs485Data.id_customer = id_customer;
    rs485Data.no_polisi = no_polisi;
    rs485Data.nama_customer = nama_customer;
    rs485Data.hasData = true;

    Serial.println("Data RFID diterima:");
    Serial.println("Slave ID: " + String(rs485Data.slave_id));
    Serial.println("Driver: " + rs485Data.nama_driver);
    Serial.println("No Pol: " + rs485Data.no_polisi);
}

void resetSystemForError() {
    // Reset data RFID
    resetRFIDData(true);
    
    // Reset state berdasarkan jenis error
    switch(lastError) {
        case ErrorCode::RFID_TIMEOUT:
        case ErrorCode::RFID_FORMAT:
        case ErrorCode::RFID_PARSE_ERROR:
        case ErrorCode::RFID_OVERFLOW:
            currentState = SystemState::HOME;
            break;
            
        case ErrorCode::HTTP_FAIL:
            currentState = SystemState::WEIGHT_DISPLAY; // Coba kirim lagi
            break;
            
        default:
            currentState = SystemState::HOME;
    }
    
    lastStateChangeTime = millis();
    lastError = ErrorCode::NONE;
}