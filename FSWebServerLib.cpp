// 
// 
// 

#define DEBUG

#define DBG_OUTPUT_PORT Serial
#define CONNECTION_LED 2 // Connection LED pin (Built in)
#define AP_ENABLE_BUTTON 4 // Button pin to enable AP during startup for configuration

#include "FSWebServerLib.h"
#include <StreamString.h>

AsyncFSWebServer ESPHTTPServer(80);

const char Page_WaitAndReload[] PROGMEM = R"=====(
<meta http-equiv="refresh" content="10; URL=/config.html">
Please Wait....Configuring and Restarting.
)=====";

const char Page_Restart[] PROGMEM = R"=====(
<meta http-equiv="refresh" content="10; URL=/general.html">
Please Wait....Configuring and Restarting.
)=====";

AsyncFSWebServer::AsyncFSWebServer(uint16_t port) : AsyncWebServer(port)
{
	
}

void AsyncFSWebServer::secondTick()
{
	_secondFlag = true;
}

void AsyncFSWebServer::secondTask() {
#ifdef DEBUG_GLOBALH
	//DBG_OUTPUT_PORT.println(ntp->getTimeString());
#endif // DEBUG_GLOBALH
	sendTimeData();
}

void AsyncFSWebServer::s_secondTick(void* arg) {
	AsyncFSWebServer* self = reinterpret_cast<AsyncFSWebServer*>(arg);
	self->secondTask();
}

void AsyncFSWebServer::sendTimeData() {
	String time = "T" + NTP.getTimeStr();
	_ws->textAll(time);
	String date = "D" + NTP.getDateStr();
	_ws->textAll(date);
	String sync = "S" + NTP.getTimeDateString(NTP.getLastNTPSync());
	_ws->textAll(sync);
#ifdef DEBUG_DYNAMICDATA
	//DBG_OUTPUT_PORT.println(__PRETTY_FUNCTION__);
#endif // DEBUG_DYNAMICDATA
}

String formatBytes(size_t bytes) {
	if (bytes < 1024) {
		return String(bytes) + "B";
	}
	else if (bytes < (1024 * 1024)) {
		return String(bytes / 1024.0) + "KB";
	}
	else if (bytes < (1024 * 1024 * 1024)) {
		return String(bytes / 1024.0 / 1024.0) + "MB";
	}
	else {
		return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
	}
}

void AsyncFSWebServer::begin(FS* fs)
{
	_fs = fs;
	DBG_OUTPUT_PORT.begin(115200);
	DBG_OUTPUT_PORT.print("\n\n");
#ifdef DEBUG
	DBG_OUTPUT_PORT.setDebugOutput(true);
#endif // DEBUG
	if (CONNECTION_LED >= 0) {
		pinMode(CONNECTION_LED, OUTPUT); // CONNECTION_LED pin defined as output
	}
	if (AP_ENABLE_BUTTON >= 0) {
		pinMode(AP_ENABLE_BUTTON, INPUT); // If this pin is HIGH during startup ESP will run in AP_ONLY mode. Backdoor to change WiFi settings when configured WiFi is not available.
	}
	//analogWriteFreq(200);

	if (AP_ENABLE_BUTTON >= 0) {
		_apConfig.APenable = digitalRead(AP_ENABLE_BUTTON); // Read AP button
#ifdef DEBUG
		DBG_OUTPUT_PORT.printf("AP Enable = %d\n", _apConfig.APenable);
#endif // DEBUG
	}
	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, HIGH); // Turn LED off
	}
	if (!_fs) // If SPIFFS is not started
		_fs->begin();

#ifdef DEBUG
	{ // List files
		Dir dir = _fs->openDir("/");
		while (dir.next()) {
			String fileName = dir.fileName();
			size_t fileSize = dir.fileSize();

			DBG_OUTPUT_PORT.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
		}
		DBG_OUTPUT_PORT.printf("\n");
	}
#endif // DEBUG
	if (!load_config()) { // Try to load configuration from file system
		defaultConfig(); // Load defaults if any error
		_apConfig.APenable = true;
	}
	loadHTTPAuth();
	//WIFI INIT

	// Register wifi Event to control connection LED
	WiFi.onStationModeGotIP(std::bind(&AsyncFSWebServer::onWiFiGotIp, this, _1));
	WiFi.onStationModeDisconnected(std::bind(&AsyncFSWebServer::onWiFiDisconnected, this, _1));

	WiFi.hostname(_config.deviceName.c_str());
	if (AP_ENABLE_BUTTON >= 0) {
		if (_apConfig.APenable) {
			configureWifiAP(); // Set AP mode if AP button was pressed
		}
		else {
			configureWifi(); // Set WiFi config
		}
	}
	else {
		configureWifi(); // Set WiFi config
	}

#ifdef DEBUG
	DBG_OUTPUT_PORT.print("Open http://");
	DBG_OUTPUT_PORT.print(_config.deviceName);
	DBG_OUTPUT_PORT.println(".local/edit to see the file browser");
	DBG_OUTPUT_PORT.printf("Flash chip size: %u\n", ESP.getFlashChipRealSize());
	DBG_OUTPUT_PORT.printf("Scketch size: %u\n", ESP.getSketchSize());
	DBG_OUTPUT_PORT.printf("Free flash space: %u\n", ESP.getFreeSketchSpace());
#endif
	// NTP client setup
	if (_config.updateNTPTimeEvery > 0) { // Enable NTP sync
		NTP.setInterval(15, _config.updateNTPTimeEvery * 60);
		NTP.begin(_config.ntpServerName, _config.timezone / 10, _config.daylight);
	}

	_secondTk.attach(1.0f, &AsyncFSWebServer::s_secondTick, static_cast<void*>(this)); // Task to run periodic things every second
	*_ws = AsyncWebSocket("/ws");
	serverInit(); // Configure and start Web server
	AsyncWebServer::begin();

	MDNS.begin(_config.deviceName.c_str()); // I've not got this to work. Need some investigation.
	MDNS.addService("http", "tcp", 80);
	ConfigureOTA(_httpAuth.wwwPassword.c_str());

#ifdef DEBUG
	DBG_OUTPUT_PORT.println("END Setup");
#endif // DEBUG


}

