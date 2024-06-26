// Import required libraries
#include "ESPAsyncWebServer.h"
#include "SPI.h"
#include "SPIFFS.h"
#include "WiFi.h"
#include <Adafruit_ADXL375.h>
#include <Adafruit_LSM6DSO32.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <ElegantOTA.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <RHSoftwareSPI.h>
#include <RH_RF95.h>
#include <RTClib.h>
#include <SD_MMC.h>

#define BOOT0 0
#define LED 1
#define SDA 14
#define SCL 21
#define clk 48
#define cmd 37
#define d0 38
#define d1 39
#define d2 35
#define d3 36 // GPIO 34 is not broken-out on ESP32-S3-DevKitC-1 v1.1

#define ADXL375_SCK 42
#define ADXL375_MISO 41
#define ADXL375_MOSI 40
#define ADXL375_CS 2
#define LORA_CS 12

#define ON_SICK 13
#define SICK1 7
#define RFM95_RST 10
#define RFM95_INT 11
#define RFM95_CS 12
#define battPin 9

#define RF95_FREQ 433.0

// Replace with your network credentials
String ssid = "raspi-Wifi";
String password = "sensarPWD";

String soft_ap_ssid = "MyESP32AP";
String soft_ap_password = "testpassword";

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

// Stores LED state
int ledPin = 4;
String ledState;
float bright = 12.0;
float color[3] = {0., 0., 0.};

byte sdBuf[200];
int r = 0;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
JsonDocument prints;

RTC_DS3231 rtc;
Adafruit_LSM6DSOX dsox; // accelerometer
sensors_event_t accel;
sensors_event_t gyro;
sensors_event_t temp;
// Adafruit_ADXL375 adxl = Adafruit_ADXL375(ADXL375_CS, &SPI, 12345);
Adafruit_ADXL375 adxl = Adafruit_ADXL375(ADXL375_SCK, ADXL375_MISO, ADXL375_MOSI, ADXL375_CS, 12345);

RHSoftwareSPI rhSPI(RHSoftwareSPI::Frequency1MHz, RHSoftwareSPI::BitOrderMSBFirst, RHSoftwareSPI::DataMode0);

RH_RF95 rf95(RFM95_CS, RFM95_INT, rhSPI);
Preferences preferences;
int id = 0;
int blink = 0;
bool chg = false; // bool to know if system is on charge and needs to adapt

int genVar = 5;
int printInt = 0;
bool bLSM = false;
bool bS_LSM = false;
bool bS_ADXL = false;
bool bS_SICK = false;
bool bADXL = false;
bool bSick = false;
bool bLora = false;
bool bWifi = false;

unsigned int to = 2;
String uid = "04 71 F1 79 B6 2A 81";
int battSend;

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
    case WS_EVT_CONNECT:
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        break;
    case WS_EVT_DISCONNECT:
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
        break;
    case WS_EVT_DATA:
        handleWebSocketMessage(arg, data, len);
        break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
        break;
    }
}

void initWebSocket() {
    ws.onEvent(onEvent);
    server.addHandler(&ws);
}

bool rf95Setup(void) {
    /*
    setup rf95 module at beginning
    do only once in program since it seems to freeze if done again after
    */
    bool rfSetup = false;
    digitalWrite(ADXL375_CS, HIGH);
    digitalWrite(RFM95_CS, LOW);

    digitalWrite(RFM95_RST, LOW);
    delay(100);
    digitalWrite(RFM95_RST, HIGH);
    delay(100);
    Serial.println("try rf init");
    if (!rf95.init()) {
        Serial.println("LoRa radio init failed");
        return rfSetup;
    }
    rfSetup = true;
    Serial.println("LoRa radio init OK!");
    rf95.setFrequency(RF95_FREQ);
    rf95.setTxPower(23, false);
    // set Bandwidth (7800,10400,15600,20800,31250,41700,62500,125000,250000, 500000)
    int bwSet = 125000;
    rf95.setSignalBandwidth(bwSet);
    // set Coding Rate (5, 6, 7, 8)
    int crSet = 5;
    rf95.setCodingRate4(crSet);
    // set Spreading Factor (6->64, 7->128, 8->256, 9->512, 10->1024, 11->2048, 12->4096)
    int sfSet = 7;
    rf95.setSpreadingFactor(sfSet);
    // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on
    return rfSetup;
}

