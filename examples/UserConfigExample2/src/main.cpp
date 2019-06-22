#include "FSWebServerLib.h"
#include <ArduinoJson.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <NtpClientLib.h>

// pin used for entering setup mode
#define PIN_SETUP 5

unsigned long previousMillis = 0;
unsigned long interval = 10000;

void setup() {
    pinMode(PIN_SETUP, INPUT_PULLUP);
    Serial.begin(115200);
    // WiFi is started inside library
    SPIFFS.begin(); // Not really needed, checked inside library and started if
                    // needed
    ESPHTTPServer.begin(&SPIFFS);
    /* add setup code here */
}

void loop() {
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis > interval) {
        previousMillis = currentMillis;

        String T = "";
        int I = 0;
        float F = 0.0;

        ESPHTTPServer.load_user_config("value1", T);
        Serial.print("value1: ");
        Serial.println(T);

        ESPHTTPServer.load_user_config("value2", T);
        Serial.print("value2: ");
        Serial.println(T);

        ESPHTTPServer.load_user_config("value3", I);
        Serial.print("value3: ");
        Serial.println(I);

        ESPHTTPServer.load_user_config("value4", F);
        Serial.print("value4: ");
        Serial.println(F);

        ESPHTTPServer.load_user_config("value5", T);
        Serial.print("value5: ");
        Serial.println(T);
    }

    // DO NOT REMOVE. Attend OTA update from Arduino IDE
    ESPHTTPServer.handle();
}
