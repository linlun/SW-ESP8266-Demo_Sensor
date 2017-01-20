#include <user_config.h>
#include "SmingCore.h"
#include <AppSettings.h>
#include <config.h>
#include "wifi_config.h"

// If you want, you can define WiFi settings globally in Eclipse Environment Variables
#ifndef WIFI_SSID
	#define WIFI_SSID "ssid" // Put you SSID and Password here
	#define WIFI_PWD "pass"
#endif



HttpServer server;
BssList networks;
String network, password;
String mqtt_client;
rBootHttpUpdate* otaUpdater = 0;

// Forward declarations
void startMqttClient();
void onMessageReceived(String topic, String message);
Timer procTimer;
Timer SendValueTimer;
// MQTT client
// For quick check you can use: http://www.hivemq.com/demos/websocket-client/ (Connection= test.mosquitto.org:8080)
MqttClient *mqtt;
bool messageSentByMe = false;
Timer indicationTimer;

#define INDICATION_DISCONNECTED	0u
#define INDICATION_CONNECTED	1u
#define INDICATION_LUNCH		2u
#define INDICATION_LUNCH_LATE	3u
uint8 indicationState = INDICATION_DISCONNECTED;


void ShowInfo() {
    Serial.printf("\r\nSDK: v%s\r\n", system_get_sdk_version());
    Serial.printf("Free Heap: %d\r\n", system_get_free_heap_size());
    Serial.printf("CPU Frequency: %d MHz\r\n", system_get_cpu_freq());
    Serial.printf("System Chip ID: %x\r\n", system_get_chip_id());
    Serial.printf("SPI Flash ID: %x\r\n", spi_flash_get_id());
    //Serial.printf("SPI Flash Size: %d\r\n", (1 << ((spi_flash_get_id() >> 16) & 0xff)));
}

void serialCallBack(Stream& stream, char arrivedChar, unsigned short availableCharsCount) {
	if (arrivedChar == '\n') {
		char str[availableCharsCount];
		for (int i = 0; i < availableCharsCount; i++) {
			str[i] = stream.read();
			if (str[i] == '\r' || str[i] == '\n') {
				str[i] = '\0';
			}
		}
		
		if (!strcmp(str, "connect")) {
			// connect to wifi
			WifiStation.config(WIFI_SSID, WIFI_PWD);
			WifiStation.enable(true);
		} else if (!strcmp(str, "ip")) {
			Serial.printf("ip: %s mac: %s\r\n", WifiStation.getIP().toString().c_str(), WifiStation.getMAC().c_str());
		} else if (!strcmp(str, "ota")) {
			//OtaUpdate();
		} else if (!strcmp(str, "switch")) {
			//Switch();
		} else if (!strcmp(str, "restart")) {
			System.restart();
		} else if (!strcmp(str, "ls")) {
			Vector<String> files = fileList();
			Serial.printf("filecount %d\r\n", files.count());
			for (unsigned int i = 0; i < files.count(); i++) {
				Serial.println(files[i]);
			}
		} else if (!strcmp(str, "cat")) {
			Vector<String> files = fileList();
			if (files.count() > 0) {
				Serial.printf("dumping file %s:\r\n", files[0].c_str());
				Serial.println(fileGetContent(files[0]));
			} else {
				Serial.println("Empty spiffs!");
			}
		} else if (!strcmp(str, "info")) {
			ShowInfo();
		} else if (!strcmp(str, "help")) {
			Serial.println();
			Serial.println("available commands:");
			Serial.println("  help - display this message");
			Serial.println("  ip - show current ip address");
			Serial.println("  connect - connect to wifi");
			Serial.println("  restart - restart the esp8266");
			//Serial.println("  switch - switch to the other rom and reboot");
			//Serial.println("  ota - perform ota update, switch rom and reboot");
			Serial.println("  info - show esp8266 info");
			Serial.println("  sl - Sleep for 5 seconds");
#ifndef DISABLE_SPIFFS
			Serial.println("  ls - list files in spiffs");
			Serial.println("  cat - show first file in spiffs");
#endif
			Serial.println();
		} else if (!strcmp(str, "sl")) {
			Serial.println("Going to sleep");
			delay(500);
			system_deep_sleep(5000000);
			delay(500);
			Serial.println("Wakeing up");
		} else {
			Serial.println("unknown command");
		}
	}
}

