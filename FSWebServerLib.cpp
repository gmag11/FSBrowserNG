// 
// 
// 

#define DEBUG

#define DBG_OUTPUT_PORT Serial
#define CONNECTION_LED 2 // Connection LED pin (Built in)
#define AP_ENABLE_BUTTON 4 // Button pin to enable AP during startup for configuration

#include "FSWebServerLib.h"

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
	ws->textAll(time);
	String date = "D" + NTP.getDateStr();
	ws->textAll(date);
	String sync = "S" + NTP.getTimeDateString(NTP.getLastNTPSync());
	ws->textAll(sync);
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
	WiFi.hostname(_config.DeviceName.c_str());
	if (AP_ENABLE_BUTTON >= 0) {
		if (_apConfig.APenable) {
			ConfigureWifiAP(); // Set AP mode if AP button was pressed
		}
		else {
			ConfigureWifi(); // Set WiFi config
		}
	}
	else {
		ConfigureWifi(); // Set WiFi config
	}

#ifdef DEBUG
	DBG_OUTPUT_PORT.print("Open http://");
	DBG_OUTPUT_PORT.print(_config.DeviceName);
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
	_secondTk.attach(1, secondTick, (void *)this); // Task to run periodic things every second
	*ws = AsyncWebSocket("/ws");
	serverInit(); // Configure and start Web server
	AsyncWebServer::begin();

	MDNS.begin(_config.DeviceName.c_str()); // I've not got this to work. Need some investigation.
	MDNS.addService("http", "tcp", 80);
	ConfigureOTA(_httpAuth.wwwPassword.c_str());

#ifdef DEBUG
	DBG_OUTPUT_PORT.println("END Setup");
#endif // DEBUG


}

boolean AsyncFSWebServer::load_config() {
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
	_config.DeviceName = json["deviceName"].asString();

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
	_config.DeviceName = "ESP8266fs";
	//config.connectionLed = CONNECTION_LED;
	save_config();
#ifdef DEBUG
	DBG_OUTPUT_PORT.println(__PRETTY_FUNCTION__);
#endif // DEBUG
}

boolean AsyncFSWebServer::save_config() {
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
	json["deviceName"] = _config.DeviceName;

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



void AsyncFSWebServer::handlePeriodic()
{
	if (_secondFlag) { // Run periodic tasks
		_secondFlag = false;
		secondTask();
	}
	ArduinoOTA.handle();
}