float measBatt() {
    float cellVolt;
    int count = 0;
    for (int battCount = 1; battCount <= 100; battCount++) {
        count++;
        float meas = analogRead(battPin);
        cellVolt = (float)((float)((float)(cellVolt * (count - 1)) + (float)analogRead(battPin)) / (float)count); // reading pin to measure battery level
    }
    cellVolt = cellVolt / 4096 * 3.3 * 2; // convert measure from 12bytes to volts
    battSend = cellVolt * 100;
    return cellVolt;
}

void rtcSetup() {
    if (rtc.begin()) {
        rtc.disable32K();
        rtc.writeSqwPinMode(DS3231_OFF);
        rtc.disableAlarm(2);
        Serial.print("alarm fired : ");
        Serial.println(rtc.alarmFired(1));
        if (!rtc.alarmFired(1)) {
            chg = true;
        }
        rtc.setAlarm1(rtc.now() + TimeSpan(1), DS3231_A1_Date);
        Serial.println("alarm setup");
    } else {
        Serial.println("Couldn't find RTC");
        Serial.flush();
        ESP.restart();
    }
}

bool sdmmcSetup() {
    if (!SD_MMC.setPins(clk, cmd, d0, d1, d2, d3)) {
        Serial.println("Pin change failed!");
        return false;
    }
    if (!SD_MMC.begin("/sdcard", true, false, 20000, 5)) {
        Serial.println("Card Mount Failed");
        neopixelWrite(LED, 0, bright, bright);
        delay(500);
        return false;
    }
    return true;
}

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
            html += "<li><a style='color:red;' href='/sd?folder=" + folderPath + "/" + folderName + "'>" + folderName + "/</a>      " +
                    "<a href='/removeFolder?filename=" + String(file.path()) + "'>delete</ a> </li> ";
        } else {
            html += "<li><a href='/download?filename=" + String(file.path()) + "'>" + String(file.path()) + "</a>     " +
                    "<a href='/removeFile?filename=" + String(file.path()) + "'>delete</ a> </li> ";
        }
        file.close();
        file = root.openNextFile();
    }
    html += "</ul><p><a href='/'><button class='button rtc obj'>HOME</button></a></p></body></html>";
    request->send(200, "text/html", html);
}

bool lsmSetup(void) {
    /*set up accelerometer*/
    if (!dsox.begin_I2C()) {
        Serial.println("LSM not found");
        return false;
    };
    Serial.println("LSM ok");

    dsox.setAccelDataRate(LSM6DS_RATE_6_66K_HZ);
    dsox.setAccelRange(LSM6DS_ACCEL_RANGE_8_G);
    dsox.setGyroRange(LSM6DS_GYRO_RANGE_125_DPS);
    dsox.setGyroDataRate(LSM6DS_RATE_6_66K_HZ);

    return true;
}

bool adxlSetup(void) {
    SPI.begin(ADXL375_SCK, ADXL375_MISO, ADXL375_MOSI, ADXL375_CS);
    SPI.beginTransaction(SPISettings(100000, MSBFIRST, SPI_MODE3));
    digitalWrite(RFM95_CS, HIGH);
    digitalWrite(ADXL375_CS, LOW);
    if (!adxl.begin()) {
        Serial.println("could not find ADXL");
        return false;
    }
    adxl.setDataRate(ADXL3XX_DATARATE_3200_HZ);
    SPI.endTransaction();
    digitalWrite(ADXL375_CS, HIGH);
    return true;
}

String printLocalTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return String();
    }
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    String date = String(timeinfo.tm_year + 1900) + "/" +
                  String(timeinfo.tm_mon + 1) + "/" +
                  String(timeinfo.tm_mday) + " " +
                  String(timeinfo.tm_hour) + ":" +
                  String(timeinfo.tm_min) + ":" +
                  String(timeinfo.tm_sec);
    Serial.println(date);
    char unixTime[15];
    strftime(unixTime, 15, "%s", &timeinfo);
    Serial.println(unixTime);
    Serial.println();
    return date;
}

void syncRTC() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    printLocalTime();

    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        delay(100);
    }
    if (!rtc.begin()) {
        Serial.println("Couldn't find RTC");
        Serial.flush();
        return;
    }

    DateTime now_esp = DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    rtc.adjust(now_esp);
}

// Replaces placeholder with LED state value
String processor(const String &var) {
    if (var == "TIMESTAMP") {
        Serial.print(var);
        Serial.print(" : ");
        Serial.println(rtc.now().unixtime());
        DateTime dt = rtc.now();
        String date = String(dt.year()) + "/" +
                      String(dt.month()) + "/" +
                      String(dt.day()) + " " +
                      String(dt.hour()) + ":" +
                      String(dt.minute()) + ":" +
                      String(dt.second());

        date += " - " + String(rtc.now().unixtime());
        return (date);
    }
    if (var == "ID") {
        preferences.begin("prefid", false);
        int idRead = preferences.getUInt("id", 0);
        preferences.end();

        return (String(idRead));
    }
    if (var == "BLINK") {
        preferences.begin("prefid", false);
        int idRead = preferences.getUInt("blink", 5);
        preferences.end();

        return (String(idRead));
    }
    if (var == "SSID") {
        preferences.begin("prefid", false);
        String prefSSID = preferences.getString("SSID", ssid);
        preferences.end();

        return (String(prefSSID));
    }
    if (var == "PWD") {
        preferences.begin("prefid", false);
        String prefPWD = preferences.getString("PWD", password);
        preferences.end();

        return (String(prefPWD));
    }
    if (var == "GENERAL") {
        return (String(genVar));
    }
    if (var == "BATTERY") {
        return (String(measBatt()) + "V   --   " + String(rtc.getTemperature()) + "&degC");
    }
    return String();
}

void goSleep(int sleepTime) {
    rtc.setAlarm1(rtc.now() + sleepTime, DS3231_A1_Date);
    Serial.println("sleeping " + String(sleepTime) + "s");
    rtc.clearAlarm(1);
    delay(500);
    Serial.println("Alarm cleared");
    ESP.restart();
}

void serverRoutes() {
    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/index.html", String(), false, processor);
    });

    // Route to load style.css file
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/style.css", "text/css");
    });
    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/script.js", "text/javascript");
    });

    // Route to list all files and folders
    server.on("/sd", HTTP_GET, [](AsyncWebServerRequest *request) {
        String folder = request->arg("folder");
        if (folder == "") {
            folder = "/";
        }
        handleFileList(request, folder);
    });

    // Route to download file
    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
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

    // Route to remove file
    server.on("/removeFile", HTTP_GET, [](AsyncWebServerRequest *request) {
        int paramsNr = request->params();
        Serial.println(paramsNr);
        String remFile = "/1/test1.txt";

        for (int i = 0; i < paramsNr; i++) {
            AsyncWebParameter *p = request->getParam(i);
            Serial.print("Param name: ");
            Serial.println(p->name());
            Serial.print("Param value: ");
            Serial.println(p->value());
            Serial.println("------");
            remFile = p->value();
        }
        if (SD_MMC.remove(remFile)) {
            neopixelWrite(LED, 0, bright, 0); // G
            delay(50);
        } else {
            neopixelWrite(LED, bright, 0, 0); // R
            delay(50);
        };
        request->redirect("/sd");
    });

    // Route to remove folder
    server.on("/removeFolder", HTTP_GET, [](AsyncWebServerRequest *request) {
        int paramsNr = request->params();
        Serial.println(paramsNr);
        String remFile = "/1/test1.txt";

        for (int i = 0; i < paramsNr; i++) {
            AsyncWebParameter *p = request->getParam(i);
            Serial.print("Param name: ");
            Serial.println(p->name());
            Serial.print("Param value: ");
            Serial.println(p->value());
            Serial.println("------");
            remFile = p->value();
        }
        if (SD_MMC.rmdir(remFile)) {
            neopixelWrite(LED, 0, bright, 0); // G
            delay(50);
        } else {
            neopixelWrite(LED, bright, 0, 0); // R
            delay(50);
        };
        request->redirect("/sd");
    });

    Serial.println("server ok");
}

