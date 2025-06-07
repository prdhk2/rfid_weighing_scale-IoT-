#include "config.h"
#include "utils/timeUtils.h"

void HomeDisplay() {
    // Ambil waktu dari RTC
    DateTime now = getAdjustedTime(rtc.now());
    
    // Format waktu dan tanggal
    char timeStr[9];
    sprintf(timeStr, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    
    char dateStr[12];
    sprintf(dateStr, "%02d/%02d/%04d", now.day(), now.month(), now.year());

    // Status WiFi
    const char* wifiStatus = (WiFi.status() == WL_CONNECTED) ? "OK" : "NC";

    u8g2.setDrawColor(1);
    u8g2.setFontMode(0);
    u8g2.clearBuffer();

    // ===== BAGIAN ATAS =====
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 7, dateStr);

    // Status WiFi (kanan atas) - Font kecil
    u8g2.setFont(u8g2_font_5x7_tr);
    int wifiStatusWidth = u8g2.getStrWidth(wifiStatus);
    u8g2.drawStr(128 - wifiStatusWidth, 7, wifiStatus);

    // ===== BAGIAN TENGAH =====
    u8g2.setFont(u8g2_font_logisoso24_tr);
    int timeWidth = u8g2.getStrWidth(timeStr);
    u8g2.drawStr((128 - timeWidth) / 2, 40, timeStr);

    // ===== BAGIAN BAWAH =====
    u8g2.setFont(u8g2_font_6x10_tr);
    const char* tapMsg = "Tap Your Card";
    int msgWidth = u8g2.getStrWidth(tapMsg);
    u8g2.drawStr((128 - msgWidth) / 2, 60, tapMsg);

    u8g2.sendBuffer();
}