bool AsyncFSWebServer::load_config() {
	File configFile =  _fs->open(CONFIG_FILE, "r");
	if (!configFile) {
#ifdef DEBUG
		DBG_OUTPUT_PORT.println("Failed to open config file");
#endif // DEBUG
		return false;
	}

	size_t size = configFile.size();
	/*if (size > 1024) {
#ifdef DEBUG
		DBG_OUTPUT_PORT.println("Config file size is too large");
#endif
		configFile.close();
		return false;
	}*/

	// Allocate a buffer to store contents of the file.
	std::unique_ptr<char[]> buf(new char[size]);

	// We don't use String here because ArduinoJson library requires the input
	// buffer to be mutable. If you don't use ArduinoJson, you may as well
	// use configFile.readString instead.
	configFile.readBytes(buf.get(), size);
	configFile.close();
#ifdef DEBUG
	DBG_OUTPUT_PORT.print("JSON file size: "); Serial.print(size); Serial.println(" bytes");
#endif
	DynamicJsonBuffer jsonBuffer(1024);
	//StaticJsonBuffer<1024> jsonBuffer;
	JsonObject& json = jsonBuffer.parseObject(buf.get());

	if (!json.success()) {
#ifdef DEBUG
		DBG_OUTPUT_PORT.println("Failed to parse config file");
#endif // DEBUG
		return false;
	}
#ifdef DEBUG
	String temp;
	json.prettyPrintTo(temp);
	Serial.println(temp);
#endif
	//memset(config.ssid, 0, 28);
	//memset(config.pass, 0, 50);
	//String("Virus_Detected!!!").toCharArray(config.ssid, 28); // Assign WiFi SSID
	//String("LaJunglaSigloXX1@.").toCharArray(config.pass, 50); // Assign WiFi PASS

	_config.ssid = json["ssid"].as<const char *>();

	_config.password = json["pass"].as<const char *>();

	_config.ip = IPAddress(json["ip"][0], json["ip"][1], json["ip"][2], json["ip"][3]);
	_config.netmask = IPAddress(json["netmask"][0], json["netmask"][1], json["netmask"][2], json["netmask"][3]);
	_config.gateway = IPAddress(json["gateway"][0], json["gateway"][1], json["gateway"][2], json["gateway"][3]);
	_config.dns = IPAddress(json["dns"][0], json["dns"][1], json["dns"][2], json["dns"][3]);

	_config.dhcp = json["dhcp"].as<bool>();

	//String(pass_str).toCharArray(config.pass, 28);
	_config.ntpServerName = json["ntp"].as<const char *>();
	_config.updateNTPTimeEvery = json["NTPperiod"].as<long>();
	_config.timezone = json["timeZone"].as<long>();
	_config.daylight = json["daylight"].as<long>();
	_config.deviceName = json["deviceName"].as<const char *>();

	//config.connectionLed = json["led"];

#ifdef DEBUG
	DBG_OUTPUT_PORT.println("Data initialized.");
	DBG_OUTPUT_PORT.print("SSID: "); Serial.println(_config.ssid);
	DBG_OUTPUT_PORT.print("PASS: "); Serial.println(_config.password);
	DBG_OUTPUT_PORT.print("NTP Server: "); Serial.println(_config.ntpServerName);
	//DBG_OUTPUT_PORT.printf("Connection LED: %d\n", config.connectionLed);
	DBG_OUTPUT_PORT.println(__PRETTY_FUNCTION__);
#endif // DEBUG

	return true;
}

void AsyncFSWebServer::defaultConfig() {
	// DEFAULT CONFIG
	_config.ssid = "YOUR_DEFAULT_WIFI_SSID";
	_config.password = "YOUR_DEFAULT_WIFI_PASSWD";
	_config.dhcp = true;
	_config.ip = IPAddress(192, 168, 1, 4);
	_config.netmask = IPAddress(255, 255, 255, 0);
	_config.gateway = IPAddress(192, 168, 1, 1);
	_config.dns = IPAddress(192, 168, 1, 1);
	_config.ntpServerName = "pool.ntp.org";
	_config.updateNTPTimeEvery = 15;
	_config.timezone = 10;
	_config.daylight = true;
	_config.deviceName = "ESP8266fs";
	//config.connectionLed = CONNECTION_LED;
	save_config();
#ifdef DEBUG
	DBG_OUTPUT_PORT.println(__PRETTY_FUNCTION__);
#endif // DEBUG
}

bool AsyncFSWebServer::save_config() {
	//flag_config = false;
#ifdef DEBUG
	DBG_OUTPUT_PORT.println("Save config");
#endif
	DynamicJsonBuffer jsonBuffer(1024);
	//StaticJsonBuffer<1024> jsonBuffer;
	JsonObject& json = jsonBuffer.createObject();
	json["ssid"] = _config.ssid;
	json["pass"] = _config.password;

	JsonArray& jsonip = json.createNestedArray("ip");
	jsonip.add(_config.ip[0]);
	jsonip.add(_config.ip[1]);
	jsonip.add(_config.ip[2]);
	jsonip.add(_config.ip[3]);

	JsonArray& jsonNM = json.createNestedArray("netmask");
	jsonNM.add(_config.netmask[0]);
	jsonNM.add(_config.netmask[1]);
	jsonNM.add(_config.netmask[2]);
	jsonNM.add(_config.netmask[3]);

	JsonArray& jsonGateway = json.createNestedArray("gateway");
	jsonGateway.add(_config.gateway[0]);
	jsonGateway.add(_config.gateway[1]);
	jsonGateway.add(_config.gateway[2]);
	jsonGateway.add(_config.gateway[3]);

	JsonArray& jsondns = json.createNestedArray("dns");
	jsondns.add(_config.dns[0]);
	jsondns.add(_config.dns[1]);
	jsondns.add(_config.dns[2]);
	jsondns.add(_config.dns[3]);

	json["dhcp"] = _config.dhcp;
	json["ntp"] = _config.ntpServerName;
	json["NTPperiod"] = _config.updateNTPTimeEvery;
	json["timeZone"] = _config.timezone;
	json["daylight"] = _config.daylight;
	json["deviceName"] = _config.deviceName;

	//json["led"] = config.connectionLed;

	//TODO add AP data to html
	File configFile = _fs->open(CONFIG_FILE, "w");
	if (!configFile) {
#ifdef DEBUG
		DBG_OUTPUT_PORT.println("Failed to open config file for writing");
#endif // DEBUG
		configFile.close();
		return false;
	}

#ifdef DEBUG
	String temp;
	json.prettyPrintTo(temp);
	Serial.println(temp);
#endif

	json.printTo(configFile);
	configFile.flush();
	configFile.close();
	return true;
}

bool AsyncFSWebServer::loadHTTPAuth() {
	File configFile = _fs->open(SECRET_FILE, "r");
	if (!configFile) {
#ifdef DEBUG
		DBG_OUTPUT_PORT.println("Failed to open secret file");
#endif // DEBUG
		_httpAuth.auth = false;
		_httpAuth.wwwUsername = "";
		_httpAuth.wwwPassword = "";
		configFile.close();
		return false;
	}

	size_t size = configFile.size();
	/*if (size > 256) {
#ifdef DEBUG
		DBG_OUTPUT_PORT.println("Secret file size is too large");
#endif
		httpAuth.auth = false;
		configFile.close();
		return false;
	}*/

	// Allocate a buffer to store contents of the file.
	std::unique_ptr<char[]> buf(new char[size]);

	// We don't use String here because ArduinoJson library requires the input
	// buffer to be mutable. If you don't use ArduinoJson, you may as well
	// use configFile.readString instead.
	configFile.readBytes(buf.get(), size);
	configFile.close();
#ifdef DEBUG
	DBG_OUTPUT_PORT.printf("JSON secret file size: %d %s\n", size, "bytes");
#endif
	DynamicJsonBuffer jsonBuffer(256);
	//StaticJsonBuffer<256> jsonBuffer;
	JsonObject& json = jsonBuffer.parseObject(buf.get());

	if (!json.success()) {
#ifdef DEBUG
		String temp;
		json.prettyPrintTo(temp);
		DBG_OUTPUT_PORT.println(temp);
		DBG_OUTPUT_PORT.println("Failed to parse secret file");
#endif // DEBUG
		_httpAuth.auth = false;
		return false;
	}
#ifdef DEBUG
	String temp;
	json.prettyPrintTo(temp);
	DBG_OUTPUT_PORT.println(temp);
#endif
	//memset(config.ssid, 0, 28);
	//memset(config.pass, 0, 50);
	//String("Virus_Detected!!!").toCharArray(config.ssid, 28); // Assign WiFi SSID
	//String("LaJunglaSigloXX1@.").toCharArray(config.pass, 50); // Assign WiFi PASS

	_httpAuth.auth = json["auth"];
	_httpAuth.wwwUsername = json["user"].asString();
	_httpAuth.wwwPassword = json["pass"].asString();

#ifdef DEBUG
	DBG_OUTPUT_PORT.println(_httpAuth.auth ? "Secret initialized." : "Auth disabled.");
	DBG_OUTPUT_PORT.print("User: "); DBG_OUTPUT_PORT.println(_httpAuth.wwwUsername);
	DBG_OUTPUT_PORT.print("Pass: "); DBG_OUTPUT_PORT.println(_httpAuth.wwwPassword);
	DBG_OUTPUT_PORT.println(__PRETTY_FUNCTION__);
#endif // DEBUG
	return true;
}