// Check for MQTT Disconnection
void checkMQTTDisconnect(TcpClient& client, bool flag){
	indicationState = INDICATION_DISCONNECTED;
	// Called whenever MQTT connection is failed.
	if (flag == true)
		Serial.println("MQTT Broker Disconnected!!");
	else
		Serial.println("MQTT Broker Unreachable!!");
	WifiAccessPoint.enable(true);
	// Restart connection attempt after few seconds
	procTimer.initializeMs(10 * 1000, startMqttClient).start(); // every 2 seconds
}
void SendValue()
{
	static float oldValue = 0;
	uint16 value = analogRead(17);
	Serial.println(value);
	float valuef = value/20.0;
	if (oldValue != valuef)
	{
	if (mqtt->getConnectionState() == eTCS_Connected)
		mqtt->publish(mqtt_client + "/SensorValue", String(valuef)); // or publishWithQoS
	}
	oldValue = valuef;
}

float currentSetTemp = 20;
void indicationFunction()
{
	static uint32 buttonCounter = 0;
	static uint32 counter = 0;
	static uint32 counter_short = 0;

	//Check Button
	if (digitalRead(PIN_BUTTON))
	{
		buttonCounter++;
		if (buttonCounter ==6)
		{
			currentSetTemp += 5.0;
			if (currentSetTemp > 45)
				currentSetTemp = 5;
			if (mqtt->getConnectionState() == eTCS_Connected)
				mqtt->publish(mqtt_client + "/SetValue", String(currentSetTemp)); // or publishWithQoS
		}
	} else {
		buttonCounter = 0;
	}

}

// Publish our message
void publishMessage()
{
	if (mqtt->getConnectionState() != eTCS_Connected)
		startMqttClient(); // Auto reconnect

	mqtt->publish(mqtt_client + "/Status", "Online"); // or publishWithQoS
}
bool heater;
float currentTemp;
// Callback for messages, arrived from MQTT server
void onMessageReceived(String topic, String message)
{
	if (topic.endsWith("Firmware"))
	{
		if (message.equals("UPDATE"))
		{
			//OtaUpdate();
		}
	}
	if (topic.endsWith("SensorValue"))
	{
		if (topic.equals(mqtt_client + "/SensorValue"))
		{
			// My own message

		} else {
			messageSentByMe = false;
		}
		currentTemp = message.toFloat();
		//currentTemp = value/20.0;

	}
	if (topic.endsWith("SetValue"))
	{
		if (topic.equals(mqtt_client + "/SetValue"))
		{
			// My own message

		} else {
			messageSentByMe = false;
		}
		currentSetTemp = message.toFloat();
		//currentTemp = value/20.0;

	}
	if (topic.endsWith("Heater"))
		{
			if (topic.equals(mqtt_client + "/Heater"))
			{
				// My own message

			} else {
				messageSentByMe = false;
			}
			if (message.equals("ON"))
			{
				heater = true;
				//indicationState = INDICATION_LUNCH;
		//		digitalWrite(PIN_GREEN,1); //Green
	//digitalWrite(PIN_BLUE,1); //Blue
	digitalWrite(PIN_RED,1); //Red
			}
			else
			{
				heater = false;
				//indicationState = INDICATION_CONNECTED;
				digitalWrite(PIN_GREEN,0); //Green
	digitalWrite(PIN_BLUE,0); //Blue
	digitalWrite(PIN_RED,0); //Red
			}
		}
	Serial.print(topic);
	Serial.print(":\r\n\t"); // Pretify alignment for printing
	Serial.println(message);
	WifiAccessPoint.enable(false);
}

// Run MQTT client

void startMqttClient()
{
	/*if (mqtt == null)
	{
		mqtt = new MqttClient(MQTT_HOST, MQTT_PORT, onMessageReceived);
	}
	*/
	procTimer.stop();
	mqtt_client = AppSettings.mqtt_nodeName + "_" + WifiStation.getMAC();
	if(!mqtt->setWill(mqtt_client + "/Status", "Offline", 1, true)) {
		debugf("Unable to set the last will and testament. Most probably there is not enough memory on the device.");
	}
	mqtt->connect(mqtt_client, MQTT_USERNAME, MQTT_PWD);
	indicationState = INDICATION_CONNECTED;
	// Assign a disconnect callback function
	mqtt->setCompleteDelegate(checkMQTTDisconnect);
	//mqtt->subscribe(mqtt_client + "/Lunch");
	mqtt->subscribe("+/SensorValue");
	mqtt->subscribe("+/Heater");
	mqtt->subscribe("+/SetValue");

	mqtt->subscribe("+/Firmware");
	mqtt->publish(mqtt_client + "/Status", "Online"); // or publishWithQoS
}
// Will be called when WiFi station was connected to AP
void connectOk()
{
	Serial.println("I'm CONNECTED");
//	ntpDemo = new ntpClientDemo();
	// Run MQTT client
	startMqttClient();
	WifiAccessPoint.enable(false);
	// Start publishing loop
	procTimer.initializeMs(20 * 1000, publishMessage).start(); // every 20 seconds
}

// Will be called when WiFi station timeout was reached
void connectFail()
{
	Serial.println("I'm NOT CONNECTED. Need help :(");
	WifiAccessPoint.enable(true);
	// .. some you code for device configuration ..
}

