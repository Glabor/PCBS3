#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <SD_MMC.h>
#include <SPI.h>
#include <Wire.h>

int clk = 48;
int cmd = 37;
int d0 = 38;
int d1 = 39;
int d2 = 35;
int d3 = 36; // GPIO 34 is not broken-out on ESP32-S3-DevKitC-1 v1.1

String fn = "/a/abcdefghijkl/test1.txt";

// Replace with your WiFi credentials
// const char *ssid = "SENSAR 4661";
const char *ssid = "chgServer";
// const char *password = "H6{3g897";

// Create an instance of the server
AsyncWebServer server(80);

void handleFileList(AsyncWebServerRequest *request, String folderPath) {
    String html = "<html><body><h1>File List - " + folderPath + "</h1><ul>";

    File root = SD_MMC.open(folderPath);
    if (!root) {
        Serial.println("Failed to open folder");
        request->send(500, "text/plain", "Internal Server Error");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            String folderName = file.name();
            html += "<li><a style='color:red;' href='/?folder=" + folderPath + "/" + folderName + "'>" + folderName + "/</a></li>";
        } else {
            html += "<li><a href='/download?filename=" + String(file.path()) + "'>" + String(file.path()) + "</a></li>";
        }
        file.close();
        file = root.openNextFile();
    }
    html += "</ul></body></html>";
    request->send(200, "text/html", html);
}

void setup() {
    Serial.begin(115200);
    // init SD_MMC
    if (!SD_MMC.setPins(clk, cmd, d0, d1, d2, d3)) {
        Serial.println("Pin change failed!");
        return;
    }
    if (!SD_MMC.begin("/sdcard", true, false, 20000, 5)) {
        Serial.println("Card Mount Failed");
        return;
    }

    // create directories for file to save
    String name = fn;
    int index = 0;
    Serial.println("SUBSTRINGS OF " + name);
    while (name.indexOf("/", index) >= 0) {
        int start = name.indexOf("/", index) + 1;
        index = name.indexOf("/", index) + 1;
        int end = name.indexOf("/", index);
        if (end >= 0) {
            String dirCreate = SD_MMC.mkdir(name.substring(0, end)) ? "dir " + name.substring(0, end) + " created " : " dir not created ";
            Serial.println(dirCreate);
        } else {
            Serial.println("file : /" + name.substring(start));
        }
    }

    // write
    File file = SD_MMC.open(fn, FILE_WRITE);
    String fileOpen = file ? "file opened for writing" : "error opening file for writing";
    Serial.println(fileOpen);
    file.println(fn);
    file.close();

    // read
    File file2 = SD_MMC.open(fn, FILE_READ);
    fileOpen = file2 ? "file opened for reading" : "error opening file for reading";
    Serial.println(fileOpen);
    Serial.println("File Content:");
    while (file2.available()) {
        Serial.write(file2.read());
        // file2.read();
    }
    file2.close();

    // write binary
    File file3 = SD_MMC.open("/milli.txt", FILE_WRITE);
    fileOpen = file3 ? "file opened for writing" : "error opening file for writing";
    Serial.println(fileOpen);
    int startMilli = millis();
    int count = 0;
    while (millis() - startMilli < 1000) {
        int currentMilli = millis() - startMilli;
        byte buffer[4];
        file3.print(count);
        file3.print(",");
        file3.println(currentMilli);
        // buffer[0] = lowByte(currentMilli >> 8);
        // buffer[1] = lowByte(currentMilli);
        // buffer[2] = lowByte(count >> 8);
        // buffer[3] = lowByte(count);
        // file3.write(buffer, 4);
        count++;
        // delay(10);
    }
    file3.close();

    Serial.println("end write");

    // Connect to Wi-Fi
    // WiFi.begin(ssid, password);
    WiFi.begin(ssid);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.print("Connected to WiFi network with IP Address: ");
    Serial.println(WiFi.localIP());

    // Route to list all files and folders
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String folder = request->arg("folder");
        if (folder == "") {
            folder = "/";
        }
        handleFileList(request, folder);
    });

    // Route to download file
    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        // String filename = request->arg("filename");
        // File file = SD.open(filename);
        // Serial.println(filename);

        // if (file) {
        //     request->send(SD, filename, "text/plain", true);
        //     file.close();
        // } else {
        //     request->send(404, "text/plain", "File not found");
        // }

        int paramsNr = request->params();
        Serial.println(paramsNr);
        String downFile = "/1/test1.txt";

        for (int i = 0; i < paramsNr; i++) {

            AsyncWebParameter *p = request->getParam(i);
            Serial.print("Param name: ");
            Serial.println(p->name());
            Serial.print("Param value: ");
            Serial.println(p->value());
            Serial.println("------");
            downFile = p->value();
        }

        request->send(SD_MMC, downFile, "text/plain", true);
    });

    // Start server
    server.begin();
}

#define bright 5

#define LED_PIN 1

// void setup() {
//     // Set the LED pin as an output
//     pinMode(LED_PIN, OUTPUT);
// }

// void loop() {
//     rainbowLoop(50); // Change the value to adjust the speed of the rainbow effect
// }

// Function to set RGB color using neopixelWrite
void setRGBColor(uint8_t red, uint8_t green, uint8_t blue) {
    neopixelWrite(LED_PIN, red, green, blue);
}

// Function to combine RGB components into a 32-bit color value
uint32_t neopixelColor(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint32_t(red) << 16) | (uint32_t(green) << 8) | blue;
}

// Function to convert a Wheel position to RGB color
uint32_t Wheel(byte WheelPos) {
    WheelPos = 255 - WheelPos;
    if (WheelPos < 85) {
        return neopixelColor(255 - WheelPos * 3, 0, WheelPos * 3);
    }
    if (WheelPos < 170) {
        WheelPos -= 85;
        return neopixelColor(0, WheelPos * 3, 255 - WheelPos * 3);
    }
    WheelPos -= 170;
    return neopixelColor(WheelPos * 3, 255 - WheelPos * 3, 0);
}

// Function to create a rainbow effect using neopixelWrite for a single RGB LED
void rainbowLoop(int wait) {
    for (int j = 0; j < 256; j++) {
        // Calculate color based on position and current rainbow value (j)
        uint32_t color = Wheel(((j * 3) + 0) & 255); // Incrementing by 3 for a single RGB LED

        // Extract individual RGB components
        uint8_t red = (color >> 16) & 0xFF;
        uint8_t green = (color >> 8) & 0xFF;
        uint8_t blue = color & 0xFF;

        // Set the RGB color on the LED using neopixelWrite
        setRGBColor(red / 20, green / 20, blue / 20);

        delay(wait);
    }
}

void loop() {
#ifdef RGB_BUILTIN
    //   digitalWrite(RGB_BUILTIN, HIGH);   // Turn the RGB LED white
    //   delay(1000);
    //   digitalWrite(RGB_BUILTIN, LOW);    // Turn the RGB LED off
    //   delay(1000);
    rainbowLoop(50);
    // neopixelWrite(RGB_BUILTIN, bright, 0, 0); // Red
    // delay(1000);
    // neopixelWrite(RGB_BUILTIN, 0, bright, 0); // Green
    // delay(1000);
    // neopixelWrite(RGB_BUILTIN, 0, 0, bright); // Blue
    // delay(1000);
    // neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Off / black
    // delay(1000);
#endif
}