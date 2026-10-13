#include "ui.h"

#include <U8g2lib.h>
#include <cstring>

#include "config.h"

namespace {

constexpr int LCD_W = 128;
constexpr size_t NAME_CHARS = 13;  // 6x10 font from x=50: 13 x 6 px fits

U8G2_ST7920_128X64_F_SW_SPI lcd(U8G2_R0, Config::Pins::LCD_CLOCK, Config::Pins::LCD_DATA,
                                Config::Pins::LCD_CS, U8X8_PIN_NONE);

void drawCentered(int y, const char *text) {
  int x = (LCD_W - static_cast<int>(lcd.getStrWidth(text))) / 2;
  lcd.drawStr(x < 0 ? 0 : x, y, text);
}

// Inverted title bar across the top, as the original screens had.
void drawHeaderBar(const char *text) {
  lcd.setFont(u8g2_font_5x7_tr);
  lcd.setDrawColor(1);
  lcd.drawBox(0, 0, LCD_W, 9);
  lcd.setDrawColor(0);
  drawCentered(7, text);
  lcd.setDrawColor(1);
}

void drawField(int y, const char *label, const char *value) {
  char cut[NAME_CHARS + 1];
  strncpy(cut, value, NAME_CHARS);
  cut[NAME_CHARS] = '\0';
  lcd.setFont(u8g2_font_5x7_tr);
  lcd.drawStr(2, y, label);
  lcd.drawStr(40, y, ":");
  lcd.setFont(u8g2_font_6x10_tr);
  lcd.drawStr(50, y, cut);
}

const char *directionLabel(RfidFrame::Direction d) {
  return d == RfidFrame::Direction::OUT ? "KELUAR" : "MASUK";
}

}  // namespace

void beginUi() {
  lcd.begin();
  lcd.setBusClock(Config::LCD_BUS_CLOCK_HZ);
}

void uiBoot() {
  lcd.clearBuffer();
  lcd.setDrawColor(1);
  lcd.setFont(u8g2_font_helvB12_tr);
  drawCentered(24, "Timbangan");
  lcd.setFont(u8g2_font_6x10_tr);
  char line[32];
  snprintf(line, sizeof(line), "fw %s", Config::FIRMWARE_VERSION);
  drawCentered(42, line);
  lcd.setFont(u8g2_font_5x7_tr);
  drawCentered(58, Config::DEVICE_ID);
  lcd.sendBuffer();
}

void uiHome(const HomeView &v) {
  char timeStr[16];
  char dateStr[16];
  if (v.adjustField != 0) {
    const bool hide = !v.blinkOn;
    char h[4], m[4], s[4];
    snprintf(h, sizeof(h), "%02u", v.adjHour);
    snprintf(m, sizeof(m), "%02u", v.adjMinute);
    snprintf(s, sizeof(s), "%02u", v.adjSecond);
    snprintf(timeStr, sizeof(timeStr), "%s:%s:%s", (hide && v.adjustField == 1) ? "  " : h,
             (hide && v.adjustField == 2) ? "  " : m, (hide && v.adjustField == 3) ? "  " : s);
  } else if (v.time.valid) {
    snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u", v.time.hour, v.time.minute, v.time.second);
  } else {
    strcpy(timeStr, "--:--:--");
  }
  if (v.time.valid) {
    snprintf(dateStr, sizeof(dateStr), "%02u/%02u/%04u", v.time.day, v.time.month, v.time.year);
  } else {
    strcpy(dateStr, "--/--/----");
  }
  const char *wifi = v.wifiOk ? "OK" : "NC";

  lcd.clearBuffer();
  lcd.setDrawColor(1);

  lcd.setFont(u8g2_font_5x7_tr);
  lcd.drawStr(0, 7, dateStr);
  lcd.drawStr(LCD_W - lcd.getStrWidth(wifi), 7, wifi);

  lcd.setFont(u8g2_font_logisoso24_tr);
  drawCentered(45, timeStr);

  lcd.setFont(u8g2_font_6x10_tr);
  drawCentered(60, v.footer ? v.footer : "");

  lcd.sendBuffer();
}