Timer connectionTimer;

void onIndex(HttpRequest &request, HttpResponse &response)
{
	TemplateFileStream *tmpl = new TemplateFileStream("index.html");
	auto &vars = tmpl->variables();
	response.sendTemplate(tmpl); // will be automatically deleted
}

void onIpConfig(HttpRequest &request, HttpResponse &response)
{
	if (request.getRequestMethod() == RequestMethod::POST)
	{
		AppSettings.dhcp = request.getPostParameter("dhcp") == "1";
		AppSettings.ip = request.getPostParameter("ip");
		AppSettings.netmask = request.getPostParameter("netmask");
		AppSettings.gateway = request.getPostParameter("gateway");
		debugf("Updating IP settings: %d", AppSettings.ip.isNull());
		AppSettings.save();
	}

	TemplateFileStream *tmpl = new TemplateFileStream("settings.html");
	auto &vars = tmpl->variables();

	bool dhcp = WifiStation.isEnabledDHCP();
	vars["dhcpon"] = dhcp ? "checked='checked'" : "";
	vars["dhcpoff"] = !dhcp ? "checked='checked'" : "";

	if (!WifiStation.getIP().isNull())
	{
		vars["ip"] = WifiStation.getIP().toString();
		vars["netmask"] = WifiStation.getNetworkMask().toString();
		vars["gateway"] = WifiStation.getNetworkGateway().toString();
	}
	else
	{
		vars["ip"] = "192.168.1.77";
		vars["netmask"] = "255.255.255.0";
		vars["gateway"] = "192.168.1.1";
	}

	response.sendTemplate(tmpl); // will be automatically deleted
}
void onMqttConfig(HttpRequest &request, HttpResponse &response)
{
	if (request.getRequestMethod() == RequestMethod::POST)
	{
		AppSettings.mqtt_password = request.getPostParameter("password");
		AppSettings.mqtt_user = request.getPostParameter("user");
		AppSettings.mqtt_server = request.getPostParameter("adr");
		AppSettings.mqtt_period = request.getPostParameter("period").toInt();
		AppSettings.mqtt_port = request.getPostParameter("port").toInt();
		AppSettings.mqtt_nodeName = request.getPostParameter("nodeName");
		//debugf("Updating MQTT settings: %d", AppSettings.ip.isNull());
		AppSettings.save();
	}

	TemplateFileStream *tmpl = new TemplateFileStream("mqttsettings.html");
	auto &vars = tmpl->variables();

	vars["user"] = AppSettings.mqtt_user;
	vars["password"] = AppSettings.mqtt_password;
	vars["period"] = AppSettings.mqtt_period;
	vars["port"] = AppSettings.mqtt_port;
	vars["adr"] = AppSettings.mqtt_server;
	vars["nodeName"] = AppSettings.mqtt_nodeName;

	response.sendTemplate(tmpl); // will be automatically deleted
}

void onOtaConfig(HttpRequest &request, HttpResponse &response)
{
	if (request.getRequestMethod() == RequestMethod::POST)
	{
		AppSettings.ota_ROM_0 = request.getPostParameter("rom0");
		AppSettings.ota_SPIFFS = request.getPostParameter("spiffs");
		AppSettings.save();
	}

	TemplateFileStream *tmpl = new TemplateFileStream("otasettings.html");
	auto &vars = tmpl->variables();

	vars["rom0"] = AppSettings.ota_ROM_0;
	vars["spiffs"] = AppSettings.ota_SPIFFS;

	response.sendTemplate(tmpl); // will be automatically deleted
}
void onFile(HttpRequest &request, HttpResponse &response)
{
	String file = request.getPath();
	if (file[0] == '/')
		file = file.substring(1);

	if (file[0] == '.')
		response.forbidden();
	else
	{
		response.setCache(86400, true); // It's important to use cache for better performance.
		response.sendFile(file);
	}
}

void onAjaxNetworkList(HttpRequest &request, HttpResponse &response)
{
	JsonObjectStream* stream = new JsonObjectStream();
	JsonObject& json = stream->getRoot();

	json["status"] = (bool)true;

	bool connected = WifiStation.isConnected();
	json["connected"] = connected;
	if (connected)
	{
		// Copy full string to JSON buffer memory
		json["network"]= WifiStation.getSSID();
	}

	JsonArray& netlist = json.createNestedArray("available");
	for (int i = 0; i < networks.count(); i++)
	{
		if (networks[i].hidden) continue;
		JsonObject &item = netlist.createNestedObject();
		item["id"] = (int)networks[i].getHashId();
		// Copy full string to JSON buffer memory
		item["title"] = networks[i].ssid;
		item["signal"] = networks[i].rssi;
		item["encryption"] = networks[i].getAuthorizationMethodName();
	}

	response.setAllowCrossDomainOrigin("*");
	response.sendJsonObject(stream);
}
void onAjaxRunOta(HttpRequest &request, HttpResponse &response)
{
	//OtaUpdate();
}
void makeConnection()
{
	WifiStation.enable(true);
	WifiStation.config(network, password);

	AppSettings.ssid = network;
	AppSettings.password = password;
	AppSettings.save();

	network = ""; // task completed
}

