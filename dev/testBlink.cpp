// #include "Adafruit_LC709203F.h"
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#define LED 1
#define SDA 1
#define SCL 2

// Adafruit_LC709203F lc;

void setup() {
    // No need to initialize the RGB LED
    delay(2000);
    Serial.begin(115200);
    Serial.println("begin");
    Wire.begin(SDA, SCL);
    // if (!lc.begin()) {
    //     Serial.println(F("Couldnt find Adafruit LC709203F?\nMake sure a battery is plugged in!"));
    //     while (1)
    //         delay(10);
    // }
    // Serial.println(F("Found LC709203F"));
}

#define bright 12
// the loop function runs over and over again forever
void loop() {
#ifdef RGB_BUILTIN
    //   digitalWrite(RGB_BUILTIN, HIGH);   // Turn the RGB LED white
    //   delay(1000);
    //   digitalWrite(RGB_BUILTIN, LOW);    // Turn the RGB LED off
    //   delay(1000);

    neopixelWrite(RGB_BUILTIN, bright, 0, 0); // Red
    delay(1000);
    neopixelWrite(RGB_BUILTIN, 0, bright, 0); // Green
    delay(1000);
    neopixelWrite(RGB_BUILTIN, 0, 0, bright); // Blue
    delay(1000);
    neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Off / black
    delay(1000);
    Serial.println("loop");
#endif
}