void AsyncFSWebServer::handle()
{
	if (_secondFlag) { // Run periodic tasks
		_secondFlag = false;
		secondTask();
	}
	ArduinoOTA.handle();
}

void flashLED(int pin, int times, int delayTime) {
	int oldState = digitalRead(pin);

	for (int i = 0; i < times; i++) {
		digitalWrite(pin, LOW); // Turn on LED
		delay(delayTime);
		digitalWrite(pin, HIGH); // Turn on LED
		delay(delayTime);
	}
	digitalWrite(pin, oldState); // Turn on LED
}

void AsyncFSWebServer::configureWifiAP() {
#ifdef DEBUG_GLOBALH
	DBG_OUTPUT_PORT.println(__PRETTY_FUNCTION__);
#endif // DEBUG_GLOBALH
	//WiFi.disconnect();
	WiFi.mode(WIFI_AP);
	String APname = _apConfig.APssid + (String)ESP.getChipId();
	//APname += (String)ESP.getChipId();
	//WiFi.softAP(APname.c_str(), apConfig.APpassword.c_str());
	if (_httpAuth.auth)
		WiFi.softAP(APname.c_str(), _httpAuth.wwwPassword.c_str());
	else
		WiFi.softAP(APname.c_str());
	if (CONNECTION_LED >= 0) {
		flashLED(CONNECTION_LED, 3, 250);
	}
}

void AsyncFSWebServer::configureWifi()
{
	WiFi.mode(WIFI_STA);
	//currentWifiStatus = WIFI_STA_DISCONNECTED;
#ifdef DEBUG
	DBG_OUTPUT_PORT.printf("Connecting to %s\n", _config.ssid.c_str());
#endif // DEBUG
	WiFi.begin(_config.ssid.c_str(), _config.password.c_str());
	if (!_config.dhcp)
	{
#ifdef DEBUG_GLOBALH
		DBG_OUTPUT_PORT.println("NO DHCP");
#endif // DEBUG_GLOBALH
		WiFi.config(_config.ip, _config.gateway, _config.netmask, _config.dns);
	}
	//delay(2000);
	//delay(5000); // Wait for WiFi
	
	while (!WL_CONNECTED) {
		delay(1000);
		Serial.print(".");
	}
	/*if (WiFi.isConnected()) {
		currentWifiStatus = WIFI_STA_CONNECTED;
	}*/

	DBG_OUTPUT_PORT.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
#ifdef DEBUG
	DBG_OUTPUT_PORT.printf("Gateway:    %s\n", WiFi.gatewayIP().toString().c_str());
	DBG_OUTPUT_PORT.printf("DNS:        %s\n", WiFi.dnsIP().toString().c_str());
	Serial.println(__PRETTY_FUNCTION__);
#endif // DEBUG
}

void AsyncFSWebServer::ConfigureOTA(String password) {
	// Port defaults to 8266
	// ArduinoOTA.setPort(8266);

	// Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(_config.deviceName.c_str());

	// No authentication by default
	if (password != "") {
		ArduinoOTA.setPassword(password.c_str());
#ifdef DEBUG
		DBG_OUTPUT_PORT.printf("OTA password set %s\n", password.c_str());
#endif // DEBUG
	}

#ifdef DEBUG
	ArduinoOTA.onStart([]() {
		DBG_OUTPUT_PORT.println("StartOTA \n");
	});
	ArduinoOTA.onEnd(std::bind([](FS *fs) {
		fs->end();
		DBG_OUTPUT_PORT.println("\nEnd OTA\n");
	}, _fs));
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		DBG_OUTPUT_PORT.printf("OTA Progress: %u%%\n", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		DBG_OUTPUT_PORT.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) DBG_OUTPUT_PORT.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) DBG_OUTPUT_PORT.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) DBG_OUTPUT_PORT.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) DBG_OUTPUT_PORT.println("Receive Failed");
		else if (error == OTA_END_ERROR) DBG_OUTPUT_PORT.println("End Failed");
	});
	DBG_OUTPUT_PORT.println("\nOTA Ready");
#endif // DEBUG
	ArduinoOTA.begin();
}

void AsyncFSWebServer::onWiFiGotIp(WiFiEventStationModeGotIP data) {
	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, LOW); // Turn LED on
	}
	//DBG_OUTPUT_PORT.printf("Led %s on\n", CONNECTION_LED);
	//turnLedOn();
	wifiDisconnectedSince = 0;
}

void AsyncFSWebServer::onWiFiDisconnected(WiFiEventStationModeDisconnected data) {
#ifdef DEBUG_GLOBALH
	DBG_OUTPUT_PORT.println("case STA_DISCONNECTED");
#endif // DEBUG_GLOBALH
	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, HIGH); // Turn LED off
	}
	//DBG_OUTPUT_PORT.printf("Led %s off\n", CONNECTION_LED);
	//flashLED(config.connectionLed, 2, 100);
	if (wifiDisconnectedSince == 0) {
		wifiDisconnectedSince = millis();
	}
#ifdef DEBUG
	DBG_OUTPUT_PORT.printf("Disconnected for %d seconds\n", (int)((millis() - wifiDisconnectedSince) / 1000));
#endif // DEBUG
}

/*
void WiFiEvent(WiFiEvent_t event) {
	static long wifiDisconnectedSince = 0;

	switch (event) {
	case WIFI_EVENT_STAMODE_GOT_IP:
		//DBG_OUTPUT_PORT.println(event);
		if (CONNECTION_LED >= 0) {
			digitalWrite(CONNECTION_LED, LOW); // Turn LED on
		}
		//DBG_OUTPUT_PORT.printf("Led %s on\n", CONNECTION_LED);
		//turnLedOn();
		wifiDisconnectedSince = 0;
		//currentWifiStatus = WIFI_STA_CONNECTED;
		break;
	case WIFI_EVENT_STAMODE_DISCONNECTED:
#ifdef DEBUG_GLOBALH
		DBG_OUTPUT_PORT.println("case STA_DISCONNECTED");
#endif // DEBUG_GLOBALH
		if (CONNECTION_LED >= 0) {
			digitalWrite(CONNECTION_LED, HIGH); // Turn LED off
		}
		//DBG_OUTPUT_PORT.printf("Led %s off\n", CONNECTION_LED);
		//flashLED(config.connectionLed, 2, 100);
		if (wifiDisconnectedSince == 0) {
			wifiDisconnectedSince = millis();
		}
#ifdef DEBUG
		DBG_OUTPUT_PORT.printf("Disconnected for %d seconds\n", (int)((millis() - wifiDisconnectedSince) / 1000));
#endif // DEBUG
	}
}*/

