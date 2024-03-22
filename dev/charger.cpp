#include <Arduino.h>

#include <SD_MMC.h>
// #include "SPIFFS.h"
#include "led_strip.h"
#include <AsyncElegantOTA.h>
#include <AsyncTCP.h>
#include <EEPROM.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <RTClib.h>
#include <SPI.h>
#include <Update.h>
#include <WiFi.h>

Preferences preferences;

#define EEPROM_SIZE 1
#define LED LED_BUILTIN
#define IN 39

String uid = "04 71 F1 79 B6 2A 81";
RTC_DS3231 rtc;

int firmCount = 3;

int clk = 7;
int cmd = 15;
int d0 = 6;
int d1 = 5;
int d2 = 17;
int d3 = 16; // GPIO 34 is not broken-out on ESP32-S3-DevKitC-1 v1.1

int globVar;
bool led_on = false;
unsigned int to = 2;
unsigned long p_to = 0;

unsigned long previousMillis = 0;
const long interval = 2000;

#define battPin 4
int battSend;
int tempLvl;
float battTemp;

int prev_time = 0;
bool stay = true; // bool to keep track if the update was done after switching to update mode
                  // goes back to true after it no longer sees power
bool chg = false; // bool to know if interrupt got triggered

const char *ssid = "chgServer";
// const char *password = "turbulent";
// const char *ssid = "Incubateur";
// const char *password = "IDescartes77420*";

// String serv= "http://172.20.10.4:5555/up/"; //iphone
// String serv= "http://192.168.125.182:5555/up/"; //incubateur
String serv = "http://10.42.0.1:5555/up/"; // raspi

String serverNameVar = serv + "var/";
String serverName = serv + "post/";
String serverNameUp = serv + "update/";
String serverNameDL = serv + "dl/";
String serverReadyUp = serv + "readyUp/";
String serverReadyDL = serv + "readyDL/";
String serverUID = serv + "postUID/";
String serverVAR = serv + "postVAR/";

AsyncWebServer server(80);

File root;
const char *html = "<p>%PLACEHOLDER%</p>";

String processor(const String &var) {
    String listHtml = "";
    File root = SD_MMC.open("/");
    File file = root.openNextFile();

    while (file) {

        Serial.print("FILE: ");
        Serial.println(file.path());
        // Serial.println(file.name());
        listHtml += "<a href = \"down/?file=" + String(file.path()) + "\">" + String(file.path()) + "</a><br />";

        file = root.openNextFile();
    }
    Serial.println(listHtml);
    Serial.println(var);

    if (var == "PLACEHOLDER")
        return listHtml;

    return String();
}

// void SPIFFS_test() {
//     if (!SPIFFS.begin(true)) {
//         Serial.println("An error occurred while mounting SPIFFS");
//         return;
//     }

//     File file = SPIFFS.open("/test/exa.txt", "w");
//     if (!file) {
//         Serial.println("Failed to open file for writing");
//         return;
//     }

//     file.println("Hello, ESP32!");

//     // Close the file
//     file.close();

//     Serial.println("Test complete");
// }

// get data from url
String httpGETRequest(String serverName) {
    WiFiClient client;
    HTTPClient http;
    // Your Domain name with URL path or IP address with path
    http.begin(client, serverName);
    // Send HTTP POST request
    int httpResponseCode = http.GET();
    String payload = "2";

    if (httpResponseCode > 0) {
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        payload = http.getString();
    } else {
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);
    }
    // Free resources
    http.end();

    return payload;
}

int httpPostRequest(String serverName, String postText) {
    WiFiClient client;
    HTTPClient http;
    int response = 0;
    // Your Domain name with URL path or IP address with path
    http.begin(client, serverName);
    // Send HTTP POST request
    http.addHeader("Content-Type", "text/plain");
    int httpResponseCode = http.POST(postText);

    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    // Free resources
    if (httpResponseCode > 0) {
        response = http.getString().toInt();
        Serial.println("text from post: " + String(response));
    }
    http.end();
    return response;
}