void onAjaxConnect(HttpRequest &request, HttpResponse &response)
{
	JsonObjectStream* stream = new JsonObjectStream();
	JsonObject& json = stream->getRoot();

	String curNet = request.getPostParameter("network");
	String curPass = request.getPostParameter("password");

	bool updating = curNet.length() > 0 && (WifiStation.getSSID() != curNet || WifiStation.getPassword() != curPass);
	bool connectingNow = WifiStation.getConnectionStatus() == eSCS_Connecting || network.length() > 0;

	if (updating && connectingNow)
	{
		debugf("wrong action: %s %s, (updating: %d, connectingNow: %d)", network.c_str(), password.c_str(), updating, connectingNow);
		json["status"] = (bool)false;
		json["connected"] = (bool)false;
	}
	else
	{
		json["status"] = (bool)true;
		if (updating)
		{
			network = curNet;
			password = curPass;
			debugf("CONNECT TO: %s %s", network.c_str(), password.c_str());
			json["connected"] = false;
			connectionTimer.initializeMs(1200, makeConnection).startOnce();
		}
		else
		{
			json["connected"] = WifiStation.isConnected();
			debugf("Network already selected. Current status: %s", WifiStation.getConnectionStatusName());
		}
	}

	if (!updating && !connectingNow && WifiStation.isConnectionFailed())
		json["error"] = WifiStation.getConnectionStatusName();

	response.setAllowCrossDomainOrigin("*");
	response.sendJsonObject(stream);
}

void startWebServer()
{
	server.listen(80);
	server.addPath("/", onIndex);
	server.addPath("/ipconfig", onIpConfig);
	server.addPath("/mqttconfig", onMqttConfig);
	//server.addPath("/ota", onOtaConfig);
	server.addPath("/ajax/get-networks", onAjaxNetworkList);
	//server.addPath("/ajax/run-ota", onAjaxRunOta);
	server.addPath("/ajax/connect", onAjaxConnect);
	server.setDefaultHandler(onFile);
}


void networkScanCompleted(bool succeeded, BssList list)
{
	if (succeeded)
	{
		for (int i = 0; i < list.count(); i++)
			if (!list[i].hidden && list[i].ssid.length() > 0)
				networks.add(list[i]);
	}
	networks.sort([](const BssInfo& a, const BssInfo& b){ return b.rssi - a.rssi; } );
}
void init() {
	pinMode(PIN_BUTTON, INPUT);
	pinMode(PIN_GREEN, OUTPUT);
	pinMode(PIN_BLUE, OUTPUT);
	pinMode(PIN_RED, OUTPUT);
	digitalWrite(PIN_GREEN,1); //Green
	digitalWrite(PIN_BLUE,0); //Blue
	digitalWrite(PIN_RED,0); //Red

	indicationTimer.initializeMs(50, indicationFunction).start(); // every 20 seconds
	Serial.begin(SERIAL_BAUD_RATE); // 115200 by default
	Serial.systemDebugOutput(false); // Debug output to serial

	
    spiffs_mount(); // Mount file system, in order to work with files
	AppSettings.load();

	WifiAccessPoint.enable(false);
	// connect to wifi
	WifiStation.config(WIFI_SSID_2, WIFI_PWD_2);
	WifiStation.enable(true);
	
	if (AppSettings.exist())
	{
		WifiStation.config(AppSettings.ssid, AppSettings.password);
		if (!AppSettings.dhcp && !AppSettings.ip.isNull())
			WifiStation.setIP(AppSettings.ip, AppSettings.netmask, AppSettings.gateway);
	}
	mqtt = new MqttClient(AppSettings.mqtt_server,AppSettings.mqtt_port, onMessageReceived);

	WifiStation.startScan(networkScanCompleted);

	// Start AP for configuration
	WifiAccessPoint.enable(true);
	WifiAccessPoint.config("Sming Configuration "+ WifiStation.getMAC(), "", AUTH_OPEN);

	// Run WEB server
	startWebServer();
	
	
	Serial.println("Type 'help' and press enter for instructions.");
	Serial.println();
	
	Serial.setCallback(serialCallBack);
	// Run our method when station was connected to AP (or not connected)
	WifiStation.waitConnection(connectOk, 20, connectFail); // We recommend 20+ seconds for connection timeout at start

	SendValueTimer.initializeMs(500, SendValue).start(); // every 20 seconds

}