void AsyncFSWebServer::handleFileList(AsyncWebServerRequest *request) {
	//if (!checkAuth(request))
		//return request->requestAuthentication();
	if (!request->hasArg("dir")) { request->send(500, "text/plain", "BAD ARGS"); return; }

	String path = request->arg("dir");
#ifdef DEBUG_WEBSERVER
	DBG_OUTPUT_PORT.println("handleFileList: " + path);
#endif // DEBUG_WEBSERVER
	Dir dir = _fs->openDir(path);
	path = String();

	String output = "[";
	while (dir.next()) {
		File entry = dir.openFile("r");
		if (true)//entry.name()!="secret.json") // Do not show secrets
		{
			if (output != "[")
				output += ',';
			bool isDir = false;
			output += "{\"type\":\"";
			output += (isDir) ? "dir" : "file";
			output += "\",\"name\":\"";
			output += String(entry.name()).substring(1);
			output += "\"}";
		}
		entry.close();
	}

	output += "]";
#ifdef DEBUG_WEBSERVER
	DBG_OUTPUT_PORT.println(output);
#endif // DEBUG_WEBSERVER
	request->send(200, "text/json", output);
}

String getContentType(String filename, AsyncWebServerRequest *request) {
	if (request->hasArg("download")) return "application/octet-stream";
	else if (filename.endsWith(".htm")) return "text/html";
	else if (filename.endsWith(".html")) return "text/html";
	else if (filename.endsWith(".css")) return "text/css";
	else if (filename.endsWith(".js")) return "application/javascript";
	else if (filename.endsWith(".json")) return "application/json";
	else if (filename.endsWith(".png")) return "image/png";
	else if (filename.endsWith(".gif")) return "image/gif";
	else if (filename.endsWith(".jpg")) return "image/jpeg";
	else if (filename.endsWith(".ico")) return "image/x-icon";
	else if (filename.endsWith(".xml")) return "text/xml";
	else if (filename.endsWith(".pdf")) return "application/x-pdf";
	else if (filename.endsWith(".zip")) return "application/x-zip";
	else if (filename.endsWith(".gz")) return "application/x-gzip";
	return "text/plain";
}

/*
void AsyncFSWebServer::handleFileRead_edit_html(AsyncWebServerRequest *request) {
	if (!checkAuth(request))
		return request->requestAuthentication();
	handleFileRead(request, "/edit.html");
}
*/

bool AsyncFSWebServer::handleFileRead(AsyncWebServerRequest *request, String path) {
#ifdef DEBUG_WEBSERVER
	DBG_OUTPUT_PORT.println("handleFileRead: " + path);
#endif // DEBUG_WEBSERVER
	if (CONNECTION_LED >= 0) {
		// CANNOT RUN DELAY() INSIDE CALLBACK
		//flashLED(CONNECTION_LED, 1, 25); // Show activity on LED
	}
	if (path.endsWith("/"))
		path += "index.htm";
	String contentType = getContentType(path, request);
	String pathWithGz = path + ".gz";
	if (_fs->exists(pathWithGz) || _fs->exists(path)) {
		if (_fs->exists(pathWithGz)) {
			path += ".gz";
		}
#ifdef DEBUG_WEBSERVER
		DBG_OUTPUT_PORT.printf("Content type: %s\r\n", contentType.c_str());
#endif // DEBUG_WEBSERVER
		AsyncWebServerResponse *response = request->beginResponse(*_fs, path, contentType);
		if (path.endsWith(".gz"))
			response->addHeader("Content-Encoding", "gzip");
		//File file = SPIFFS.open(path, "r");
#ifdef DEBUG_WEBSERVER
		DBG_OUTPUT_PORT.printf("File %s exist\n", path.c_str());
#endif // DEBUG_WEBSERVER
		request->send(response);

		return true;
	}
#ifdef DEBUG_WEBSERVER
	else
		DBG_OUTPUT_PORT.printf("Cannot find %s\n", path.c_str());
#endif // DEBUG_WEBSERVER
	return false;
}

void AsyncFSWebServer::handleFileCreate(AsyncWebServerRequest *request) {
	if (!checkAuth(request))
		return request->requestAuthentication();
	if (request->args() == 0)
		return request->send(500, "text/plain", "BAD ARGS");
	String path = request->arg(0);
#ifdef DEBUG_WEBSERVER
	DBG_OUTPUT_PORT.println("handleFileCreate: " + path);
#endif // DEBUG_WEBSERVER
	if (path == "/")
		return request->send(500, "text/plain", "BAD PATH");
	if (_fs->exists(path))
		return request->send(500, "text/plain", "FILE EXISTS");
	File file = _fs->open(path, "w");
	if (file)
		file.close();
	else
		return request->send(500, "text/plain", "CREATE FAILED");
	request->send(200, "text/plain", "");
	path = String(); // Remove? Useless statement?
}

void AsyncFSWebServer::handleFileDelete(AsyncWebServerRequest *request) {
	if (!checkAuth(request))
		return request->requestAuthentication();
	if (request->args() == 0) return request->send(500, "text/plain", "BAD ARGS");
	String path = request->arg(0);
#ifdef DEBUG_WEBSERVER
	DBG_OUTPUT_PORT.println("handleFileDelete: " + path);
#endif // DEBUG_WEBSERVER
	if (path == "/")
		return request->send(500, "text/plain", "BAD PATH");
	if (!_fs->exists(path))
		return request->send(404, "text/plain", "FileNotFound");
	_fs->remove(path);
	request->send(200, "text/plain", "");
	path = String(); // Remove? Useless statement?
}

void AsyncFSWebServer::handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	File fsUploadFile;
	
	//if (request->url() != "/edit") return;
	if (!index) { // Start
#ifdef DEBUG_WEBSERVER
		DBG_OUTPUT_PORT.printf("handleFileUpload Name: %s\n", filename.c_str());
#endif // DEBUG_WEBSERVER
		if (!filename.startsWith("/")) filename = "/" + filename;
		File fsUploadFile = _fs->open(filename, "w");

	}
	// Continue
	if (fsUploadFile) {
		if (fsUploadFile.write(data, len) != len) {
			DBG_OUTPUT_PORT.println("Write error during upload");
		}
	}
	/*for (size_t i = 0; i < len; i++) {
	if (fsUploadFile)
	fsUploadFile.write(data[i]);
	}*/
	if (final) { // End
		if (fsUploadFile)
			fsUploadFile.close();
#ifdef DEBUG_WEBSERVER
		DBG_OUTPUT_PORT.printf("handleFileUpload Size: %u\n", len);
#endif // DEBUG_WEBSERVER

	}
}

void AsyncFSWebServer::send_general_configuration_values_html(AsyncWebServerRequest *request)
{
	String values = "";
	values += "devicename|" + (String)_config.deviceName + "|input\n";
	request->send(200, "text/plain", values);
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__FUNCTION__);
#endif // DEBUG_DYNAMICDATA
}