// void alarmSetup(void) {
//     // setup rtc at beginning
//     while (!rtc.begin())
//         ;
//     // if (rtc.lostPower()) {
//     //   //if internal battery of rtc is lost, readjust time to windows time
//     //   rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
//     // }
//     rtc.disable32K();
//     rtc.writeSqwPinMode(DS3231_OFF);
//     rtc.disableAlarm(2);
//     Serial.print("alarm fired : ");
//     Serial.println(rtc.alarmFired(1));
//     if (rtc.alarmFired(1)) {
//         rtc.setAlarm1(rtc.now() + TimeSpan(1), DS3231_A1_Date);
//     } else {
//         chg = true;
//         rtc.setAlarm1(rtc.now() + TimeSpan(30), DS3231_A1_Date);
//     }
//     Serial.println("alarm setup");
// }

void alarmSetup() {
    if (rtc.begin()) {
        rtc.disable32K();
        rtc.writeSqwPinMode(DS3231_OFF);
        rtc.disableAlarm(2);
        Serial.print("alarm fired : ");
        Serial.println(rtc.alarmFired(1));
        if (rtc.alarmFired(1)) {
            rtc.setAlarm1(rtc.now() + TimeSpan(1), DS3231_A1_Date);
        } else {
            chg = true;
            rtc.setAlarm1(rtc.now() + TimeSpan(30), DS3231_A1_Date);
        }
        Serial.println("alarm setup");
    } else {
        Serial.println("Couldn't find RTC");
        Serial.flush();
        while (1)
            delay(10);
    }
}

String dateRTC(void) {
    DateTime dateDate = rtc.now();
    String dateString = "";
    dateString += String(dateDate.year(), DEC);
    dateString += "/";
    dateString += String(dateDate.month(), DEC);
    dateString += "/";
    dateString += String(dateDate.day(), DEC);
    dateString += "    ";
    dateString += String(dateDate.hour(), DEC);
    dateString += ":";
    dateString += String(dateDate.minute(), DEC);
    dateString += ":";
    dateString += String(dateDate.second(), DEC);
    return dateString;
}

void measBatt(void) {
    // measure, send and save battery level
    float battVolt = 0;
    float cellVolt;
    delay(100);
    for (int battCount = 1; battCount <= 100; battCount++) {
        cellVolt = analogRead(battPin);
        if (cellVolt > battVolt) {
            battVolt = cellVolt;
        }
        // battVolt = ((battCount-1)*battVolt + lc.cellVoltage()) / battCount;
        delay(10);
    }
    battVolt = battVolt / 4096 * 3.3 * 2; // convert measure from 12bytes to volts
    // battVolt = 4.12;
    // battSend = (int)(battVolt * 100.0 * 1.09);
    battSend = (int)(battVolt * 100.0 * 1.0);

    int rtcTemp = int(rtc.getTemperature() * 10);
    Serial.print("temp : ");
    Serial.println(rtcTemp);
    Serial.print("batt : ");
    Serial.println(battSend);
    DateTime startDate = rtc.now();
    int startTime = startDate.unixtime();
    String nowStr = String(startTime);
}

bool wifiConnect() {
    int count = 0;
    // trying to connect to wifi a number of times
    for (size_t j = 0; j < 3; j++) {
        // WiFi.begin(ssid, password);
        WiFi.begin(ssid);
        Serial.println("Connecting");
        // Wait for connection (10 dots)

        while ((WiFi.status() != WL_CONNECTED) && count < 3) {
            for (int i = 0; i < 4; i++) {
                led_on = !led_on;
                // digitalWrite(LED, led_on);
                neopixelWrite(RGB_BUILTIN, 20, 0, 0); // Red
                delay(100);
            }
            neopixelWrite(RGB_BUILTIN, 0, 0, 0);
            Serial.print(".");
            delay(200);
            count++;
        }
        count = 0;
        // if connected : update value
        if (WiFi.status() == WL_CONNECTED) {
            neopixelWrite(RGB_BUILTIN, 0, 20, 0); // Green
            Serial.println("");
            Serial.print("Connected to WiFi network with IP Address: ");
            Serial.println(WiFi.localIP());

            return true;
        } else {
            Serial.println("WiFi connection trial failed");
        }
        // turn off wifi
        WiFi.disconnect(true);
        delay(500);
        // WiFi.mode>(WIFI_OFF);
    }
    return false;
}