void uiWeight(const RfidFrame::Card &card, bool weightLive, float weightKg) {
  lcd.clearBuffer();
  drawHeaderBar("Display Timbangan");

  drawField(19, "Driver", card.driverName);
  drawField(29, "No Pol", card.plate);
  drawField(39, "Cust", card.customerName);

  lcd.setFont(u8g2_font_5x7_tr);
  lcd.drawStr(2, 49, "Berat");
  lcd.drawStr(40, 49, ":");
  lcd.drawStr(105, 49, "/Kg");

  lcd.setFont(u8g2_font_ncenB10_tr);
  char weightStr[16];
  if (weightLive) snprintf(weightStr, sizeof(weightStr), "%.2f", static_cast<double>(weightKg));
  else strcpy(weightStr, "N/A");
  // Right-aligned against "/Kg" so a 5-digit truck weight never overlaps it.
  int x = 103 - static_cast<int>(lcd.getStrWidth(weightStr));
  lcd.drawStr(x < 46 ? 46 : x, 53, weightStr);

  lcd.setFont(u8g2_font_5x7_tr);
  if (card.direction == RfidFrame::Direction::IN) {
    lcd.drawBox(0, 56, 38, 8);
    lcd.setDrawColor(0);
    lcd.drawStr(6, 63, "Masuk");
  } else {
    lcd.drawBox(88, 56, 40, 8);
    lcd.setDrawColor(0);
    lcd.drawStr(94, 63, "Keluar");
  }
  lcd.setDrawColor(1);
  lcd.sendBuffer();
}

void uiSending(RfidFrame::Direction direction, uint8_t dots) {
  char header[24];
  snprintf(header, sizeof(header), "KIRIM DATA %s", directionLabel(direction));
  char text[16] = "Mengirim";
  for (uint8_t i = 0; i < dots && i < 3; ++i) strcat(text, ".");

  lcd.clearBuffer();
  drawHeaderBar(header);
  lcd.setFont(u8g2_font_helvB12_tr);
  lcd.drawStr(20, 38, text);  // fixed x: the dots grow without the word moving
  lcd.setFont(u8g2_font_6x10_tr);
  drawCentered(58, "Mohon tunggu");
  lcd.sendBuffer();
}

void uiResult(HttpOutcome::Verdict verdict, int httpCode, RfidFrame::Direction direction) {
  char header[24];
  snprintf(header, sizeof(header), "DATA %s", directionLabel(direction));
  char code[24];
  if (verdict == HttpOutcome::Verdict::NO_WIFI) strcpy(code, "Cek koneksi WiFi");
  else snprintf(code, sizeof(code), "Kode: %d", httpCode);

  lcd.clearBuffer();
  drawHeaderBar(header);
  lcd.drawFrame(2, 14, 124, 50);
  lcd.setFont(u8g2_font_helvB12_tr);
  drawCentered(32, HttpOutcome::lcdTitle(verdict));
  lcd.setFont(u8g2_font_6x10_tr);
  drawCentered(46, HttpOutcome::lcdDetail(verdict, direction == RfidFrame::Direction::OUT));
  lcd.setFont(u8g2_font_5x7_tr);
  drawCentered(59, code);
  lcd.sendBuffer();
}

void uiError(const char *line1, const char *line2) {
  lcd.clearBuffer();
  lcd.setDrawColor(1);
  lcd.drawBox(0, 0, LCD_W, 16);
  lcd.setDrawColor(0);
  lcd.setFont(u8g2_font_7x14B_tr);
  drawCentered(13, "ERROR");
  lcd.setDrawColor(1);
  lcd.setFont(u8g2_font_6x10_tr);
  lcd.drawStr(4, 32, line1);
  lcd.drawStr(4, 47, line2);
  lcd.drawHLine(0, 63, LCD_W);
  lcd.sendBuffer();
}

void uiShowRestarting(const char *what) {
  lcd.clearBuffer();
  lcd.setDrawColor(1);
  lcd.setFont(u8g2_font_helvB12_tr);
  drawCentered(30, "Restart...");
  lcd.setFont(u8g2_font_6x10_tr);
  drawCentered(48, what);
  lcd.sendBuffer();
}