void AsyncFSWebServer::send_network_configuration_values_html(AsyncWebServerRequest *request)
{

	String values = "";

	values += "ssid|" + (String)_config.ssid + "|input\n";
	values += "password|" + (String)_config.password + "|input\n";
	values += "ip_0|" + (String)_config.ip[0] + "|input\n";
	values += "ip_1|" + (String)_config.ip[1] + "|input\n";
	values += "ip_2|" + (String)_config.ip[2] + "|input\n";
	values += "ip_3|" + (String)_config.ip[3] + "|input\n";
	values += "nm_0|" + (String)_config.netmask[0] + "|input\n";
	values += "nm_1|" + (String)_config.netmask[1] + "|input\n";
	values += "nm_2|" + (String)_config.netmask[2] + "|input\n";
	values += "nm_3|" + (String)_config.netmask[3] + "|input\n";
	values += "gw_0|" + (String)_config.gateway[0] + "|input\n";
	values += "gw_1|" + (String)_config.gateway[1] + "|input\n";
	values += "gw_2|" + (String)_config.gateway[2] + "|input\n";
	values += "gw_3|" + (String)_config.gateway[3] + "|input\n";
	values += "dns_0|" + (String)_config.dns[0] + "|input\n";
	values += "dns_1|" + (String)_config.dns[1] + "|input\n";
	values += "dns_2|" + (String)_config.dns[2] + "|input\n";
	values += "dns_3|" + (String)_config.dns[3] + "|input\n";
	values += "dhcp|" + (String)(_config.dhcp ? "checked" : "") + "|chk\n";

	request->send(200, "text/plain", values);
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__PRETTY_FUNCTION__);
#endif // DEBUG_DYNAMICDATA
}

void AsyncFSWebServer::send_connection_state_values_html(AsyncWebServerRequest *request)
{

	String state = "N/A";
	String Networks = "";
	if (WiFi.status() == 0) state = "Idle";
	else if (WiFi.status() == 1) state = "NO SSID AVAILBLE";
	else if (WiFi.status() == 2) state = "SCAN COMPLETED";
	else if (WiFi.status() == 3) state = "CONNECTED";
	else if (WiFi.status() == 4) state = "CONNECT FAILED";
	else if (WiFi.status() == 5) state = "CONNECTION LOST";
	else if (WiFi.status() == 6) state = "DISCONNECTED";



	int n = WiFi.scanNetworks();

	if (n == 0)
	{
		Networks = "<font color='#FF0000'>No networks found!</font>";
	}
	else
	{


		Networks = "Found " + String(n) + " Networks<br>";
		Networks += "<table border='0' cellspacing='0' cellpadding='3'>";
		Networks += "<tr bgcolor='#DDDDDD' ><td><strong>Name</strong></td><td><strong>Quality</strong></td><td><strong>Enc</strong></td><tr>";
		for (int i = 0; i < n; ++i)
		{
			int quality = 0;
			if (WiFi.RSSI(i) <= -100)
			{
				quality = 0;
			}
			else if (WiFi.RSSI(i) >= -50)
			{
				quality = 100;
			}
			else
			{
				quality = 2 * (WiFi.RSSI(i) + 100);
			}


			Networks += "<tr><td><a href='javascript:selssid(\"" + String(WiFi.SSID(i)) + "\")'>" + String(WiFi.SSID(i)) + "</a></td><td>" + String(quality) + "%</td><td>" + String((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*") + "</td></tr>";
		}
		Networks += "</table>";
	}

	String values = "";
	values += "connectionstate|" + state + "|div\n";
	values += "networks|" + Networks + "|div\n";
	request->send(200, "text/plain", values);
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__FUNCTION__);
#endif // DEBUG_DYNAMICDATA

}

void AsyncFSWebServer::send_information_values_html(AsyncWebServerRequest *request)
{

	String values = "";

	values += "x_ssid|" + (String)WiFi.SSID() + "|div\n";
	values += "x_ip|" + (String)WiFi.localIP()[0] + "." + (String)WiFi.localIP()[1] + "." + (String)WiFi.localIP()[2] + "." + (String)WiFi.localIP()[3] + "|div\n";
	values += "x_gateway|" + (String)WiFi.gatewayIP()[0] + "." + (String)WiFi.gatewayIP()[1] + "." + (String)WiFi.gatewayIP()[2] + "." + (String)WiFi.gatewayIP()[3] + "|div\n";
	values += "x_netmask|" + (String)WiFi.subnetMask()[0] + "." + (String)WiFi.subnetMask()[1] + "." + (String)WiFi.subnetMask()[2] + "." + (String)WiFi.subnetMask()[3] + "|div\n";
	values += "x_mac|" + getMacAddress() + "|div\n";
	values += "x_dns|" + (String)WiFi.dnsIP()[0] + "." + (String)WiFi.dnsIP()[1] + "." + (String)WiFi.dnsIP()[2] + "." + (String)WiFi.dnsIP()[3] + "|div\n";
	values += "x_ntp_sync|" + NTP.getTimeDateString(NTP.getLastNTPSync()) + "|div\n";
	values += "x_ntp_time|" + NTP.getTimeStr() + "|div\n";
	values += "x_ntp_date|" + NTP.getDateStr() + "|div\n";

	request->send(200, "text/plain", values);
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__FUNCTION__);
#endif // DEBUG_DYNAMICDATA

}

String AsyncFSWebServer::getMacAddress()
{
	uint8_t mac[6];
	char macStr[18] = { 0 };
	WiFi.macAddress(mac);
	sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return  String(macStr);
}

void AsyncFSWebServer::send_NTP_configuration_values_html(AsyncWebServerRequest *request)
{

	String values = "";
	values += "ntpserver|" + (String)_config.ntpServerName + "|input\n";
	values += "update|" + (String)_config.updateNTPTimeEvery + "|input\n";
	values += "tz|" + (String)_config.timezone + "|input\n";
	values += "dst|" + (String)(_config.daylight ? "checked" : "") + "|chk\n";
	request->send(200, "text/plain", values);
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__FUNCTION__);
#endif // DEBUG_DYNAMICDATA

}

// convert a single hex digit character to its integer value (from https://code.google.com/p/avr-netino/)
unsigned char AsyncFSWebServer::h2int(char c)
{
	if (c >= '0' && c <= '9') {
		return((unsigned char)c - '0');
	}
	if (c >= 'a' && c <= 'f') {
		return((unsigned char)c - 'a' + 10);
	}
	if (c >= 'A' && c <= 'F') {
		return((unsigned char)c - 'A' + 10);
	}
	return(0);
}

String AsyncFSWebServer::urldecode(String input) // (based on https://code.google.com/p/avr-netino/)
{
	char c;
	String ret = "";

	for (byte t = 0; t<input.length(); t++)
	{
		c = input[t];
		if (c == '+') c = ' ';
		if (c == '%') {


			t++;
			c = input[t];
			t++;
			c = (h2int(c) << 4) | h2int(input[t]);
		}

		ret.concat(c);
	}
	return ret;

}

//
// Check the Values is between 0-255
//
boolean AsyncFSWebServer::checkRange(String Value)
{
	if (Value.toInt() < 0 || Value.toInt() > 255)
	{
		return false;
	}
	else
	{
		return true;
	}
}