void sendBatt() {
    if (wifiConnect()) {
        int responseCode = httpPostRequest(serverName, String(battSend));
        if (responseCode > 0) {
            return;
        }
    } else {
        Serial.println("WiFi Disconnected");
    }
    // turn off wifi
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
    WiFi.disconnect(true);
}

// void checkUpdate() {
//     if (wifiConnect()) {
//         int upCode = httpGETRequest(serverNameUp).toInt(); // state of the variable
//         if (upCode > 0) {
//             unsigned long upMillis = millis();
//             Serial.print("up response : ");
//             Serial.println(upCode);
//             Serial.println("Updating firmware");
//             httpPostRequest(serverReadyUp, String(battSend));
//             int upCount = 0;
//             server.begin();
//             // TODO set alarm 2min (so it doesnt clear in the middle and cannot get set again (or check when setting if its triggered))
//             while (upCount < 300) {
//                 if (millis() - upMillis > 2000) {
//                     upCount++;
//                     upMillis = millis();
//                     Serial.print(".");
//                 }
//             }
//             Serial.println("");
//         }
//     } else {
//         Serial.println("WiFi Disconnected");
//     }
//     // turn off wifi
//     neopixelWrite(RGB_BUILTIN, 0, 0, 0);
//     WiFi.disconnect(true);
// }

// void checkDL() {
//     if (wifiConnect()) {
//         int upCode = httpGETRequest(serverNameDL).toInt();
//         if (upCode > 0) {
//             unsigned long upMillis = millis();
//             Serial.print("dl response : ");
//             Serial.println(upCode);
//             Serial.println("Downloading files");
//             httpPostRequest(serverReadyDL, String(battSend));
//             int upCount = 0;
//             server.begin();
//             // TODO set alarm 2min (so it doesnt clear in the middle and cannot get set again (or check when setting if its triggered))
//             while (upCount < 300) {
//                 if (millis() - upMillis > 2000) {
//                     upCount++;
//                     upMillis = millis();
//                     Serial.print(".");
//                 }
//             }
//             Serial.println("");
//         }
//     } else {
//         Serial.println("WiFi Disconnected");
//     }
//     // turn off wifi
//     neopixelWrite(RGB_BUILTIN, 0, 0, 0);
//     WiFi.disconnect(true);
// }

void checkUID() {
    if (wifiConnect()) {
        int upCode = httpPostRequest(serverUID, uid);
        Serial.println("received from postUID : " + String(upCode));
        if (upCode > 0) {
            unsigned long upMillis = millis();
            int upCount = 0;
            server.begin();
            // TODO set alarm 2min (so it doesnt clear in the middle and cannot get set again (or check when setting if its triggered))
            while (upCount < 30) {
                if (millis() - upMillis > 2000) {
                    rtc.setAlarm1(rtc.now() + 10, DS3231_A1_Date);
                    upCount++;
                    upMillis = millis();
                    Serial.print(".");
                }
            }
            Serial.println("");
        }
    } else {
        Serial.println("WiFi Disconnected");
    }
    // turn off wifi
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
    WiFi.disconnect(true);
}

