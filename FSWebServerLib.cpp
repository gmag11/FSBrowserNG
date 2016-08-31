// 
// 
// 

#define DEBUG

#define DBG_OUTPUT_PORT Serial

#ifdef DEBUG
#define DEBUGLOG(...) DBG_OUTPUT_PORT.printf(__VA_ARGS__)
#else
#define DEBUGLOG(...)
#endif

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

AsyncFSWebServer::AsyncFSWebServer(uint16_t port) : AsyncWebServer(port) {}

void AsyncFSWebServer::secondTick()
{
	_secondFlag = true;
}

void AsyncFSWebServer::secondTask() {
	//DEBUGLOG("%s\r\n", NTP.getTimeDateString().c_str());
	sendTimeData();
}

void AsyncFSWebServer::s_secondTick(void* arg) {
	AsyncFSWebServer* self = reinterpret_cast<AsyncFSWebServer*>(arg);
	self->secondTask();
}

void AsyncFSWebServer::sendTimeData() {
	String str = "";
	str = "T" + NTP.getTimeStr();
	_ws.textAll(str);
	str = "";
	str = "D" + NTP.getDateStr();
	_ws.textAll(str);
	str = "";
	str = "S" + NTP.getTimeDateString(NTP.getLastNTPSync());
	_ws.textAll(str);
	str = "";
	str = "U" + NTP.getUptimeString();
	_ws.textAll(str);
	str = "";
	str = "B" + NTP.getTimeDateString(NTP.getLastBootTime());
	_ws.textAll(str);
	str = "";
	//DEBUGLOG(__PRETTY_FUNCTION__);
	//DEBUGLOG("\r\n")
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

void flashLED(int pin, int times, int delayTime) {
	int oldState = digitalRead(pin);
	DEBUGLOG("---Flash LED during %d ms %d times. Old state = %d\r\n", delayTime, times, oldState);

	for (int i = 0; i < times; i++) {
		digitalWrite(pin, LOW); // Turn on LED
		delay(delayTime);
		digitalWrite(pin, HIGH); // Turn on LED
		delay(delayTime);
	}
	digitalWrite(pin, oldState); // Turn on LED
}

void AsyncFSWebServer::begin(FS* fs)
{
	_fs = fs;
	DBG_OUTPUT_PORT.begin(115200);
	DBG_OUTPUT_PORT.print("\n\n");
#ifdef DEBUG
	DBG_OUTPUT_PORT.setDebugOutput(true);
#endif // DEBUG
	// NTP client setup
	if (CONNECTION_LED >= 0) {
		pinMode(CONNECTION_LED, OUTPUT); // CONNECTION_LED pin defined as output
	}
	if (AP_ENABLE_BUTTON >= 0) {
		pinMode(AP_ENABLE_BUTTON, INPUT); // If this pin is HIGH during startup ESP will run in AP_ONLY mode. Backdoor to change WiFi settings when configured WiFi is not available.
	}
	//analogWriteFreq(200);

	if (AP_ENABLE_BUTTON >= 0) {
		_apConfig.APenable = digitalRead(AP_ENABLE_BUTTON); // Read AP button
	DEBUGLOG("AP Enable = %d\n", _apConfig.APenable);
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

			DEBUGLOG("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
		}
		DEBUGLOG("\n");
	}
#endif // DEBUG
	if (!load_config()) { // Try to load configuration from file system
		defaultConfig(); // Load defaults if any error
		_apConfig.APenable = true;
	}
	loadHTTPAuth();
	//WIFI INIT
	if (_config.updateNTPTimeEvery > 0) { // Enable NTP sync
		NTP.begin(_config.ntpServerName, _config.timezone / 10, _config.daylight);
		NTP.setInterval(15, _config.updateNTPTimeEvery * 60);
	}
	// Register wifi Event to control connection LED
	onStationModeConnectedHandler = WiFi.onStationModeConnected([this](WiFiEventStationModeConnected data) {
		this->onWiFiConnected(data);
	});
	onStationModeDisconnectedHandler = WiFi.onStationModeDisconnected([this](WiFiEventStationModeDisconnected data) {
		this->onWiFiDisconnected(data);
	});
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
	DEBUGLOG("Open http://");
	DEBUGLOG(_config.deviceName.c_str());
	DEBUGLOG(".local/edit to see the file browser\r\n");
	DEBUGLOG("Flash chip size: %u\r\n", ESP.getFlashChipRealSize());
	DEBUGLOG("Scketch size: %u\r\n", ESP.getSketchSize());
	DEBUGLOG("Free flash space: %u\r\n", ESP.getFreeSketchSpace());

	_secondTk.attach(1.0f, &AsyncFSWebServer::s_secondTick, static_cast<void*>(this)); // Task to run periodic things every second
	//AsyncWebSocket ws = AsyncWebSocket("/ws");
	//_ws = &ws;

	AsyncWebServer::begin();
	serverInit(); // Configure and start Web server
	
	MDNS.begin(_config.deviceName.c_str()); // I've not got this to work. Need some investigation.
	MDNS.addService("http", "tcp", 80);
	ConfigureOTA(_httpAuth.wwwPassword.c_str());
	DEBUGLOG("END Setup");
}

bool AsyncFSWebServer::load_config() {
	File configFile =  _fs->open(CONFIG_FILE, "r");
	if (!configFile) {
		DEBUGLOG("Failed to open config file");
		return false;
	}

	size_t size = configFile.size();
	/*if (size > 1024) {
		DEBUGLOG("Config file size is too large");
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
	DEBUGLOG("JSON file size: %d bytes\r\n", size);
	DynamicJsonBuffer jsonBuffer(1024);
	//StaticJsonBuffer<1024> jsonBuffer;
	JsonObject& json = jsonBuffer.parseObject(buf.get());

	if (!json.success()) {
		DEBUGLOG("Failed to parse config file\r\n");
		return false;
	}
#ifdef DEBUG
	String temp;
	json.prettyPrintTo(temp);
	Serial.println(temp);
#endif

	_config.ssid = json["ssid"].as<const char *>();

	_config.password = json["pass"].as<const char *>();

	_config.ip = IPAddress(json["ip"][0], json["ip"][1], json["ip"][2], json["ip"][3]);
	_config.netmask = IPAddress(json["netmask"][0], json["netmask"][1], json["netmask"][2], json["netmask"][3]);
	_config.gateway = IPAddress(json["gateway"][0], json["gateway"][1], json["gateway"][2], json["gateway"][3]);
	_config.dns = IPAddress(json["dns"][0], json["dns"][1], json["dns"][2], json["dns"][3]);

	_config.dhcp = json["dhcp"].as<bool>();

	_config.ntpServerName = json["ntp"].as<const char *>();
	_config.updateNTPTimeEvery = json["NTPperiod"].as<long>();
	_config.timezone = json["timeZone"].as<long>();
	_config.daylight = json["daylight"].as<long>();
	_config.deviceName = json["deviceName"].as<const char *>();

	//config.connectionLed = json["led"];

	DEBUGLOG("Data initialized.\r\n");
	DEBUGLOG("SSID: %s ", _config.ssid.c_str());
	DEBUGLOG("PASS: %s\r\n", _config.password.c_str());
	DEBUGLOG("NTP Server: %s\r\n", _config.ntpServerName.c_str());
	//DEBUGLOG("Connection LED: %d\n", config.connectionLed);
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");

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
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

bool AsyncFSWebServer::save_config() {
	//flag_config = false;
	DEBUGLOG("Save config\r\n");
	DynamicJsonBuffer jsonBuffer(512);
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
		DEBUGLOG("Failed to open config file for writing\r\n");
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
		DEBUGLOG("Failed to open secret file\r\n");
		_httpAuth.auth = false;
		_httpAuth.wwwUsername = "";
		_httpAuth.wwwPassword = "";
		configFile.close();
		return false;
	}

	size_t size = configFile.size();
	/*if (size > 256) {
		DEBUGLOG("Secret file size is too large\r\n");
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
	DEBUGLOG("JSON secret file size: %d bytes\n", size);
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

	DEBUGLOG(_httpAuth.auth ? "Secret initialized.\r\n" : "Auth disabled.\r\n");
	if (_httpAuth.auth) {
		DEBUGLOG("User: %s\r\n", _httpAuth.wwwUsername.c_str());
		DEBUGLOG("Pass: %s\r\n", _httpAuth.wwwPassword.c_str());
	}
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");

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

void AsyncFSWebServer::configureWifiAP() {
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
	//WiFi.disconnect();
	WiFi.mode(WIFI_AP);
	String APname = _apConfig.APssid + (String)ESP.getChipId();
	if (_httpAuth.auth) {
		WiFi.softAP(APname.c_str(), _httpAuth.wwwPassword.c_str());
		DEBUGLOG("AP Pass enabled: %s\r\n", _httpAuth.wwwPassword.c_str());
	}
	else {
		WiFi.softAP(APname.c_str());
		DEBUGLOG("AP Pass disabled\r\n");
	}
	if (CONNECTION_LED >= 0) {
		flashLED(CONNECTION_LED, 3, 250);
	}
}

void AsyncFSWebServer::configureWifi()
{
	WiFi.mode(WIFI_STA);
	//currentWifiStatus = WIFI_STA_DISCONNECTED;
	DEBUGLOG("Connecting to %s\r\n", _config.ssid.c_str());
	WiFi.begin(_config.ssid.c_str(), _config.password.c_str());
	if (!_config.dhcp)
	{
		DEBUGLOG("NO DHCP\r\n");
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

	DEBUGLOG("Gateway:    %s\r\n", WiFi.gatewayIP().toString().c_str());
	DEBUGLOG("DNS:        %s\r\n", WiFi.dnsIP().toString().c_str());
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::ConfigureOTA(String password) {
	// Port defaults to 8266
	// ArduinoOTA.setPort(8266);

	// Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(_config.deviceName.c_str());

	// No authentication by default
	if (password != "") {
		ArduinoOTA.setPassword(password.c_str());
		DEBUGLOG("OTA password set %s\n", password.c_str());
	}

#ifdef DEBUG
	ArduinoOTA.onStart([]() {
		DEBUGLOG("StartOTA\r\n");
	});
	ArduinoOTA.onEnd(std::bind([](FS *fs) {
		fs->end();
		DEBUGLOG("\r\nEnd OTA\r\n");
	}, _fs));
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		DEBUGLOG("OTA Progress: %u%%\r\n", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		DEBUGLOG("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) DEBUGLOG("Auth Failed\r\n");
		else if (error == OTA_BEGIN_ERROR) DEBUGLOG("Begin Failed\r\n");
		else if (error == OTA_CONNECT_ERROR) DEBUGLOG("Connect Failed\r\n");
		else if (error == OTA_RECEIVE_ERROR) DEBUGLOG("Receive Failed\r\n");
		else if (error == OTA_END_ERROR) DEBUGLOG("End Failed\r\n");
	});
	DEBUGLOG("\r\nOTA Ready\r\n");
#endif // DEBUG
	ArduinoOTA.begin();
}

void AsyncFSWebServer::onWiFiConnected(WiFiEventStationModeConnected data) {
	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, LOW); // Turn LED on
	}
	DEBUGLOG("Led %d on\n", CONNECTION_LED);
	//turnLedOn();
	wifiDisconnectedSince = 0;
}

void AsyncFSWebServer::onWiFiDisconnected(WiFiEventStationModeDisconnected data) {
	DEBUGLOG("case STA_DISCONNECTED");

	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, HIGH); // Turn LED off
	}
	//DBG_OUTPUT_PORT.printf("Led %s off\n", CONNECTION_LED);
	//flashLED(config.connectionLed, 2, 100);
	if (wifiDisconnectedSince == 0) {
		wifiDisconnectedSince = millis();
	}
	DEBUGLOG("\r\nDisconnected for %d seconds\r\n", (int)((millis() - wifiDisconnectedSince) / 1000));
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
	if (!request->hasArg("dir")) { request->send(500, "text/plain", "BAD ARGS"); return; }

	String path = request->arg("dir");
	DEBUGLOG("handleFileList: %s\r\n", path.c_str());
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
	DEBUGLOG("%s\r\n", output.c_str());
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

bool AsyncFSWebServer::handleFileRead(String path, AsyncWebServerRequest *request) {
	DEBUGLOG("handleFileRead: %s\r\n", path.c_str());
	if (CONNECTION_LED >= 0) {
		// CANNOT RUN DELAY() INSIDE CALLBACK
		flashLED(CONNECTION_LED, 1, 25); // Show activity on LED
	}
	if (path.endsWith("/"))
		path += "index.htm";
	String contentType = getContentType(path, request);
	String pathWithGz = path + ".gz";
	if (_fs->exists(pathWithGz) || _fs->exists(path)) {
		if (_fs->exists(pathWithGz)) {
			path += ".gz";
		}
		DEBUGLOG("Content type: %s\r\n", contentType.c_str());
		AsyncWebServerResponse *response = request->beginResponse(*_fs, path, contentType);
		if (path.endsWith(".gz"))
			response->addHeader("Content-Encoding", "gzip");
		//File file = SPIFFS.open(path, "r");
		DEBUGLOG("File %s exist\r\n", path.c_str());
		request->send(response);

		return true;
	}
	else
		DEBUGLOG("Cannot find %s\n", path.c_str());
	return false;
}

void AsyncFSWebServer::handleFileCreate(AsyncWebServerRequest *request) {
	if (!checkAuth(request))
		return request->requestAuthentication();
	if (request->args() == 0)
		return request->send(500, "text/plain", "BAD ARGS");
	String path = request->arg(0);
	DEBUGLOG("handleFileCreate: %s\r\n", path.c_str());
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
	DEBUGLOG("handleFileDelete: %s\r\n", path.c_str());
	if (path == "/")
		return request->send(500, "text/plain", "BAD PATH");
	if (!_fs->exists(path))
		return request->send(404, "text/plain", "FileNotFound");
	_fs->remove(path);
	request->send(200, "text/plain", "");
	path = String(); // Remove? Useless statement?
}

void AsyncFSWebServer::handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	static File fsUploadFile;
	
	if (!index) { // Start
		DEBUGLOG("handleFileUpload Name: %s\r\n", filename.c_str());
		if (!filename.startsWith("/")) filename = "/" + filename;
		fsUploadFile = _fs->open(filename, "w");
		DEBUGLOG("First upload part.\r\n");

	}
	// Continue
	if (fsUploadFile) {
		DEBUGLOG("Continue upload part. Size = %u\r\n", len);
		if (fsUploadFile.write(data, len) != len) {
			DBG_OUTPUT_PORT.println("Write error during upload");
		}
	}
	/*for (size_t i = 0; i < len; i++) {
		if (fsUploadFile)
			fsUploadFile.write(data[i]);
	}*/
	if (final) { // End
		if (fsUploadFile) {
			fsUploadFile.close();
		}
		DEBUGLOG("handleFileUpload Size: %u\n", len);
	}
}

void AsyncFSWebServer::send_general_configuration_values_html(AsyncWebServerRequest *request)
{
	String values = "";
	values += "devicename|" + (String)_config.deviceName + "|input\n";
	request->send(200, "text/plain", values);
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
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
	values = "";

	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::onWiFiScanComplete(int numNetworks) {
	String networks = "";

	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");

	if (numNetworks == 0)
	{
		networks = "<font color='#FF0000'>No networks found!</font>";
	}
	else
	{
		networks = "Found " + String(numNetworks) + " Networks<br>\n";
		networks += "<table border='0' cellspacing='0' cellpadding='3'>";
		networks += "<tr bgcolor='#DDDDDD'><td><strong>Name</strong></td><td><strong>Ch</strong></td><td><strong>Quality</strong></td><td><strong>Enc</strong></td><tr>\n";
		for (int i = 0; i < numNetworks; ++i)
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


			networks += "<tr><td><a href='javascript:selssid(\"" + String(WiFi.SSID(i)) + "\")'>" + String(WiFi.SSID(i)) + "</a></td><td>" + String(WiFi.channel(i)) + "</td><td>" + String(quality) + "%</td><td>" + String((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*") + "</td></tr>\n";
		}
		networks += "</table>\n";
	}
	WiFi.scanDelete();
	_evs.send(networks.c_str(),"networks");
	//DEBUGLOG("%s\r\n", networks.c_str());
	DEBUGLOG("Found %d networks\r\n", numNetworks);
	DEBUGLOG("Message length %d bytes\r\n", networks.length());
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

	WiFi.scanNetworksAsync([this](int n) {
		DEBUGLOG("--Scan complete");
		this->onWiFiScanComplete(n);
	});

	String values = "";
	values += "connectionstate|" + state + "|div\n";
	values += "networks|Scanning networks ...|div\n";
	request->send(200, "text/plain", values);
	state = "";
	values = "";
	Networks = "";
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
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
	values += "x_uptime|" + NTP.getUptimeString() + "|div\n";
	values += "x_last_boot|" + NTP.getTimeDateString(NTP.getLastBootTime()) + "|div\n";

	request->send(200, "text/plain", values);
	//delete &values;
	values = "";
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");

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
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");

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
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");


	if (request->args() > 0)  // Save Settings
	{
		//String temp = "";
		bool oldDHCP = _config.dhcp; // Save status to avoid general.html cleares it
		_config.dhcp = false;
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s\r\n", i, request->arg(i).c_str());
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
		handleFileRead(request->url(), request);
	}
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::send_general_configuration_html(AsyncWebServerRequest *request)
{
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");

	if (!checkAuth(request))
		return request->requestAuthentication();

	if (request->args() > 0)  // Save Settings
	{
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s\r\n", i, request->arg(i).c_str());
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
		handleFileRead(request->url(), request);
	}

	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
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
				DEBUGLOG("Daylight Saving: %d\r\n", _config.daylight);
				continue;
			}
		}

		NTP.setDayLight(_config.daylight);
		save_config();
		//firstStart = true;

		setTime(NTP.getTime()); //set time
	}
	handleFileRead("/ntp.html", request);
	//server.send(200, "text/html", PAGE_NTPConfiguration);
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");

}

void AsyncFSWebServer::restart_esp(AsyncWebServerRequest *request) {
	request->send(200, "text/html", Page_Restart);
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
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

	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::send_wwwauth_configuration_html(AsyncWebServerRequest *request)
{
	DEBUGLOG("%s %d\n", __FUNCTION__, request->args());
	if (request->args() > 0)  // Save Settings
	{
		_httpAuth.auth = false;
		//String temp = "";
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "wwwuser") {
				_httpAuth.wwwUsername = urldecode(request->arg(i));
				DEBUGLOG("User: %s\n", _httpAuth.wwwUsername.c_str());
				continue;
			}
			if (request->argName(i) == "wwwpass") {
				_httpAuth.wwwPassword = urldecode(request->arg(i));
				DEBUGLOG("Pass: %s\n", _httpAuth.wwwPassword.c_str());
				continue;
			}
			if (request->argName(i) == "wwwauth") {
				_httpAuth.auth = true;
				DEBUGLOG("HTTP Auth enabled\r\n");
				continue;
			}
		}

		saveHTTPAuth();
	}
	handleFileRead("/system.html", request);

	//DEBUGLOG(__PRETTY_FUNCTION__);
	//DEBUGLOG("\r\n");
}