void AsyncFSWebServer::send_network_configuration_html(AsyncWebServerRequest *request)
{
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__FUNCTION__);
#endif // DEBUG_DYNAMICDATA

	if (!checkAuth(request))
		return request->requestAuthentication();

	if (request->args() > 0)  // Save Settings
	{
		//String temp = "";
		bool oldDHCP = _config.dhcp; // Save status to avoid general.html cleares it
		_config.dhcp = false;
		for (uint8_t i = 0; i < request->args(); i++) {
#ifdef DEBUG_DYNAMICDATA
			DBG_OUTPUT_PORT.printf("Arg %d: %s\n", i, request->arg(i).c_str());
#endif // DEBUG_DYNAMICDATA
			if (request->argName(i) == "devicename") {
				_config.deviceName = urldecode(request->arg(i));
				_config.dhcp = oldDHCP;
				continue;
			}
			if (request->argName(i) == "ssid") { _config.ssid = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "password") { _config.password = urldecode(request->arg(i)); continue; }
			if (request->argName(i) == "ip_0") { if (checkRange(request->arg(i))) 	_config.ip[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_1") { if (checkRange(request->arg(i))) 	_config.ip[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_2") { if (checkRange(request->arg(i))) 	_config.ip[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_3") { if (checkRange(request->arg(i))) 	_config.ip[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_0") { if (checkRange(request->arg(i))) 	_config.netmask[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_1") { if (checkRange(request->arg(i))) 	_config.netmask[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_2") { if (checkRange(request->arg(i))) 	_config.netmask[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_3") { if (checkRange(request->arg(i))) 	_config.netmask[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_0") { if (checkRange(request->arg(i))) 	_config.gateway[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_1") { if (checkRange(request->arg(i))) 	_config.gateway[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_2") { if (checkRange(request->arg(i))) 	_config.gateway[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_3") { if (checkRange(request->arg(i))) 	_config.gateway[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_0") { if (checkRange(request->arg(i))) 	_config.dns[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_1") { if (checkRange(request->arg(i))) 	_config.dns[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_2") { if (checkRange(request->arg(i))) 	_config.dns[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_3") { if (checkRange(request->arg(i))) 	_config.dns[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dhcp") {									_config.dhcp = true; continue; }
		}
		request->send(200, "text/html", Page_WaitAndReload);
		save_config();
		//yield();
		delay(1000);
		_fs->end();
		ESP.restart();
		//ConfigureWifi();
		//AdminTimeOutCounter = 0;
	}
	else
	{
		DBG_OUTPUT_PORT.println(request->url());
		handleFileRead(request, request->url());
	}
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__PRETTY_FUNCTION__);
#endif // DEBUG_DYNAMICDATA
}

void AsyncFSWebServer::send_general_configuration_html(AsyncWebServerRequest *request)
{
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__FUNCTION__);
#endif // DEBUG_DYNAMICDATA

	if (!checkAuth(request))
		return request->requestAuthentication();

	if (request->args() > 0)  // Save Settings
	{
		for (uint8_t i = 0; i < request->args(); i++) {
#ifdef DEBUG_DYNAMICDATA
			DBG_OUTPUT_PORT.printf("Arg %d: %s\n", i, request->arg(i).c_str());
#endif // DEBUG_DYNAMICDATA
			if (request->argName(i) == "devicename") {
				_config.deviceName = urldecode(request->arg(i));
				continue;
			}
		}
		request->send(200, "text/html", Page_Restart);
		save_config();
		_fs->end();
		ESP.restart();
		//ConfigureWifi();
		//AdminTimeOutCounter = 0;
	}
	else
	{
		handleFileRead(request, request->url());
	}
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__PRETTY_FUNCTION__);
#endif // DEBUG_DYNAMICDATA
}

void AsyncFSWebServer::send_NTP_configuration_html(AsyncWebServerRequest *request)
{

	if (!checkAuth(request))
		return request->requestAuthentication();

	if (request->args() > 0)  // Save Settings
	{
		_config.daylight = false;
		//String temp = "";
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "ntpserver") {
				_config.ntpServerName = urldecode(request->arg(i));
				NTP.setNtpServerName(_config.ntpServerName);
				continue;
			}
			if (request->argName(i) == "update") {
				_config.updateNTPTimeEvery = request->arg(i).toInt();
				NTP.setInterval(_config.updateNTPTimeEvery * 60);
				continue;
			}
			if (request->argName(i) == "tz") {
				_config.timezone = request->arg(i).toInt();
				NTP.setTimeZone(_config.timezone / 10);
				continue;
			}
			if (request->argName(i) == "dst") {
				_config.daylight = true;
#ifdef DEBUG_DYNAMICDATA
				DBG_OUTPUT_PORT.printf("Daylight Saving: %d\n", config.daylight);
#endif // DEBUG_DYNAMICDATA
				continue;
			}
		}

		NTP.setDayLight(_config.daylight);
		save_config();
		//firstStart = true;

		setTime(NTP.getTime()); //set time
	}
	handleFileRead(request, "/ntp.html");
	//server.send(200, "text/html", PAGE_NTPConfiguration);
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__PRETTY_FUNCTION__);
#endif // DEBUG_DYNAMICDATA

}

void AsyncFSWebServer::restart_esp(AsyncWebServerRequest *request) {
	//server.send(200, "text/html", Page_Restart);
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__FUNCTION__);
#endif // DEBUG_DYNAMICDATA
	_fs->end(); // SPIFFS.end();
	delay(1000);
	ESP.restart();
}

void AsyncFSWebServer::send_wwwauth_configuration_values_html(AsyncWebServerRequest *request) {
	String values = "";

	values += "wwwauth|" + (String)(_httpAuth.auth ? "checked" : "") + "|chk\n";
	values += "wwwuser|" + (String)_httpAuth.wwwUsername + "|input\n";
	values += "wwwpass|" + (String)_httpAuth.wwwPassword + "|input\n";

	request->send(200, "text/plain", values);
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__FUNCTION__);
#endif // DEBUG_DYNAMICDATA
}

void AsyncFSWebServer::send_wwwauth_configuration_html(AsyncWebServerRequest *request)
{
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.printf("%s %d\n", __FUNCTION__, request->args());
#endif // DEBUG_DYNAMICDATA
	if (request->args() > 0)  // Save Settings
	{
		_httpAuth.auth = false;
		//String temp = "";
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "wwwuser") {
				_httpAuth.wwwUsername = urldecode(request->arg(i));
#ifdef DEBUG_DYNAMICDATA
				DBG_OUTPUT_PORT.printf("User: %s\n", httpAuth.wwwUsername.c_str());
#endif // DEBUG_DYNAMICDATA
				continue;
			}
			if (request->argName(i) == "wwwpass") {
				_httpAuth.wwwPassword = urldecode(request->arg(i));
#ifdef DEBUG_DYNAMICDATA
				DBG_OUTPUT_PORT.printf("Pass: %s\n", httpAuth.wwwPassword.c_str());
#endif // DEBUG_DYNAMICDATA
				continue;
			}
			if (request->argName(i) == "wwwauth") {
				_httpAuth.auth = true;
#ifdef DEBUG_DYNAMICDATA
				DBG_OUTPUT_PORT.printf("HTTP Auth enabled\n");
#endif // DEBUG_DYNAMICDATA
				continue;
			}
		}

		saveHTTPAuth();
	}
	handleFileRead(request, "/system.html");
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__PRETTY_FUNCTION__);
#endif // DEBUG_DYNAMICDATA

}