bool initSPIFFS() {
    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        neopixelWrite(LED, bright, 0, 0); // R

        return false;
    }
    return true;
}

bool wifiConnect() {
    // Connect to Wi-Fi
    int count1 = 0;
    int count2 = 0;
    WiFi.mode(WIFI_MODE_APSTA);
    if (WiFi.status() != WL_CONNECTED) {
        while (count1 < 3 && (WiFi.status() != WL_CONNECTED)) {
            count1++;
            WiFi.begin(ssid, password);
            // WiFi.begin(ssid);
            while (count2 < 3 && (WiFi.status() != WL_CONNECTED)) {
                count2++;
                delay(100);
                neopixelWrite(LED, bright, 0, 0); // R
                delay(100);
                neopixelWrite(LED, 0, 0, 0); // 0
                delay(100);
                Serial.println("Connecting to WiFi..");
            }
            if (WiFi.status() == WL_CONNECTED) {
                neopixelWrite(LED, 0, bright, 0); // G
                Serial.println(WiFi.localIP());
            } else {
                WiFi.disconnect(true);
            }
        }
    }
    WiFi.softAP(soft_ap_ssid, soft_ap_password);

    if (!MDNS.begin("esp32")) {
        Serial.println("Error setting up MDNS responder!");
    }
    Serial.println("mDNS responder started");

    server.begin();

    return (WiFi.status() == WL_CONNECTED);
}

void initBlink() {
    neopixelWrite(LED, bright, 0, 0); // R
    delay(200);
    neopixelWrite(LED, 0, 0, 0); // 0
    delay(200);
    neopixelWrite(LED, 0, 0, bright); // B
    delay(200);
}

void accBuffering(int meas) {
    // divide int to two bytes
    sdBuf[r] = highByte(meas);
    r++;
    sdBuf[r] = lowByte(meas);
    r++;
}

bool initSens(String sens) {
    Serial.println(sens);
    if (sens == "lsm") {
        return lsmSetup();
    } else if (sens == "adxl") {
        return adxlSetup();
    } else if (sens == "sick") {
        digitalWrite(ON_SICK, HIGH);
        return true;
    }
    return false;
}

void getSens(String sens) {
    if (sens == "lsm") {
        sensors_event_t event;
        dsox.getEvent(&accel, &gyro, &temp);
        accBuffering((int)(accel.acceleration.x * 100));
        accBuffering((int)(accel.acceleration.y * 100));
        accBuffering((int)(accel.acceleration.z * 100));
        return;
    } else if (sens == "adxl") {
        sensors_event_t event;
        adxl.getEvent(&event);
        accBuffering((int)(event.acceleration.x * 100));
        accBuffering((int)(event.acceleration.y * 100));
        accBuffering((int)(event.acceleration.z * 100));
        return;
    } else if (sens == "sick") {
        int micros1 = micros();
        int count1 = 0;
        float val1 = 0;
        while ((micros() - micros1) < 1000) {
            count1++;
            val1 = (float)((val1 * (count1 - 1) + (float)analogRead(SICK1)) / (float)count1); // read adc
        }
        int val = (int)val1;
        accBuffering(val);
        return;
    }
    return;
}

