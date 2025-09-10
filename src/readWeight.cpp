#include "config.h"
#include "readWeight.h"
#include "extractNumericPart.h"

String extractNumericPart(String inputString) {
    String numericPart = "";
    bool foundNumeric = false;
    bool foundDecimal = false;
    bool foundNegative = false;

    inputString.trim();

    for (int i = 0; i < inputString.length(); i++) {
        char currentChar = inputString.charAt(i);

        if (isdigit(currentChar)) {
            numericPart += currentChar;
            foundNumeric = true;
        } else if (currentChar == '.' && !foundDecimal && foundNumeric) {
            numericPart += currentChar;
            foundDecimal = true;
        } else if (currentChar == '-' && !foundNumeric && !foundNegative) {
            numericPart += currentChar;
            foundNegative = true;
        } else if (foundNumeric) {
            break;
        }
    }

    return numericPart;
}

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