bool AsyncFSWebServer::saveHTTPAuth() {
	//flag_config = false;
#ifdef DEBUG
	DBG_OUTPUT_PORT.println("Save secret");
#endif
	StaticJsonBuffer<256> jsonBuffer;
	JsonObject& json = jsonBuffer.createObject();
	json["auth"] = _httpAuth.auth;
	json["user"] = _httpAuth.wwwUsername;
	json["pass"] = _httpAuth.wwwPassword;

	//TODO add AP data to html
	File configFile = _fs->open(SECRET_FILE, "w");
	if (!configFile) {
#ifdef DEBUG
		DBG_OUTPUT_PORT.println("Failed to open secret file for writing");
#endif // DEBUG
		configFile.close();
		return false;
	}

#ifdef DEBUG
	String temp;
	json.prettyPrintTo(temp);
	Serial.println(temp);
#endif

	json.printTo(configFile);
	configFile.flush();
	configFile.close();
	return true;
}

void AsyncFSWebServer::send_update_firmware_values_html(AsyncWebServerRequest *request) {
	String values = "";
	uint32_t maxSketchSpace = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
	//bool updateOK = Update.begin(maxSketchSpace);
	bool updateOK = maxSketchSpace < ESP.getFreeSketchSpace();
	StreamString result;
	Update.printError(result);
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.printf("--MaxSketchSpace: %d\n", maxSketchSpace);
	DBG_OUTPUT_PORT.printf("--Update error = %s\n", result.c_str());
#endif // DEBUG_DYNAMICDATA
	values += "remupd|" + (String)((updateOK) ? "OK" : "ERROR") + "|div\n";

	if (Update.hasError()) {
		result.trim();
		values += "remupdResult|" + result + "|div\n";
	}
	else {
		values += "remupdResult||div\n";
	}

	request->send(200, "text/plain", values);
#ifdef DEBUG_DYNAMICDATA
	DBG_OUTPUT_PORT.println(__FUNCTION__);
#endif // DEBUG_DYNAMICDATA
}

void AsyncFSWebServer::setUpdateMD5(AsyncWebServerRequest *request) {
	_browserMD5 = "";
#ifdef DEBUG_WEBSERVER
	DBG_OUTPUT_PORT.printf("Arg number: %d\n", request->args());
#endif // DEBUG_WEBSERVER
	if (request->args() > 0)  // Read hash
	{
		//String temp = "";
		for (uint8_t i = 0; i < request->args(); i++) {
#ifdef DEBUG_WEBSERVER
			DBG_OUTPUT_PORT.printf("Arg %s: %s\n", request->argName(i).c_str(), request->arg(i).c_str());
#endif // DEBUG_WEBSERVER
			if (request->argName(i) == "md5") {
				_browserMD5 = urldecode(request->arg(i));
				Update.setMD5(_browserMD5.c_str());
				continue;
			}if (request->argName(i) == "size") {
				_updateSize = request->arg(i).toInt();
#ifdef DEBUG_WEBSERVER
				DBG_OUTPUT_PORT.printf("Update size: %u\n", request->arg(i).toInt());
#endif // DEBUG_WEBSERVER
				continue;
			}
		}
		request->send(200, "text/html", "OK --> MD5: " + _browserMD5);
	}

}

void AsyncFSWebServer::updateFirmware(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	// handler for the file upload, get's the sketch bytes, and writes
	// them through the Update object
	static long totalSize = 0;
	if (!index) { //UPLOAD_FILE_START
		SPIFFS.end();
		Update.runAsync(true);
		DBG_OUTPUT_PORT.printf("Update start: %s\n", filename.c_str());
		uint32_t maxSketchSpace = ESP.getSketchSize();
		DBG_OUTPUT_PORT.printf("Max free scketch space: %u\n", maxSketchSpace);
		DBG_OUTPUT_PORT.printf("New scketch size: %u\n", _updateSize);
		if (_browserMD5 != NULL && _browserMD5 != "") {
			Update.setMD5(_browserMD5.c_str());
			DBG_OUTPUT_PORT.printf("Hash from client: %s\n", _browserMD5.c_str());
		}
		if (!Update.begin(_updateSize)) {//start with max available size
			Update.printError(DBG_OUTPUT_PORT);
		}

	}

	// Get upload file, continue if not start
	totalSize += len;
	DBG_OUTPUT_PORT.print(".");
	size_t written = Update.write(data, len);
	if (written != len) {
		DBG_OUTPUT_PORT.printf("len = %d, written = %l, totalSize = %l\r\n", len, written, totalSize);
		//Update.printError(DBG_OUTPUT_PORT);
		//return;
	}
	if (final) {  // UPLOAD_FILE_END
		String updateHash;
		DBG_OUTPUT_PORT.println("Applying update...");
		if (Update.end(true)) { //true to set the size to the current progress
			updateHash = Update.md5String();
			DBG_OUTPUT_PORT.printf("Upload finished. Calculated MD5: %s\n", updateHash.c_str());
			DBG_OUTPUT_PORT.printf("Update Success: %u\nRebooting...\n", request->contentLength());
		}
		else {
			updateHash = Update.md5String();
			DBG_OUTPUT_PORT.printf("Upload failed. Calculated MD5: %s\n", updateHash.c_str());
			Update.printError(DBG_OUTPUT_PORT);
		}
	}

	//delay(2);
}

void AsyncFSWebServer::webSocketEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *payload, size_t length) {

	if (type == WS_EVT_DISCONNECT) {
#ifdef DEBUG_DYNAMICDATA
		DBG_OUTPUT_PORT.printf("[%u] Disconnected!\n", client->id());
#endif // DEBUG_DYNAMICDATA
	}
	else if (type == WS_EVT_CONNECT) {
#ifdef DEBUG_DYNAMICDATA
		wsNumber = client->id();
		IPAddress ip = client->remoteIP();
		DBG_OUTPUT_PORT.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", client->id(), ip[0], ip[1], ip[2], ip[3], payload);
#endif // DEBUG_DYNAMICDATA

		// send message to client
		//wsServer.sendTXT(num, "Connected");
	}
	else if (type == WS_EVT_DATA) {
		AwsFrameInfo * info = (AwsFrameInfo*)arg;
		String msg = "";
		if (info->final && info->index == 0 && info->len == length) {
			//the whole message is in a single frame and we got all of it's data
			if (info->opcode == WS_TEXT) {
				for (size_t i = 0; i < info->len; i++) {
					msg += (char)payload[i];
				}
			}
			else { // Binary
				char buff[3];
				for (size_t i = 0; i < info->len; i++) {
					sprintf(buff, "%02x ", (uint8_t)payload[i]);
					msg += buff;
				}
			}
#ifdef DEBUG_DYNAMICDATA
			DBG_OUTPUT_PORT.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);
			DBG_OUTPUT_PORT.printf("%s\r\n", msg.c_str());
#endif // DEBUG_DYNAMICDATA
		}
		else {
			//message is comprised of multiple frames or the frame is split into multiple packets
			if (info->index == 0) { // Message start
				if (info->num == 0)
					DBG_OUTPUT_PORT.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
				DBG_OUTPUT_PORT.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
			}
			// Continue message
			DBG_OUTPUT_PORT.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, info->index + length);

			if (info->opcode == WS_TEXT) { // Text
				for (size_t i = 0; i < info->len; i++) {
					msg += (char)payload[i];
				}
			}
			else { // Binary
				char buff[3];
				for (size_t i = 0; i < info->len; i++) {
					sprintf(buff, "%02x ", (uint8_t)payload[i]);
					msg += buff;
				}
			}
			DBG_OUTPUT_PORT.printf("%s\r\n", msg.c_str());

			if ((info->index + length) == info->len) { // Message end
				DBG_OUTPUT_PORT.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
				if (info->final) {
					DBG_OUTPUT_PORT.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
					if (info->message_opcode == WS_TEXT)
						client->text("I got your text message");
					else
						client->binary("I got your binary message");
				}
			}
		}
		// send message to client
		//client->text("message here");

		// send data to all connected clients
		//client->message();
		// webSocket.broadcastTXT("message here");

	}

}