void saveSens(String sens) {
    if (!initSens(sens)) {
        return;
    }
    // if (!sdmmcSetup()) {
    //     Serial.println("SD_MMC setup failed");
    //     return;
    // }
    // create folder for file
    DateTime startDate = rtc.now();
    int startTime = startDate.unixtime();
    // create folder to save chunk of data
    String fileDate = String(startTime);
    String beginStr = fileDate.substring(0, 5);
    String endStr = fileDate.substring(5, 10);

    String name = "/" + sens + "/" + beginStr + "/" + endStr + "/" + sens + ".bin";
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

    int accTime = genVar;
    // String fn = "/" + sens + ".bin";
    String fn = name;
    Serial.println(fn);
    int startMillis = millis();
    File file = SD_MMC.open(fn, FILE_WRITE);
    unsigned long time0 = micros();

    if (file) {
        while ((millis() - startMillis) < accTime * 1000) {
            // change LED color
            float prog = ((float)(millis() - startMillis)) / ((float)(accTime * 1000));
            neopixelWrite(LED, bright * (1.0 - prog), 0, bright * prog); // r->b

            // get data;
            r = 0;
            // accBuffering((int)(millis() - startMillis));
            unsigned long ta_micro = micros() - time0;
            for (size_t i = 0; i < 4; i++) {
                sdBuf[r] = lowByte(ta_micro >> 8 * (3 - i));
                r++;
            }

            getSens(sens);

            // write data
            for (int j = 0; j < r; j++) {
                file.write(sdBuf[j]);
            }
        }
    }
    file.flush();
    file.close();
    digitalWrite(ON_SICK, bSick);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        String message = (char *)data;
        prints = JsonDocument();
        prints["print"] = message;

        if (message == "on") {
            color[0] = bright;
            color[1] = 0.;
            color[2] = bright / 2;
            neopixelWrite(LED, color[0], color[1], color[2]); // rose

        } else if (message == "off") {
            color[0] = 0.;
            color[1] = bright;
            color[2] = bright;
            neopixelWrite(LED, color[0], color[1], color[2]); // rose
            // neopixelWrite(LED, 0, bright, bright); // cyan
        } else if (message == "alarm") {
            goSleep(genVar);
        } else if (message == "restart") {
            ESP.restart();
        } else if (message == "sync") {
            syncRTC();
        } else if (message == "sick") {
            bSick = !bSick;
            digitalWrite(ON_SICK, bSick);
            Serial.println("sick");
        } else if (message == "wifi") {
            Serial.println("wifi");
        } else if (message == "lsm") {
            bLSM = !bLSM;
            printInt = 0;
        } else if (message == "s_lsm") {
            bS_LSM = true;
        } else if (message == "s_adxl") {
            bS_ADXL = true;
        } else if (message == "s_sick") {
            bS_SICK = true;
        } else if (message == "adxl") {
            bADXL = !bADXL;
            adxl.printSensorDetails();
            Serial.println("");
        } else {
            // JSONVar myObject = JSON.parse((char *)data);
            JsonDocument myObject;
            deserializeJson(myObject, data);
            // from https://github.com/arduino-libraries/Arduino_JSON/blob/master/examples/JSONObject/JSONObject.ino
            if (myObject.containsKey("id")) {
                prints["print"] = String((const char *)myObject["id"]).toInt();

                id = String((const char *)myObject["id"]).toInt();

                preferences.begin("prefid", false);
                preferences.putUInt("id", id);
                preferences.end();
            }
            if (myObject.containsKey("blink")) {
                prints["print"] = String((const char *)myObject["blink"]).toInt();

                blink = String((const char *)myObject["blink"]).toInt();

                preferences.begin("prefid", false);
                preferences.putUInt("blink", blink);
                preferences.end();
            }
            if (myObject.containsKey("ssid")) {
                prints["print"] = String((const char *)myObject["ssid"]);

                ssid = (const char *)myObject["ssid"];

                preferences.begin("prefid", false);
                preferences.putString("SSID", ssid);
                preferences.end();
            }
            if (myObject.containsKey("pwd")) {
                prints["print"] = String((const char *)myObject["pwd"]);

                password = (const char *)myObject["pwd"];

                preferences.begin("prefid", false);
                preferences.putString("PWD", password);
                preferences.end();
            }
            if (myObject.containsKey("gen")) {
                prints["print"] = String((const char *)myObject["gen"]).toInt();
                genVar = String((const char *)myObject["gen"]).toInt();
            }
        }

        String stringPrints;
        serializeJson(prints, stringPrints);
        ws.textAll(stringPrints);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("begin");
    Wire.begin(SDA, SCL);
    SPI.begin(ADXL375_SCK, ADXL375_MISO, ADXL375_MOSI);
    rhSPI.setPins(ADXL375_MISO, ADXL375_MOSI, ADXL375_SCK);

    pinMode(LED, OUTPUT);
    pinMode(ADXL375_SCK, OUTPUT);
    pinMode(ADXL375_MOSI, OUTPUT);
    pinMode(ADXL375_MISO, INPUT_PULLDOWN);
    pinMode(ADXL375_CS, OUTPUT);
    pinMode(RFM95_CS, OUTPUT);
    pinMode(RFM95_INT, INPUT);
    pinMode(RFM95_RST, OUTPUT);
    pinMode(BOOT0, INPUT_PULLUP);
    pinMode(ON_SICK, OUTPUT);
    pinMode(SICK1, INPUT_PULLDOWN);
    pinMode(battPin, INPUT_PULLDOWN);

    digitalWrite(ADXL375_CS, HIGH);
    digitalWrite(RFM95_CS, HIGH);
    digitalWrite(ON_SICK, bSick);
    analogReadResolution(12);

    initBlink();

    if (!initSPIFFS()) {
        return;
    }
    sdmmcSetup();

    preferences.begin("prefid", false);
    id = preferences.getUInt("id", 0);
    blink = preferences.getUInt("blink", 5);
    preferences.end();

    initWebSocket();
    rtcSetup();
    lsmSetup();
    adxlSetup();
    if (!rf95Setup()) {
        Serial.println("Could not setup lora");
    }
    measBatt();
    serverRoutes();
    bWifi = true;

    preferences.begin("prefid", false);
    ssid = preferences.getString("SSID", ssid);
    password = preferences.getString("PWD", password);
    preferences.end();

    color[0] = bright;
    color[1] = bright / 2;
    color[2] = 0;

    ElegantOTA.begin(&server); // Start ElegantOTA
}

