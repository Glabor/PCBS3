#include <Adafruit_ADXL375.h>
#include <Arduino.h>
#include <SPI.h>

#define ADXL_CS 2
#define RF_CS 12
#define B_SCK 42
#define B_MISO 41
#define B_MOSI 40

Adafruit_ADXL375 adxl = Adafruit_ADXL375(ADXL_CS, &SPI, 12345);

bool adxlSetup() {
    SPI.begin(B_SCK, B_MISO, B_MOSI, ADXL_CS);
    SPI.beginTransaction(SPISettings(100000, MSBFIRST, SPI_MODE0));
    digitalWrite(ADXL_CS, LOW);
    digitalWrite(RF_CS, HIGH);
    // delay(100);
    Serial.println("find adxl ?");

    if (!adxl.begin()) {
        Serial.println("could not find ADXL");
        return false;
    }
    adxl.setDataRate(ADXL3XX_DATARATE_3200_HZ);
    Serial.println("true");
    SPI.endTransaction();
    digitalWrite(ADXL_CS, HIGH);
    return true;
}

void devID() {
    SPI.begin(B_SCK, B_MISO, B_MOSI, ADXL_CS);
    SPI.beginTransaction(SPISettings(100000, MSBFIRST, SPI_MODE3));
    digitalWrite(ADXL_CS, LOW);
    digitalWrite(RF_CS, HIGH);
    // delay(100);
    Serial.println("find adxl ?");
    Serial.print("id:");
    SPI.transfer(0x00 | 0xC0);
    Serial.println(SPI.transfer(0x00), BIN);
    SPI.endTransaction();
    digitalWrite(ADXL_CS, HIGH);
    Serial.println();
}
void rate() {
    SPI.begin(B_SCK, B_MISO, B_MOSI, ADXL_CS);
    SPI.beginTransaction(SPISettings(100000, MSBFIRST, SPI_MODE3));
    digitalWrite(ADXL_CS, LOW);
    digitalWrite(RF_CS, HIGH);
    // delay(100);
    Serial.println("find adxl ?");
    Serial.print("id:");
    SPI.transfer(0x2c | 0x40);
    Serial.println(SPI.transfer(0xf), BIN);
    SPI.endTransaction();
    digitalWrite(ADXL_CS, HIGH);
    // Serial.println();
    SPI.begin(B_SCK, B_MISO, B_MOSI, ADXL_CS);
    SPI.beginTransaction(SPISettings(100000, MSBFIRST, SPI_MODE3));
    digitalWrite(ADXL_CS, LOW);
    digitalWrite(RF_CS, HIGH);
    // delay(100);
    Serial.println("find adxl ?");
    Serial.print("id:");
    SPI.transfer(0x2c | 0xc0);
    Serial.println(SPI.transfer(0x00), BIN);
    SPI.endTransaction();
    digitalWrite(ADXL_CS, HIGH);
    Serial.println();
}

void setup() {
    pinMode(ADXL_CS, OUTPUT);
    pinMode(B_MOSI, OUTPUT);
    pinMode(RF_CS, OUTPUT);
    digitalWrite(ADXL_CS, LOW);
    delay(1000);
    Serial.begin(115200);
    Serial.println("begin");
}

void loop() {
    Serial.println("loop");
    rate();
    // Serial.println(adxlSetup());
    delay(100);
}