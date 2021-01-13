#include <String>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>
#include <CommonFunctionsWiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

ESP8266WiFiMulti wifiMulti;       // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
ESP8266WebServer server(80);       // create a web server on port 80
WebSocketsServer  webSocket(81);    // create a websocket server on port 81
File fsUploadFile;                                    // a File variable to temporarily store the received file
WiFiUDP ntpUDP;				//connection to ntp servers
NTPClient timeClient(ntpUDP, "pool.ntp.org");

const char *ssid = "PLUSNET-F97M"; // The name of the Wi-Fi network that will be created
const char *password = "NKWJKW4F";   // The password required to connect to it, leave blank for an open network
const char *ap_ssid = "RGB_STRIP";
const char *ap_password = "rgbstrip";
const char *OTAName = "esp8266";           // A name and a password for the OTA service
const char *OTAPassword = "esp8266";
const uint16_t OTAPort = 8266;
String onWiFiTime = "16:30";	//HH:MM
String offWiFiTime = "01:00";

#define ENABLE_SERIAL
#define LED_RED     14            // specify the pins with an RGB LED connected
#define LED_GREEN   4
#define LED_BLUE    13
#define LED_INDICATOR 2

volatile int red=0, green=0, blue=0;
volatile int prevRed=0, prevGreen=0, prevBlue=0;
//const char* mdnsName = "esp8266"; // Domain name for the mDNS responder

volatile bool rainbow = false;             // The rainbow effect is turned off on startup
volatile bool pulse = 0;
unsigned long prevMillis = millis(), configFileUpdateTime = millis();
float hue = 0;
float brightness = 0.98f;
float brt_val = 0.004f; 
volatile float fade_direction = 1.0f;
uint32_t sleepTime = 0;
uint32_t lastSleep = 0;

/*__________________________________________________________HELPER_FUNCTIONS__________________________________________________________*/
String formatBytes(size_t bytes) { // convert sizes in bytes to KB and MB
	if (bytes < 1024) {
		return String(bytes) + "B";
	}
	else if (bytes < (1024 * 1024)) {
		return String(bytes / 1024.0) + "KB";
	}
	else if (bytes < (1024 * 1024 * 1024)) {
		return String(bytes / 1024.0 / 1024.0) + "MB";
	}
	else{
		return String(bytes) + "B";
		}
}

String getContentType(String filename) { // determine the filetype of a given filename, based on the extension
	if (filename.endsWith(".html")) return "text/html";
	else if (filename.endsWith(".css")) return "text/css";
	else if (filename.endsWith(".js")) return "application/javascript";
	else if (filename.endsWith(".ico")) return "image/x-icon";
	else if (filename.endsWith(".gz")) return "application/x-gzip";
	return "text/plain";
}

void setHue(float hue) { // Set the RGB LED to a given hue (color) (0� = Red, 120� = Green, 240� = Blue)
	                 // hue is an angle between 0 and 359�
	float radH = hue * 3.142 / 180;   // Convert degrees to radians
	float rf = 0, gf = 0, bf = 0;

	if (hue >= 0 && hue < 120) {        // Convert from HSI color space to RGB              
		rf = cos(radH * 3 / 4);
		gf = sin(radH * 3 / 4);
		bf = 0;
	}
	else if (hue >= 120 && hue < 240) {
		radH -= 2.09439;
		gf = cos(radH * 3 / 4);
		bf = sin(radH * 3 / 4);
		rf = 0;
	}
	else if (hue >= 240 && hue < 360) {
		radH -= 4.188787;
		bf = cos(radH * 3 / 4);
		rf = sin(radH * 3 / 4);
		gf = 0;
	}
	int r = rf * rf * 1023;
	int g = gf * gf * 1023;
	int b = bf * bf * 1023;

	analogWrite(LED_RED, r);    // Write the right color to the LED output pins
	analogWrite(LED_GREEN, g);
	analogWrite(LED_BLUE, b);
}
/*__________________________________________________________SERVER_HANDLERS__________________________________________________________*/