bool AsyncFSWebServer::saveHTTPAuth() {
	//flag_config = false;
	DEBUGLOG("Save secret\r\n");
	DynamicJsonBuffer jsonBuffer(256);
	//StaticJsonBuffer<256> jsonBuffer;
	JsonObject& json = jsonBuffer.createObject();
	json["auth"] = _httpAuth.auth;
	json["user"] = _httpAuth.wwwUsername;
	json["pass"] = _httpAuth.wwwPassword;

	//TODO add AP data to html
	File configFile = _fs->open(SECRET_FILE, "w");
	if (!configFile) {
		DEBUGLOG("Failed to open secret file for writing\r\n");
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
	DEBUGLOG("--MaxSketchSpace: %d\n", maxSketchSpace);
	DEBUGLOG("--Update error = %s\n", result.c_str());
	values += "remupd|" + (String)((updateOK) ? "OK" : "ERROR") + "|div\n";

	if (Update.hasError()) {
		result.trim();
		values += "remupdResult|" + result + "|div\n";
	}
	else {
		values += "remupdResult||div\n";
	}

	request->send(200, "text/plain", values);
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::setUpdateMD5(AsyncWebServerRequest *request) {
	_browserMD5 = "";
	DEBUGLOG("Arg number: %d\r\n", request->args());
	if (request->args() > 0)  // Read hash
	{
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %s: %s\r\n", request->argName(i).c_str(), request->arg(i).c_str());
			if (request->argName(i) == "md5") {
				_browserMD5 = urldecode(request->arg(i));
				Update.setMD5(_browserMD5.c_str());
				continue;
			}if (request->argName(i) == "size") {
				_updateSize = request->arg(i).toInt();
				DEBUGLOG("Update size: %l\r\n", _updateSize);
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
		DEBUGLOG("[%u] Disconnected!\r\n", client->id());
		client->close();
	}
	else if (type == WS_EVT_CONNECT) {
#ifdef DEBUG_DYNAMICDATA
		wsNumber = client->id();
		IPAddress ip = client->remoteIP();
		DEBUGLOG("[%u] Connected from %d.%d.%d.%d url: %s\r\n", client->id(), ip[0], ip[1], ip[2], ip[3], payload);
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
			DEBUGLOG("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);
			DEBUGLOG("%s\r\n", msg.c_str());
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
	on("/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
			if (!this->checkAuth(request))
				return request->requestAuthentication();
			this->handleFileList(request);
		});
	//load editor
	on("/edit", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (!this->handleFileRead("/edit.html", request))
			request->send(404, "text/plain", "FileNotFound");
	});
	//create file
	on("/edit", HTTP_PUT, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->handleFileCreate(request);
	});	//delete file
	on("/edit", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->handleFileDelete(request);
	});
	//first callback is called after the request has ended with all parsed arguments
	//second callback handles file uploads at that location
	on("/edit", HTTP_POST, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", ""); }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		this->handleFileUpload(request, filename, index, data, len, final);
	});
	on("/admin/generalvalues", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_general_configuration_values_html(request);
	});
	//on("/admin/generalvalues", HTTP_GET, std::bind(&AsyncFSWebServer::send_general_configuration_values_html, this, _1));
	on("/admin/values", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_network_configuration_values_html(request);
	});
	//on("/admin/values", std::bind(&AsyncFSWebServer::send_network_configuration_values_html, this, _1));
	on("/admin/connectionstate", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_connection_state_values_html(request);
	});
	//on("/admin/connectionstate", std::bind(&AsyncFSWebServer::send_connection_state_values_html, this, _1));
	on("/admin/infovalues", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_information_values_html(request);
	});
	//on("/admin/infovalues", std::bind(&AsyncFSWebServer::send_information_values_html, this, _1));
	on("/admin/ntpvalues", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_NTP_configuration_values_html(request);
	});
	//on("/admin/ntpvalues", std::bind(&AsyncFSWebServer::send_NTP_configuration_values_html, this, _1));
	on("/config.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_network_configuration_html(request);
	});
	//on("/config.html", std::bind(&AsyncFSWebServer::send_network_configuration_html, this, _1));
	on("/general.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_general_configuration_html(request);
	});
	//on("/general.html", std::bind(&AsyncFSWebServer::send_general_configuration_html, this, _1));
	on("/ntp.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_NTP_configuration_html(request);
	});
	//on("/ntp.html", std::bind(&AsyncFSWebServer::send_NTP_configuration_html, this, _1));
	//server.on("/admin/devicename", send_devicename_value_html);
	on("/admin/restart", [this](AsyncWebServerRequest *request) {
		DBG_OUTPUT_PORT.println(request->url());
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->restart_esp(request);
	});
	on("/admin/wwwauth", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_wwwauth_configuration_values_html(request);
	});
	on("/admin", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (!this->handleFileRead("/admin.html", request))
			request->send(404, "text/plain", "FileNotFound");
	});
	on("/system.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_wwwauth_configuration_html(request);
	});
	on("/update/updatepossible", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_update_firmware_values_html(request);
	});
	on("/setmd5", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		//DBG_OUTPUT_PORT.println("md5?");
		this->setUpdateMD5(request);
	});
	on("/update", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (!this->handleFileRead("/update.html", request))
			request->send(404, "text/plain", "FileNotFound");
	});
	on("/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (Update.hasError()) ? "FAIL" : "<META http-equiv=\"refresh\" content=\"15;URL=/update\">Update correct. Restarting...");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
		this->_fs->end();
		ESP.restart();
	}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		this->updateFirmware(request, filename, index, data, len, final);
	});


	//called when the url is not defined here
	//use it to load content from SPIFFS
	onNotFound([this](AsyncWebServerRequest *request) {
		DBG_OUTPUT_PORT.printf("Not found: %s\r\n", request->url().c_str());
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(200);
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		if (!this->handleFileRead(request->url(), request))
			request->send(404, "text/plain", "FileNotFound");
		delete response; // Free up memory!
	});


	_ws.onEvent([this](AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *payload, size_t length) {
		this->webSocketEvent(server, client, type, arg, payload, length);
		DEBUGLOG("Websocket event: %d\r\n", type);
	});

	addHandler(&_ws);
	addHandler(&_evs);
	_evs.onConnect([](AsyncEventSourceClient* client) {
		DEBUGLOG("Event source client connected from %s\r\n", client->client()->remoteIP().toString().c_str());
	});

#define HIDE_SECRET
#ifdef HIDE_SECRET
	on(SECRET_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});
#endif // HIDE_SECRET

#ifdef HIDE_CONFIG
	on(CONFIG_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
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
	DEBUGLOG("HTTP server started\r\n");
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