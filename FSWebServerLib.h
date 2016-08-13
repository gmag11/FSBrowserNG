// FSWebServerLib.h

#ifndef _FSWEBSERVERLIB_h
#define _FSWEBSERVERLIB_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
#endif

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <Ticker.h>
#include <ArduinoOTA.h>

class AsyncFSWebServer : AsyncWebServer {
	AsyncFSWebServer(uint16_t port);

	void begin();
};

#endif

