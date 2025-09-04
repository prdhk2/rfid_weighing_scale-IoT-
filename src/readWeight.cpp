#include "config.h"
#include "readWeight.h"
#include "extractNumericPart.h"

float readWeight() {
    static String buffer = "";

    while (rs232Serial.available()) {
        char c = rs232Serial.read();
        if (c == '\n') { // Akhir data dari timbangan
            String numericPart = extractNumericPart(buffer);
            buffer = "";  // Reset
            if (numericPart.length() > 0) {
                lastWeight = numericPart.toFloat();
            } else {
                return NAN;
            }
        } 
        else if (buffer.length() < 20) { // Hindari buffer overflow
            buffer += c;
        } 
        else {
            buffer = ""; // Reset jika terlalu panjang
        }
    }
    return lastWeight;

    yield();
}