void goSleep(float sleepTime) {
    // setting a time to wake up and clearing alarm
    int sleepT = 12; // 12=1h

    int block = 300;
    int nowTime = rtc.now().unixtime();
    alarmSetup();
    while (!rtc.setAlarm1(rtc.now() + sleepTime, DS3231_A1_Date)) {
    }
    sendBatt();
    digitalWrite(LED, LOW);
    for (size_t sleepCount = 0; sleepCount < 5; sleepCount++) {
        digitalWrite(13, !led_on);
        led_on = !led_on;

        rtc.clearAlarm(1);
        delay(500);
        // when alarm cleared, the system shuts down since the battery is cut off
        // battery comes back on when time of alarm comes
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("begin");
    pinMode(LED, OUTPUT);
    pinMode(IN, INPUT_PULLUP);
    pinMode(battPin, INPUT);
    analogReadResolution(12);
    Wire.begin(35, 36);

    // if (!SPIFFS.begin(true)) {
    //     Serial.println("An Error has occurred while mounting SPIFFS");
    //     return;
    // }
    // SPIFFS_test();
    if (!SD_MMC.setPins(clk, cmd, d0, d1, d2, d3)) {
        Serial.println("Pin change failed!");
        return;
    }
    if (!SD_MMC.begin("/sdcard", true, false, 20000, 5)) {
        Serial.println("Card Mount Failed");
        return;
    }

    alarmSetup();
    measBatt();
    Serial.println(dateRTC());
    // digitalWrite(LED, HIGH);
    neopixelWrite(RGB_BUILTIN, 0, 0, 20); // Red

    preferences.begin("charger", false);
    to = preferences.getUInt("to", 2);
    preferences.end();

    // EEPROM.begin(EEPROM_SIZE);
    // to = EEPROM.read(0);
    Serial.printf("to is : %d\n", to);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Hi! I am ESP32.");
    });

    server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
        rtc.setAlarm1(rtc.now() + 10, DS3231_A1_Date);
        Serial.println("alarm set for 10");
        Serial.println("restarting");
        ESP.restart();
        request->send(200, "text/plain", "Hi! I am ESP32.");
    });

    server.on("/down", HTTP_GET, [](AsyncWebServerRequest *request) {
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
    server.on("/rend", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SD_MMC, "/1/test1.txt", "text/plain", false);
    });
    server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", html, processor);
    });

    AsyncElegantOTA.begin(&server); // Start ElegantOTA
}

void varUpdate(void) {
    if (wifiConnect()) {
        to = httpPostRequest(serverVAR, uid);
        to = (to != 0) ? to : 5;
        Serial.println("got var : " + String(to));
        preferences.begin("charger", false);
        Serial.print("previous to : ");
        Serial.println(preferences.getUInt("to", 2));
        to = preferences.putUInt("to", to);
        Serial.println("to: " + String(preferences.getUInt("to", 2)));
        preferences.end();
    }
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
    WiFi.disconnect(true);
}

void blink(int c) {
    int bright = 20;
    for (size_t i = 0; i < c; i++) {
        led_on = !led_on;
        if (led_on) {
            neopixelWrite(RGB_BUILTIN, 0, 0, bright); // Blue
        } else {
            neopixelWrite(RGB_BUILTIN, 0, 0, 0); // Black
        }
        delay(100);
    }
}

int prog_count = 0;
int battCount = 0;
void loop() {
    unsigned long currentMillis = millis();
    if (chg) {
        // digitalWrite(LED, !digitalRead(IN));
        Serial.println("chg");
        // if previous try was unsuccessful and enough time passed, try again
        if (currentMillis - previousMillis >= interval) {
            battCount++;
            rtc.clearAlarm(1);
            rtc.setAlarm1(rtc.now() + 10, DS3231_A1_Date);
            previousMillis = currentMillis;
            Serial.println("alarm set for 10");
            // try to connect to wifi and update variable
            if (stay) {
                // WiFi.begin(ssid, password);
                if (wifiConnect()) {
                    varUpdate();
                    stay = false;
                } else {
                    Serial.println("WiFi Disconnected");
                }
                // turn off wifi
                neopixelWrite(RGB_BUILTIN, 0, 0, 0);
                WiFi.disconnect(true);
            } else {
                // once it connected to wifi and waited a bit, send battery and check if firmware update is available
                if (battCount > 5) {
                    sendBatt();
                    // checkUpdate();
                    // checkDL();
                    checkUID();
                    varUpdate();
                    battCount = 0;
                }
            }
        }

    } else {
        // normal program without charge, blinka given number of times before sleeping
        stay = true;
        int m_to = (to < 20) ? to : 20; // min between timeout and 20
        // if delay greater than timeout*100, blink
        if ((currentMillis - p_to) >= (m_to * 100)) {
            // blink number depends on firmware version
            blink(firmCount);
            p_to = currentMillis;
            prog_count++;
            Serial.println(digitalRead(IN));
        }
    }

    // sleep once program is done running
    if (prog_count > 40) {
        Serial.print("sleep : ");
        Serial.println(dateRTC());
        goSleep(20); /// 1800
        ESP.restart();
    }
}