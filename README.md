# FSBrowserNG
Full autocontained (on SPIFFS) async web server on ESP8266. Written as a Library.

Same features as my FSBrowser sketch but built around AsyncWebServer library and implemented as a library itself. See *.ino file for usage example

## Introduction
I wanted to add a standard environment for all my ESP8266 based projects to be able to configure them via web browser to avoid code editing when I need to change some settings like SSID, password, etc.

I found [John Lassen's WebConfig project](http://www.john-lassen.de/index.php/projects/esp-8266-arduino-ide-webconfig) to almost fit my needs. It uses a simple but powerful web interface with dynamic data using [microAJAX](https://code.google.com/archive/p/microajax/). I've added some dynamic data using [Links2004](https://github.com/Links2004)'s [WebSockets library](https://github.com/Links2004/arduinoWebSockets).

So, I tried to fork it. Original WebConfig project stores web pages on PROGMEM, but I was recently using SPIFFS on ESP8266 and I think it is a good way to store web content. I found I was not the first to think this when I noticed about [FSBrowser](https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer/examples/FSBrowser) example in [ESP8266WebServer](https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer) library.

This example code has a great bonus: a text editor (based on [ACE](https://ace.c9.io/)) with syntax highligth that can be used to edit html on board directly. I found a [bug](https://github.com/esp8266/Arduino/pull/1771) on it (already sent to [esp8266/Arduino](https://github.com/esp8266/Arduino/) repository) and, when fixed, noticed how usefull this editor is.

My code is a fork of both projects, and has become a start framework for my future projects. Finally i've refactored code to use it as a library, so it is easier to integrate it in a project. See this [example](https://github.com/gmag11/FSBrowserNG/blob/master/FSBrowserNG.ino) to check that only 3 lines are necessary to use it.

I only have to add more HTML data and some funtions to show dynamic data in order to adapt it to any other task.

It has integrated NTP syncronization based on my own [NTPClient library](https://github.com/gmag11/NtpClient), that can be configured via this Web interface too.

Configuration is stored on SPIFFS too, in a JSON file. So, it recovers config during boot.

Implemented OTA Update with Arduino IDE and Web interface.

Web access can be protected with Basic HTTP Authentication.

I use a library called [**ESPAsyncWebServer**](https://github.com/me-no-dev/ESPAsyncWebServer). It is much faster than standard ESP8266WebServer library due to its async design.

#### Notice that Basic HTTP Authentication alone is not suitable to protect data over Internet. It allows recovery of user and password via sniffing and does not encrypt data flow. It is valid to give a first protection and may be a good solution combining it with strong VPN. Take this in account when connecting it to public networks or Internet.

## WiFi connection
I've implemented a way to turn ESP8266 into AP mode so I can set WiFi config when prior WiFi is not available. It is done by setting IO pin 4 to high status during boot. This pin is configurable by a `#define.` in [FSWebServerlib.h](https://github.com/gmag11/FSBrowserNG/blob/master/FSWebServerLib.h#L35). **You must ensure it is connected to GND to allow it to connect to a router**.

AP Mode is also started when `loadConfig()` finds no config file (config.json). This is so to help first use without needind to hardcode user and password. config.js is generated automatically if needed:

``` json
{
	"ssid":"YOUR_SSID",
	"pass":"YOUR_PASSWORD",
	"ip":[192,168,1,4],
	"netmask":[255,255,255,0],
	"gateway":[192,168,1,1],
	"dns":[192,168,1,1],
	"dhcp":1
	,"ntp":"es.pool.ntp.org"
	,"NTPperiod":5
	,"timeZone":10
	,"daylight":1
	,"deviceName":"ESP8266fs"
}
```

WiFi connection to router is confirmed via LED on IO pin GPIO02 as soon an IP address is got. Same LED is used to confirm AP mode by flashing 3 times. LED ping can be configured in [FSWebServerlib.h](https://github.com/gmag11/FSBrowserNG/blob/master/FSWebServerLib.h#L34).

## How to use it

After compile and flash into ESP8266 you need to upload SPIFFS data using [download tool](https://github.com/esp8266/arduino-esp8266fs-plugin) for Arduino IDE. Check [SPIFFS doc](https://github.com/esp8266/Arduino/blob/master/doc/filesystem.md) for details.

* `http://ip_address/admin.html` takes you to main configuration GUI.
* `http://ip_address/edit` contains code editor. Any file stored in SPIFFS may be loaded here and saved using `CTRL+S` command.
* `http://ip_address/update` allows remote update through WEB.
* `http://ip_address/` includes an example info page that shows how to get realtime data from ESP. You may change this to adapt to your project.

You may add your own files to SPIFFS to be served by ESP8266. You only need to add some code if you need dynamic data.

FSBrowserNG extends AsyncWebServer class. So, you may use all its methods as usual.

## Dependances

This library makes use of other libraries you have to include in your Arduino library folder.

- `TimeLib.h` Time library by **Paul Stoffregen** https://github.com/PaulStoffregen/Time
- `NtpClientLib.h` NTP Client library by **Germ&aacute;n Mart&iacute;n** https://github.com/gmag11/NtpClient
- `ESPAsyncTCP.h` Async TCP library by **Me No Dev** https://github.com/me-no-dev/ESPAsyncTCP
- `ESPAsyncWebServer.h` HTTP Async Web Server library by **Me No Dev** https://github.com/me-no-dev/ESPAsyncTCP
- `ArduinoJson` JSON library by **Benoît Blanchon** https://github.com/bblanchon/ArduinoJson

All other libraries used are installed in Arduino ESP8266 framework.

## Troubleshooting

If the compiler exits with an error similar to "WebHandlers.cpp:67:64: error: 'strftime' was not declared in this scope", follow these steps:

* Locate  folder `/Library/Arduino15/packages/esp8266/tools/xtensa-lx106-elf-gcc/1.20.0-26-gb404fb9-2/xtensa-lx106-elf/include` 
* make a copy of `time.h` and name it  `_time.h`
* Locate and open `ESPAsyncWebServer\src\WebHandlerImpl.h`
* Replace `#include <time.h>` to `#include <_time.h>`

see https://github.com/me-no-dev/ESPAsyncWebServer/issues/60

If the ESP8266 Sketch Data Upload fails with an error similar to "Exception in thread "AWT-EventQueue-0" java.lang.IllegalAccessError: tried to access method", replace the [ESP8266FS 0.3.0](https://github.com/esp8266/arduino-esp8266fs-plugin/releases/download/0.3.0/ESP8266FS-0.3.0.zip) plugin by the [ESP8266FS 0.2.0](https://github.com/esp8266/arduino-esp8266fs-plugin/releases/download/0.2.0/ESP8266FS-0.2.0.zip) plugin.


## TODO

- ~~HTTP Authentication~~ HTTP Basic authentication implemented. Will try to improve to a more secure mechanism.
- ~~OTA update via web interface~~ MD5 cheching added.
- ~~MD5 check of uploaded firmware~~
- ~~Configuration protection~~
- ~~HTTPS (in evaluation).~~ *Not possible with ESP8266*
- ~~Integration of editor in admin.html~~
- ~~Convert code to classes and try to design a library to add all this functionality easily.~~
- 2-Step authentication