void disconnectWiFi() {
	WiFi.mode(WIFI_OFF);
	WiFi.disconnect();
	delay(1);
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
	#ifdef ENABLE_SERIAL
	Serial.println("handleFileRead: " + path);
	#endif
	if (path.endsWith("/")) path += "index.html";          // If a folder is requested, send the index file
	String contentType = getContentType(path);             // Get the MIME type
	String pathWithGz = path + ".gz";
	if (LittleFS.exists(pathWithGz) || LittleFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
		if (LittleFS.exists(pathWithGz))                         // If there's a compressed version available
			path += ".gz";                                         // Use the compressed verion
		File file = LittleFS.open(path, "r");                    // Open the file
		//size_t sent = server.streamFile(file, contentType);    // Send it to the client
		server.streamFile(file, contentType);    // Send it to the client
		file.close();                                          // Close the file again
	#ifdef ENABLE_SERIAL
		Serial.println(String("\tSent file: ") + path);
		#endif
		return true;
	}
	#ifdef ENABLE_SERIAL
	Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
	#endif
	return false;
}

void handleNotFound() { // if the requested file or page doesn't exist, return a 404 not found error
	if (!handleFileRead(server.uri())) {          // check if the file exists in the flash memory (LittleFS), if so, send it
		server.send(404, "text/plain", "404: File Not Found");
	}
}

void handleFileUpload() { // upload a new file to the LittleFS
	HTTPUpload& upload = server.upload();
	String path;
	if (upload.status == UPLOAD_FILE_START) {
		path = upload.filename;
		if (!path.startsWith("/")) path = "/" + path;
		if (!path.endsWith(".gz")) {                          // The file server always prefers a compressed version of a file 
			String pathWithGz = path + ".gz";                    // So if an uploaded file is not compressed, the existing compressed
			if (LittleFS.exists(pathWithGz))                      // version of that file must be deleted (if it exists)
				LittleFS.remove(pathWithGz);
		}
	#ifdef ENABLE_SERIAL
		Serial.print("handleFileUpload Name: "); Serial.println(path);
	#endif
		fsUploadFile = LittleFS.open(path, "w");            // Open the file for writing in LittleFS (create if it doesn't exist)
		path = String();
	}
	else if (upload.status == UPLOAD_FILE_WRITE) {
		if (fsUploadFile)
			fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
	}
	else if (upload.status == UPLOAD_FILE_END) {
		if (fsUploadFile) {                                    // If the file was successfully created
			fsUploadFile.close();                               // Close the file again			
		#ifdef ENABLE_SERIAL
			Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
		#endif
			server.sendHeader("Location", "/success.html");      // Redirect the client to the success page
			server.send(303);
		}
		else {
			server.send(500, "text/plain", "500: couldn't create file");
		}
	}
}

//create/modify startup config file.  
void saveConfigToFS()
{
	File configFile = LittleFS.open("/config.txt", "w");
	if (!configFile) return;
	configFile.printf("%d,%d,%d\n", red, green, blue);		//write current rgb values
	//time sleep, time wake,
	//ap ssid, passw
	//sta ssid, passw
	configFile.close();
}
//upon startup, the esp will initialize with values set in config file. 
bool loadConfigFromFS()
{
	fs::File configFile = LittleFS.open("/config.txt", "r");
	if (!configFile) 	{
		Serial.println("Config file not found.");
		return false;
	}
	char line[30];
	configFile.readBytesUntil('\n', line, 30);
	char* token = strtok(line, ",");
	red = atoi(token);
	token= strtok(0, ",");
	green = atoi(token);
	token = strtok(0, ",");
	blue = atoi(token);
	configFile.close();
	return true;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) { // When a WebSocket message is received
	switch (type) {
	case WStype_DISCONNECTED:             // if the websocket is disconnected
	#ifdef ENABLE_SERIAL
		Serial.printf("[%u] Disconnected!\n", num);
	#endif
		break;
	case WStype_CONNECTED: {              // if a new websocket connection is established
		IPAddress ip = webSocket.remoteIP(num);
	#ifdef ENABLE_SERIAL
		Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
	#endif
		rainbow = false;                  // Turn rainbow off when a new connection is established
	}
		 break;
	case WStype_TEXT:                     // if new text data is received
	#ifdef ENABLE_SERIAL
		Serial.printf("[%u] get Text: %s\n", num, payload);
	#endif
		if (payload[0] == '#') {            // we get RGB data
			uint32_t rgb = (uint32_t)strtol((const char *)&payload[1], NULL, 16);   // decode rgb data
			red = ((rgb >> 20) & 0x3FF);                     // 10 bits per color, so R: bits 20-29
			green = ((rgb >> 10) & 0x3FF);                     // G: bits 10-19
			blue = rgb & 0x3FF;                      // B: bits  0-9
			if (!pulse)	{
				analogWrite(LED_RED, red);                         // write it to the LED output pins
				analogWrite(LED_GREEN, green);
				analogWrite(LED_BLUE, blue);
				//saveConfigToFS();	//no longer save every time, only at specific intervals
			}
		}
		else if (payload[0] == 'R') {                      // the browser sends an R when the rainbow effect is enabled
			rainbow = true;
			pulse = 0;
		}
		else if (payload[0] == 'P')	{
			pulse = 1;
			rainbow = false;
		}
		else if (payload[0] == 'N') {                      // the browser sends an N when the rainbow effect is disabled
			rainbow = false;
			pulse = 0;
		}
		break;
	default:
		break;
	}
}

