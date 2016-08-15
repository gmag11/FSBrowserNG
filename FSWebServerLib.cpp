// 
// 
// 

#define DEBUG

#define DBG_OUTPUT_PORT Serial
#define CONNECTION_LED 2 // Connection LED pin (Built in)
#define AP_ENABLE_BUTTON 4 // Button pin to enable AP during startup for configuration

#include "FSWebServerLib.h"

AsyncFSWebServer ESPHTTPServer(80);

AsyncFSWebServer::AsyncFSWebServer(uint16_t port) : AsyncWebServer(port)
{
	
}

void secondTick(void *flag)
{
	*static_cast<bool *>(flag) = true;
}

void AsyncFSWebServer::secondTask() {
#ifdef DEBUG_GLOBALH
	//DBG_OUTPUT_PORT.println(ntp->getTimeString());
#endif // DEBUG_GLOBALH
	sendTimeData();
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
	WiFi.onEvent(WiFiEvent); // Register wifi Event to control connection LED
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

	_secondTk.attach(1, secondTick, (void *)this->_secondFlag); // Task to run periodic things every second
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

	_config.ssid = json["ssid"].asString();
	//String(ssid_str).toCharArray(config.ssid, 28);

	_config.password = json["pass"].asString();

	_config.ip = IPAddress(json["ip"]);
	_config.netmask = IPAddress(json["netmask"]);
	_config.gateway = IPAddress(json["gateway"]);
	_config.dns = IPAddress(json["dns"]);

	/*_config.ip[0] = json["ip"][0];
	_config.ip[1] = json["ip"][1];
	_config.ip[2] = json["ip"][2];
	_config.ip[3] = json["ip"][3];

	_config.netmask[0] = json["netmask"][0];
	_config.netmask[1] = json["netmask"][1];
	_config.netmask[2] = json["netmask"][2];
	_config.netmask[3] = json["netmask"][3];

	_config.gateway[0] = json["gateway"][0];
	_config.gateway[1] = json["gateway"][1];
	_config.gateway[2] = json["gateway"][2];
	_config.gateway[3] = json["gateway"][3];

	_config.dns[0] = json["dns"][0];
	_config.dns[1] = json["dns"][1];
	_config.dns[2] = json["dns"][2];
	_config.dns[3] = json["dns"][3];*/

	_config.dhcp = json["dhcp"];

	//String(pass_str).toCharArray(config.pass, 28);
	_config.ntpServerName = json["ntp"].asString();
	_config.updateNTPTimeEvery = json["NTPperiod"];
	_config.timezone = json["timeZone"];
	_config.daylight = json["daylight"];
	_config.deviceName = json["deviceName"].asString();

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
	ArduinoOTA.onEnd([]() {
		SPIFFS.end();
		DBG_OUTPUT_PORT.println("\nEnd OTA\n");
	});
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

void WiFiEvent(WiFiEvent_t event) {
	static long wifiDisconnectedSince = 0;

	/*String eventStr;
	switch (event) {
	case WIFI_EVENT_STAMODE_CONNECTED:
	eventStr = "STAMODE_CONNECTED"; break;
	case WIFI_EVENT_STAMODE_DISCONNECTED:
	eventStr = "STAMODE_DISCONNECTED"; break;
	case WIFI_EVENT_STAMODE_AUTHMODE_CHANGE:
	eventStr = "STAMODE_AUTHMODE_CHANGE"; break;
	case WIFI_EVENT_STAMODE_GOT_IP:
	eventStr = "STAMODE_GOT_IP"; break;
	case WIFI_EVENT_STAMODE_DHCP_TIMEOUT:
	eventStr = "STAMODE_DHCP_TIMEOUT"; break;
	case WIFI_EVENT_SOFTAPMODE_STACONNECTED:
	eventStr = "SOFTAPMODE_STACONNECTED"; break;
	case WIFI_EVENT_SOFTAPMODE_STADISCONNECTED:
	eventStr = "SOFTAPMODE_STADISCONNECTED"; break;
	case WIFI_EVENT_SOFTAPMODE_PROBEREQRECVED:
	eventStr = "SOFTAPMODE_PROBEREQRECVED"; break;
	case WIFI_EVENT_MAX:
	eventStr = "MAX_EVENTS"; break;
	}
	DBG_OUTPUT_PORT.printf("%s: %s\n",__PRETTY_FUNCTION__,eventStr.c_str());
	DBG_OUTPUT_PORT.printf("Current WiFi status: %d\n", currentWifiStatus);*/
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
}

void AsyncFSWebServer::handleFileList(AsyncWebServerRequest *request) {
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

bool AsyncFSWebServer::handleFileRead(String path, AsyncWebServerRequest *request) {
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

void AsyncFSWebServer::serverInit() {
	//SERVER INIT
	//list directory
	auto handleFilelist = std::bind(&AsyncFSWebServer::handleFileList, this, *request);

	on("/list", HTTP_GET, handleFilelist);
	//load editor
	on("/edit", HTTP_GET, [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		if (!ESPHTTPServer.handleFileRead("/edit.html", request))
			request->send(404, "text/plain", "FileNotFound");
	});
	//create file
	on("/edit", HTTP_PUT, [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		handleFileCreate(request);
	});
	//delete file
	on("/edit", HTTP_DELETE, [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		handleFileDelete(request);
	});
	//first callback is called after the request has ended with all parsed arguments
	//second callback handles file uploads at that location
	on("/edit", HTTP_POST, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", ""); }, handleFileUpload);
	on("/admin/generalvalues", HTTP_GET, [](AsyncWebServerRequest *request) { send_general_configuration_values_html(request); });
	on("/admin/values", [](AsyncWebServerRequest *request) { send_network_configuration_values_html(request); });
	on("/admin/connectionstate", [](AsyncWebServerRequest *request) { send_connection_state_values_html(request); });
	on("/admin/infovalues", [](AsyncWebServerRequest *request) { send_information_values_html(request); });
	on("/admin/ntpvalues", [](AsyncWebServerRequest *request) { send_NTP_configuration_values_html(request); });
	on("/config.html", [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		send_network_configuration_html(request);
	});
	on("/general.html", [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		send_general_configuration_html(request);
	});
	on("/ntp.html", [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		send_NTP_configuration_html(request);
	});
	//server.on("/admin/devicename", send_devicename_value_html);
	on("/admin/restart", [](AsyncWebServerRequest *request) {
		DBG_OUTPUT_PORT.println(request->url());
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		restart_esp(request);
	});
	on("/admin/wwwauth", [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		send_wwwauth_configuration_values_html(request);
	});
	on("/admin", HTTP_GET, [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		if (!handleFileRead("/admin.html", request))
			request->send(404, "text/plain", "FileNotFound");
	});
	on("/system.html", [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		send_wwwauth_configuration_html(request);
	});
	on("/update/updatepossible", [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		send_update_firmware_values_html(request);
	});
	on("/setmd5", [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		//DBG_OUTPUT_PORT.println("md5?");
		setUpdateMD5(request);
	});
	on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		if (!handleFileRead("/update.html", request))
			request->send(404, "text/plain", "FileNotFound");
	});
	on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (Update.hasError()) ? "FAIL" : "<META http-equiv=\"refresh\" content=\"15;URL=/update\">Update correct. Restarting...");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
		ESP.restart();
	}, updateFirmware);

	_ws->onEvent(webSocketEvent);
	addHandler(_ws);

	//called when the url is not defined here
	//use it to load content from SPIFFS
	onNotFound([](AsyncWebServerRequest *request) {
		DBG_OUTPUT_PORT.printf("Not found: %s\r\n", request->url().c_str());
		if (!ESPHTTPServer.checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(200);
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		if (!handleFileRead(request->url(), request))
			request->send(404, "text/plain", "FileNotFound");
	});

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
	server.begin();
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