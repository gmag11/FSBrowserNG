// FSWebServerLib.h

#ifndef _FSWEBSERVERLIB_h
#define _FSWEBSERVERLIB_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
#endif

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

#define CONFIG_FILE "/config.json"
#define SECRET_FILE "/secret.json"

typedef struct {
	String ssid;
	String password;
	IPAddress  ip;
	IPAddress  netmask;
	IPAddress  gateway;
	IPAddress  dns;
	bool dhcp;
	String ntpServerName;
	long updateNTPTimeEvery;
	long timezone;
	bool daylight;
	String deviceName;
} strConfig;

typedef struct {
	String APssid = "ESP"; // ChipID is appended to this name
	String APpassword = "12345678";
	bool APenable = false; // AP disabled by default
} strApConfig;

typedef struct {
	bool auth;
	String wwwUsername;
	String wwwPassword;
} strHTTPAuth;

class AsyncFSWebServer : AsyncWebServer {
public:
	AsyncFSWebServer(uint16_t port);
	void begin(FS* fs);
	void handle();
	bool checkAuth(AsyncWebServerRequest *request);
	void handleFileList(AsyncWebServerRequest *request);
	bool handleFileRead(String path, AsyncWebServerRequest *request);

protected:
	strConfig _config; // General and WiFi configuration
	strApConfig _apConfig; // Static AP config settings
	strHTTPAuth _httpAuth;
	FS* _fs;

	//uint currentWifiStatus;

	Ticker _secondTk;
	bool _secondFlag;

	AsyncWebSocket* _ws;

	void secondTask();
	void sendTimeData();
	bool load_config();
	void defaultConfig();
	bool save_config();
	bool loadHTTPAuth();
	void configureWifiAP();
	void configureWifi();
	void ConfigureOTA(String password);
	void serverInit();

	//void secondTick();
	
};

extern AsyncFSWebServer ESPHTTPServer;

#endif // _FSWEBSERVERLIB_h