/*__________________________________________________________SETUP_FUNCTIONS__________________________________________________________*/
//put uc to sleep at certain times. 
void sleepLoop()
{
	int onHours = onWiFiTime.substring(0, 2).toInt(); 
	int onMinutes = onWiFiTime.substring(3, 5).toInt(); 
	int offHours = offWiFiTime.substring(0, 2).toInt(); 
	int offMinutes = offWiFiTime.substring(3, 5).toInt(); 
	int nowHours = timeClient.getHours();
	bool isWithinOnTime = (nowHours == onHours && timeClient.getMinutes() >= onMinutes);
	bool isWithinOffTime = (nowHours == offHours && timeClient.getMinutes() <= offMinutes);
	if (nowHours  > onHours || nowHours < offHours || isWithinOnTime || isWithinOffTime){
		//turn on Wifi
	}
	else
	{
		//disable lighting
		digitalWrite(LED_RED, 0);
		digitalWrite(LED_GREEN, 0);
		digitalWrite(LED_BLUE, 0);
		//Go to Sleep
		wifi_station_disconnect();
		WiFi.forceSleepBegin(1000000*120);
		//wifi_set_opmode_current(NULL_MODE);
		//wifi_fpm_set_sleep_type(LIGHT_SLEEP_T); // set sleep type, the above    posters wifi_set_sleep_type() didnt seem to work for me although it did let me compile and upload with no errors 
		//wifi_fpm_do_sleep(1000000*60);	//sleep for 60s

		wifi_station_connect();
		WiFi.forceSleepWake();
		delay(60000);
	}
	
}


void startWiFi() { // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
	WiFi.mode(WIFI_AP_STA);
	delay(1);
	WiFi.softAP(ap_ssid, ap_password);             // Start the access point
	wifiMulti.addAP(ssid, password);   // add Wi-Fi networks you want to connect to
	#ifdef ENABLE_SERIAL
	Serial.println("Connecting");
	#endif
	int tries = 10;
	while (wifiMulti.run() != WL_CONNECTED && WiFi.softAPgetStationNum() < 1 && tries > 0) {  // Wait for the Wi-Fi to connect
		delay(250);
		--tries;
		Serial.print('.');
	}
	digitalWrite(LED_GREEN, 1);
	delay(200);
	digitalWrite(LED_GREEN, 0);
	delay(10);

	if (WiFi.softAPgetStationNum() == 0) {      // If the ESP is connected to an AP
	#ifdef ENABLE_SERIAL
		Serial.print("Connected to ");
		Serial.println(WiFi.SSID());             // Tell us what network we're connected to
		Serial.print("IP address:\t");
		Serial.print(WiFi.localIP());            // Send the IP address of the ESP8266 to the computer
	#endif
	}
	else {                                   // If a station is connected to the ESP SoftAP
	#ifdef ENABLE_SERIAL
		Serial.print("Station connected to ESP8266 AP");
	#endif
	}
}

void startOTA() { // Start the OTA service
	ArduinoOTA.setHostname(OTAName);
	//ArduinoOTA.setPassword(OTAPassword);		//no ota password authentication
	ArduinoOTA.setPort(OTAPort);

	ArduinoOTA.onStart([]() {
	});
	ArduinoOTA.onEnd([]() {
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
	#ifdef ENABLE_SERIAL
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
	#endif
	});
	ArduinoOTA.onError([](ota_error_t error) {
	#ifdef ENABLE_SERIAL
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
	#endif
	});
	ArduinoOTA.begin();
	#ifdef ENABLE_SERIAL
	Serial.println("OTA ready\r\n");
	#endif
}

