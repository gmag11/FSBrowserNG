///*
// Name:		UserConfigExample.ino
// Created:	9/24/2017 10:39:12 AM
// Author:	Lee
//*/


#include <ESP8266WiFi.h>
#include "FS.h"
#include <WiFiClient.h>
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266mDNS.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "FSWebServerLib.h"
#include <Hash.h>

long previousMillis = 0;        // will store last time LED was updated

								// the follow variables is a long because the time, measured in miliseconds,
								// will quickly become a bigger number than can be stored in an int.
long interval = 10000;           // interval at which to blink (milliseconds)


void setup() {
	// WiFi is started inside library
	SPIFFS.begin(); // Not really needed, checked inside library and started if needed
	ESPHTTPServer.begin(&SPIFFS);
	/* add setup code here */



}

void loop() {
	/* add main program code here */

	// DO NOT REMOVE. Attend OTA update from Arduino IDE
	ESPHTTPServer.handle();


	unsigned long currentMillis = millis();

	if (currentMillis - previousMillis > interval) 
	{
		// save the last time you blinked the LED 
		previousMillis = currentMillis;

//			ESPHTTPServer.save_user_config("Test", "Test");
			String T = "";
			ESPHTTPServer.load_user_config("Test", T);
			Serial.print("S: ");
			Serial.println(T);
		
			//ESPHTTPServer.save_user_config("TestI", 10);
			int I = 0;
			ESPHTTPServer.load_user_config("TestI", I);
			Serial.print("I: ");
			Serial.println(I);

			//ESPHTTPServer.save_user_config("TestF", 10);
			float F = 0.0;
			ESPHTTPServer.load_user_config("TestF", F);
			Serial.print("F: ");
			Serial.println(F);


	}
	
}
