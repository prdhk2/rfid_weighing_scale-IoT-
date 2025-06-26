#include "config.h"

// Fungsi untuk mengambil teks dari PROGMEM dan menggambarnya di tengah layar
void drawCenteredPStr(u8g2_uint_t y, const char* pstr) {
    char buffer[32]; // Ukuran buffer yang cukup
    strncpy_P(buffer, pstr, sizeof(buffer));
    buffer[sizeof(buffer)-1] = '\0';

    u8g2_uint_t x = (128 - u8g2.getStrWidth(buffer)) / 2;
    u8g2.setCursor(x, y);
    u8g2.print(buffer);
}

void serverResponseDisplay(int16_t responseCode) {
    // Teks yang disimpan di PROGMEM
    static const char successMsg[] PROGMEM = "Data Terkirim!";
    static const char failMsg[] PROGMEM = "Gagal!";
    static const char codePrefix[] PROGMEM = "Kode: ";

    // Bersihkan layar
    u8g2.clearBuffer();

    // Gambar kotak respon
    u8g2.setDrawColor(0); // warna background kotak (hitam)
    u8g2.drawBox(0, 20, 128, 30); // posisi dan ukuran kotak
    u8g2.setDrawColor(1); // warna teks dan border (putih)
    u8g2.drawFrame(2, 18, 124, 34); // frame tepi kotak

    // Atur font
    u8g2.setFont(u8g2_font_helvB12_tr);

    if (responseCode == HTTP_CODE_OK) {
        // Jika sukses, tampilkan pesan "Data Terkirim!" di tengah
        drawCenteredPStr(40, successMsg);
    } else {
        // Jika gagal, tampilkan pesan "Gagal!" dan kode error

        // Gambar tulisan "Gagal!" di tengah atas kotak
        drawCenteredPStr(30, failMsg);

        // Gabungkan teks "Kode: " dengan angka kode error
        char codeBuffer[32];
        char prefixBuffer[16];
        strncpy_P(prefixBuffer, codePrefix, sizeof(prefixBuffer));
        prefixBuffer[sizeof(prefixBuffer)-1] = '\0';

        snprintf(codeBuffer, sizeof(codeBuffer), "%s%d", prefixBuffer, responseCode);

        // Gambar tulisan "Kode: -1" atau semacamnya di tengah bawah
        u8g2.setCursor((128 - u8g2.getStrWidth(codeBuffer)) / 2, 45);
        u8g2.print(codeBuffer);
    }

    // Tampilkan semua ke layar
    u8g2.sendBuffer();

    // Delay ringan dan yield
    yield();

    // Pindah ke state berikutnya
    currentState = SystemState::WEIGHT_DISPLAY;
}