void AsyncFSWebServer::serverInit() {
	//SERVER INIT
	//list directory
	on("/list", HTTP_GET, std::bind(
		[](AsyncFSWebServer* self, AsyncWebServerRequest *request) {
			if (!self->checkAuth(request))
				return request->requestAuthentication();
			self->handleFileList(request);
		}, 
		this, _1));
		
		//std::bind(&AsyncFSWebServer::handleFileList, this, _1));
	//load editor
	on("/edit", HTTP_GET, std::bind(&AsyncFSWebServer::handleFileRead, this, _1, "/index.html")); // Have to check this
	//create file
	on("/edit", HTTP_PUT, std::bind(&AsyncFSWebServer::handleFileCreate, this, _1));
	//delete file
	on("/edit", HTTP_DELETE, std::bind(&AsyncFSWebServer::handleFileDelete, this, _1));
	//first callback is called after the request has ended with all parsed arguments
	//second callback handles file uploads at that location
	on("/edit", HTTP_POST, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", ""); }, std::bind(&AsyncFSWebServer::handleFileUpload, this, _1, _2, _3, _4, _5, _6));
	on("/admin/generalvalues", HTTP_GET, std::bind(&AsyncFSWebServer::send_general_configuration_values_html, this, _1));
	on("/admin/values", std::bind(&AsyncFSWebServer::send_network_configuration_values_html, this, _1));
	on("/admin/connectionstate", std::bind(&AsyncFSWebServer::send_connection_state_values_html, this, _1));
	on("/admin/infovalues", std::bind(&AsyncFSWebServer::send_information_values_html, this, _1));
	on("/admin/ntpvalues", std::bind(&AsyncFSWebServer::send_NTP_configuration_values_html, this, _1));
	on("/config.html", std::bind(&AsyncFSWebServer::send_network_configuration_html, this, _1));
	on("/general.html", std::bind(&AsyncFSWebServer::send_general_configuration_html, this, _1));
	on("/ntp.html", std::bind(&AsyncFSWebServer::send_NTP_configuration_html, this, _1));
	//server.on("/admin/devicename", send_devicename_value_html);
	on("/admin/restart", std::bind([](AsyncFSWebServer* self, AsyncWebServerRequest *request) {
		DBG_OUTPUT_PORT.println(request->url());
		if (!self->checkAuth(request))
			return request->requestAuthentication();
		self->restart_esp(request);
	}, this, _1));
	on("/admin/wwwauth", std::bind([](AsyncFSWebServer* self, AsyncWebServerRequest *request) {
		if (!self->checkAuth(request))
			return request->requestAuthentication();
		self->send_wwwauth_configuration_values_html(request);
	}, this, _1));
	on("/admin", HTTP_GET, std::bind([](AsyncFSWebServer* self, AsyncWebServerRequest *request) {
		if (!self->checkAuth(request))
			return request->requestAuthentication();
		if (!self->handleFileRead(request, "/admin.html"))
			request->send(404, "text/plain", "FileNotFound");
	}, this, _1));
	on("/system.html", std::bind([](AsyncFSWebServer* self, AsyncWebServerRequest *request) {
		if (!self->checkAuth(request))
			return request->requestAuthentication();
		self->send_wwwauth_configuration_html(request);
	}, this, _1));
	on("/update/updatepossible", std::bind([](AsyncFSWebServer* self, AsyncWebServerRequest *request) {
		if (!self->checkAuth(request))
			return request->requestAuthentication();
		self->send_update_firmware_values_html(request);
	}, this, _1));
	on("/setmd5", std::bind([](AsyncFSWebServer* self, AsyncWebServerRequest *request) {
		if (!self->checkAuth(request))
			return request->requestAuthentication();
		//DBG_OUTPUT_PORT.println("md5?");
		self->setUpdateMD5(request);
	}, this, _1));
	on("/update", HTTP_GET, std::bind([](AsyncFSWebServer* self, AsyncWebServerRequest *request) {
		if (!self->checkAuth(request))
			return request->requestAuthentication();
		if (!self->handleFileRead(request, "/update.html"))
			request->send(404, "text/plain", "FileNotFound");
	}, this, _1));
	on("/update", HTTP_POST, std::bind([](AsyncFSWebServer* self, AsyncWebServerRequest *request) {
		if (!self->checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (Update.hasError()) ? "FAIL" : "<META http-equiv=\"refresh\" content=\"15;URL=/update\">Update correct. Restarting...");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
		self->_fs->end();
		ESP.restart();
	}, this, _1), std::bind(&AsyncFSWebServer::updateFirmware, this, _1, _2, _3, _4, _5, _6));

	_ws->onEvent(std::bind(&AsyncFSWebServer::webSocketEvent, this, _1, _2, _3, _4, _5, _6));
	addHandler(_ws);

	//called when the url is not defined here
	//use it to load content from SPIFFS
	onNotFound(std::bind([](AsyncFSWebServer* self, AsyncWebServerRequest *request) {
		DBG_OUTPUT_PORT.printf("Not found: %s\r\n", request->url().c_str());
		if (!self->checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(200);
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		if (!self->handleFileRead(request, request->url()))
			request->send(404, "text/plain", "FileNotFound");
	}, this, _1));

#define HIDE_SECRET
#ifdef HIDE_SECRET
	on(SECRET_FILE, HTTP_GET, [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});
#endif // HIDE_SECRET

#ifdef HIDE_CONFIG
	on(CONFIG_FILE, HTTP_GET, [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});
#endif // HIDE_CONFIG

	//get heap status, analog input value and all GPIO statuses in one json call
	on("/all", HTTP_GET, [](AsyncWebServerRequest *request) {
		String json = "{";
		json += "\"heap\":" + String(ESP.getFreeHeap());
		json += ", \"analog\":" + String(analogRead(A0));
		json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
		json += "}";
		request->send(200, "text/json", json);
		json = String();
	});
	//server.begin(); --> Not here
	//httpUpdater.setup(&server,httpAuth.wwwUsername,httpAuth.wwwPassword);
#ifdef DEBUG_WEBSERVER
	DBG_OUTPUT_PORT.println("HTTP server started");
#endif // DEBUG_WEBSERVER
}

bool AsyncFSWebServer::checkAuth(AsyncWebServerRequest *request) {
	if (!_httpAuth.auth) {
		return true;
	}
	else
	{
		return request->authenticate(_httpAuth.wwwUsername.c_str(), _httpAuth.wwwPassword.c_str());
	}

}