bool bBoot0 = false;
bool bBlink = false;

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

void rainbowLoop(int wait) {
    for (int j = 0; j < 256; j++) {
        uint32_t color = Wheel(((j * 3) + 0) & 255); // Incrementing by 3 for a single RGB LED
        uint8_t red = (color >> 16) & 0xFF;
        uint8_t green = (color >> 8) & 0xFF;
        uint8_t blue = color & 0xFF;
        neopixelWrite(LED, red / 20, green / 20, blue / 20);
        delay(wait);
    }
}

int httpPostRequest(String serverName, String postText) {
    WiFiClient client;
    HTTPClient http;
    int response = -1;
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

int sendFlask() {
    JsonDocument info;
    info["batt"] = battSend;
    info["bWifi"] = bWifi;
    String infoPost;
    serializeJson(info, infoPost);
    int responseCode = httpPostRequest("http://chg.local:5000/batt", infoPost);
    Serial.println(responseCode);
    if (responseCode > 0) {
        // connected flask on
        bWifi = true;
        return responseCode;
    } else if (responseCode == 0) {
        // connected flask on / no wifi
        bWifi = false;
        return responseCode;
    }
    // connected flask off
    return responseCode;
}

bool manageCOM() {
    bool local = false; // chg ? listen : sleep; and try again later
    bool wConnect = false;
    wConnect = wifiConnect();
    if (wConnect) {
        int respFlask = sendFlask();
        if (respFlask > 0) {
            // flask on + wifi on
            local = false;
        } else {
            // flask on or off / local true
            local = true;
        }
    } else {
        // not connected / local true
        local = true;
    }
    return local;
}

int manageLoop() {
    /*
    define timeouts until next try to communicate
    define color of LED
    */
    int comTO = 30;
    bool local = manageCOM();
    if (!local) {
        // flask on
        comTO = 10;
        color[0] = bright;
        color[1] = bright / 2;
        color[2] = 0;
    } else {
        if (!chg) {
            // sleep
            goSleep(30);
        } else {
            // listen local
            comTO = 60;
            color[0] = bright / 2;
            color[1] = 0;
            color[2] = bright / 2;
        }
    }
    return comTO;
}

void loopBlink() {
    // blinking
    float colorB[3] = {};
    if (bBlink) {
        colorB[0] = color[0];
        colorB[1] = color[1];
        colorB[2] = color[2];
    } else {
        colorB[0] = 0;
        colorB[1] = 0;
        colorB[2] = 0;
    }
    neopixelWrite(LED, colorB[0], colorB[1], colorB[2]);
    bBlink = !bBlink;
}

void loopWS() {
    // sending with websockets
    bool bBoot0Change = (digitalRead(BOOT0) != bBoot0);
    bBoot0 = bBoot0Change ? !bBoot0 : bBoot0;
    prints["BOOT0"] = bBoot0 ? "ON" : "OFF";

    if (bSick) {
        float sickMeas = analogRead(SICK1);
        Serial.println(sickMeas);
        prints["sick"] = String(sickMeas);
    }
    if (bLSM) {
        dsox.getEvent(&accel, &gyro, &temp);
        prints["lsm"] = String(accel.acceleration.x) + ',' +
                        String(accel.acceleration.y) + ',' +
                        String(accel.acceleration.z);
    }
    if (bS_LSM) {
        saveSens("lsm");
        bS_LSM = false;
        neopixelWrite(LED, bright, bright / 2, 0); // Orange
    }
    if (bS_ADXL) {
        saveSens("adxl");
        bS_ADXL = false;
        neopixelWrite(LED, bright, bright / 2, 0); // Orange
    }
    if (bS_SICK) {
        saveSens("sick");
        bS_SICK = false;
        neopixelWrite(LED, bright, bright / 2, 0); // Orange
    }
    if (bADXL) {
        adxlSetup();
        sensors_event_t event;
        adxl.getEvent(&event);
        Serial.println(event.acceleration.x);
        prints["adxl"] = String(event.acceleration.x) + ',' +
                         String(event.acceleration.y) + ',' +
                         String(event.acceleration.z);
    }
    if (bBoot0Change) {
        if (rf95Setup()) {
            byte buf[2];
            int sendSize = 2;
            buf[0] = highByte(2);
            buf[1] = lowByte(2);
            rf95.send((uint8_t *)buf, sendSize);
            rf95.waitPacketSent();
        }
        Serial.println("boot0");
    }

    if (bADXL || bLSM || bBoot0Change || bSick) {
        String stringPrints;
        serializeJson(prints, stringPrints);
        ws.textAll(stringPrints);
    }
    ws.cleanupClients();
}

void normalTask() {
    for (size_t i = 0; i < blink; i++) {
        rainbowLoop(10);
    }
}

long loopTO = 0;
long wsTO = 0;
long blinkTO = 0;
int wsDelay = 100;
bool taskDone = false;

void loop() {
    if (!chg && !taskDone) {
        normalTask();
        taskDone = true;
    }
    if (millis() > loopTO) {
        loopTO = manageLoop() * 1000 + millis();
    }
    if (millis() > wsTO) {
        loopWS();
        wsTO = millis() + wsDelay;
    }
    if (millis() > blinkTO) {
        loopBlink();
        blinkTO = millis() + 500;
        if (chg) {
            rtc.setAlarm1(rtc.now() + 10, DS3231_A1_Date);
            rtc.clearAlarm(1);
        }
    }
}