void startLittleFS() { // Start the LittleFS and list all contents
	LittleFS.begin();                             // Start the SPI Flash File System (LittleFS)
	#ifdef ENABLE_SERIAL
	Serial.println("LittleFS started. Contents:");
	#endif // ENABLE_SERIAL
	{
		Dir dir = LittleFS.openDir("/");
		while (dir.next()) {                      // List the file system contents
			String fileName = dir.fileName();
			size_t fileSize = dir.fileSize();
	#ifdef ENABLE_SERIAL
			Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
	#endif// ENABLE_SERIAL
		}
	}
}

void startWebSocket() { // Start a WebSocket server
	webSocket.begin();                          // start the websocket server
	webSocket.onEvent(webSocketEvent);          // if there's an incomming websocket message, go to function 'webSocketEvent'
}

void startServer() { // Start a HTTP server with a file read handler and an upload handler
	//handle a uri
	// Uri page("/p");
	// server.on(page, []()
	// {
	// 	handleFileRead("/index.html");
	// });
	server.onNotFound(handleNotFound);          // if someone requests any other file or page, go to function 'handleNotFound'
	server.begin();                             // start the HTTP server
}

/*__________________________________________________________SETUP__________________________________________________________*/
void setup() {
	pinMode(LED_RED, OUTPUT);    // the pins with LEDs connected are outputs
	pinMode(LED_GREEN, OUTPUT);
	pinMode(LED_BLUE, OUTPUT);
	pinMode(LED_INDICATOR, OUTPUT);
	//startup indication
	digitalWrite(LED_RED, 1);
	digitalWrite(LED_GREEN, 1);
	digitalWrite(LED_BLUE, 1);
	delay(500);
	digitalWrite(LED_INDICATOR, 0);
	digitalWrite(LED_RED, 0);
	digitalWrite(LED_GREEN, 0);
	digitalWrite(LED_BLUE, 0);

	#ifdef ENABLE_SERIAL
	Serial.begin(115200);        // Start the Serial communication to send messages to the computer
	#endif // ENABLE_SERIAL
	delay(10);
	startWiFi();                 // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
	startOTA();                  // Start the OTA service
	
  	timeClient.begin();
	startLittleFS();               // Start the LittleFS and list all contents
	startWebSocket();            // Start a WebSocket server
	MDNS.begin("rgbstrip");             // Start the mDNS responder
	MDNS.setHostname("rgbstrip");
	startServer();               // Start a HTTP server with a file read handler and an upload handler
	if (loadConfigFromFS())	//load configuration from file and initate leds. 
	{
		analogWrite(LED_RED, red);
		analogWrite(LED_GREEN, green);
		analogWrite(LED_BLUE, blue);
	}
}

/*__________________________________________________________LOOP__________________________________________________________*/
void loop() {
	webSocket.loop();                           // constantly check for websocket events
	server.handleClient();                      // run the server
  	MDNS.update();
	ArduinoOTA.handle();                        // listen for OTA events
	timeClient.update();
	//sleepLoop();
	                            // if the rainbow effect is turned on
	if (rainbow && millis() > prevMillis + 32) {
		hue += 0.21322f;
		if (hue >= 360)                        // Cycle through the color wheel (increment by one degree every 32 ms)
			hue = 0;
		setHue(hue);                            // Set the RGB LED to the right color
		prevMillis = millis();
	}
	if (pulse && millis() >= prevMillis +32) 
	{
		//decrease brightness.
		if (brightness < 0.15f) fade_direction = 1.0f;
		else if ( brightness > 1.0f) fade_direction = -1.0f;	//reverse direction
		brightness += (brt_val * fade_direction);
		analogWrite(LED_RED, int((float)red * brightness));                         // write it to the LED output pins
		analogWrite(LED_GREEN, int((float)green * brightness));
		analogWrite(LED_BLUE, int((float)blue * brightness));
		prevMillis = millis();
	}

	if (millis() >= configFileUpdateTime)
	{
		configFileUpdateTime = millis() + 200;	//check every 200 ms 
		if (red != prevRed || green != prevGreen || blue != prevBlue )		//if colours changed
		{
			saveConfigToFS();		//save to file
			prevRed = red;
			prevGreen = green;
			prevBlue = blue;
		}
	}

}
