#include <WiFi.h>
#include <time.h>
#include <FirebaseClient.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <WebSocketsServer_Generic.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>

#include "index.h"
#include "configPage.h"

#define databaseUrl "omnisense-17447-default-rtdb.asia-southeast1.firebasedatabase.app"
const String apiKey = "AIzaSyDI30Fd3AtxjZfCPC0QnaaQ68lUWe1_eK0";

const char* ntpServer = "pool.ntp.org";

struct Pin {
	uint8_t pin;
	String type;
	bool previousState;
	unsigned long lastDebounceTime = 0;
};

struct Sensor {
	String name;
	Pin pin;
};

struct Schedule {
  String days[7];
  String from;
  String to;
};

struct Device {
	String name;
	bool sensorMode;
	bool scheduleMode;
	Sensor sensor;
  Schedule schedule;
};

std::map<String, Device> devicesMap;

void asyncCB(AsyncResult &aResult);
void asyncCB1(AsyncResult &aResult);

DefaultNetwork network;
WiFiClientSecure ssl_client, ssl_client1, ssl_client2, ssl_client3;

using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client1, getNetwork(network)), aClient1(ssl_client3, getNetwork(network)), aClient2(ssl_client2, getNetwork(network));

FirebaseApp app;
RealtimeDatabase Database;

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

bool taskListenerReady = false;

bool isAuthenticated = false;
bool isConfigured = false;

String instancePath = "Default";
std::vector<String> instances;

uint8_t sensorPins[30]; 
bool pinsReady = false;

void handleAuth() {
	if (isConfigured) {
		server.send(200, "text/html", authPage);
	} else {
		server.send(200," text/html", configPage);
	}
}

void handleConfigureWifi() {
 	String body = server.arg("plain");
	
	StaticJsonDocument<200> config;
	deserializeJson(config, body);

	const char* ssid = config["ssid"];
	const char* password = config["password"];

	Serial.printf("[Omnisense Sensor] [Config] [WiFi] [SSID] %s\n", ssid);

	WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  byte tries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    if (tries++ > 30) {
      WiFi.mode(WIFI_AP);
      WiFi.softAP("omnisense-sensor", "config-it");
    	server.send(401, "Connection failed.");
      break;
    }
	}

	Serial.print("[Omnisense Sensor] [Wi-Fi] [IP] ");
	Serial.println(WiFi.localIP());

  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  Serial.println("[Omnisense Sensor] [NTP] [Time] Synchronizing...");
  struct tm timeInfo;
  while (!getLocalTime(&timeInfo)) {
  	Serial.println("[Omnisense Sensor] [NTP] [Time] Synchronization failed. Retrying...");
    delay(1000);
  }

  Serial.println("[Omnisense Sensor] [NTP] [Time] Synchronized.");

	isConfigured = true;
	if (MDNS.begin("omnisense-sensor")) Serial.println("[Omnisense Sensor] [MDNS] started.");
	MDNS.addService("http", "tcp", 80);

	server.send(200, "Success");
}

void asyncCB(AsyncResult &aResult) {
	if (aResult.available()) {
		RealtimeDatabaseResult &RTDB = aResult.to<RealtimeDatabaseResult>();
		if (RTDB.isStream()) {
			Serial.printf("[Omnisense Sensor] [%s] ----------------------------\n", aResult.uid().c_str());
			Firebase.printf("[Omnisense Sensor] [%s] [task] %s\n", aResult.uid().c_str(), aResult.uid().c_str());
			Firebase.printf("[Omnisense Sensor] [%s] [event] %s\n",aResult.uid().c_str(), RTDB.event().c_str());
			Firebase.printf("[Omnisense Sensor] [%s] [path] %s\n", aResult.uid().c_str(), RTDB.dataPath().c_str());
			Firebase.printf("[Omnisense Sensor] [%s] [data] %s\n", aResult.uid().c_str(), RTDB.to<const char *>());
			if (RTDB.event().c_str() != "keep-alive") {
				toggleRelay(RTDB.to<const char *>(), String(RTDB.dataPath().c_str()));
			}
		}
	}
	Firebase.printf("[Omnisense Sensor] [Heap]: %d\n", ESP.getFreeHeap());
}

void asyncCB1(AsyncResult &aResult) {
	if (aResult.available()) {
		RealtimeDatabaseResult &RTDB = aResult.to<RealtimeDatabaseResult>();
		if (RTDB.isStream()) {
			Serial.printf("[Omnisense Sensor] [%s] ----------------------------\n", aResult.uid().c_str());
			Firebase.printf("[Omnisense Sensor] [%s] [task] %s\n", aResult.uid().c_str(), aResult.uid().c_str());
			Firebase.printf("[Omnisense Sensor] [%s] [event] %s\n",aResult.uid().c_str(), RTDB.event().c_str());
			Firebase.printf("[Omnisense Sensor] [%s] [path] %s\n", aResult.uid().c_str(), RTDB.dataPath().c_str());
			Firebase.printf("[Omnisense Sensor] [%s] [data] %s\n", aResult.uid().c_str(), RTDB.to<const char *>());
			if (RTDB.event().c_str() != "keep-alive") {
				setInstances(RTDB.to<const char *>());
			}
		}
	}
	Firebase.printf("[Omnisense Sensor] [Heap]: %d\n", ESP.getFreeHeap());
}

void toggleRelay(const char* serializedDoc, String dataPath) {
  DynamicJsonDocument deserializedDoc(1024);
  deserializeJson(deserializedDoc, serializedDoc);

	if (serializedDoc == "null") {
		devicesMap.erase(dataPath);
		return;
	}

	if (dataPath != "/" && !devicesMap.count(dataPath) > 0) {
		JsonObject object = deserializedDoc.as<JsonObject>(); 
		Device device;

		device.name = object["name"].as<String>();
		device.sensorMode = object["sensorMode"].as<bool>();
		device.scheduleMode = object["scheduleMode"].as<bool>();

		Schedule schedule;
		if (object.containsKey("schedule")) {
			JsonObject scheduleObj = object["schedule"].as<JsonObject>();

			JsonArray daysArray = scheduleObj["days"].as<JsonArray>();
			int dayCount = min((size_t)7, daysArray.size());
			
			for (int i = 0; i < dayCount; i++) {
				schedule.days[i] = daysArray[i].as<String>();
			}

			schedule.from = scheduleObj["from"].as<String>();
			schedule.to = scheduleObj["to"].as<String>();
		} else {
			schedule.from = "";
			schedule.to = "";
		}
		device.schedule = schedule;

		Sensor sensor;
		if (object.containsKey("sensor")) {
			JsonObject sensorObj = object["sensor"].as<JsonObject>();
			Sensor sensor;

			sensor.name = sensorObj["name"].as<String>();

			uint8_t pin = sensorObj["pin"].as<int>();
			sensor.pin.pin = pin;
			sensor.pin.previousState = object["state"].as<bool>();

			pinMode(pin, INPUT);
		} else {
			Device device;
			device.name = object["name"].as<String>();
		}
		device.sensor = sensor;

		String path = "/" + String(dataPath);
		devicesMap[path] = device;

		return;
	}

	if (dataPath == "/") {
		JsonObject rootObject = deserializedDoc.as<JsonObject>();
		
		for (JsonPair kvPair : rootObject) {
			const char* key = kvPair.key().c_str();
			JsonObject object = kvPair.value().as<JsonObject>(); 

			Serial.print("Processing device with ID: ");
			Serial.println(key);

			Device device;
			device.name = object["name"].as<String>();
			device.sensorMode = object["sensorMode"].as<bool>();
			device.scheduleMode = object["scheduleMode"].as<bool>();

			Schedule schedule;
			if (object.containsKey("schedule")) {
				JsonObject scheduleObj = object["schedule"].as<JsonObject>();

				JsonArray daysArray = scheduleObj["days"].as<JsonArray>();
				int dayCount = min((size_t)7, daysArray.size());
				
				for (int i = 0; i < dayCount; i++) {
					schedule.days[i] = daysArray[i].as<String>();
				}

				schedule.from = scheduleObj["from"].as<String>();
				schedule.to = scheduleObj["to"].as<String>();
			} else {
				schedule.from = "";
				schedule.to = "";
			}
			device.schedule = schedule;

			Sensor sensor;
			if (object.containsKey("sensor")) {
				JsonObject sensorObj = object["sensor"].as<JsonObject>();

				sensor.name = sensorObj["name"].as<String>();

				uint8_t pin = sensorObj["pin"].as<int>();
				sensor.pin.pin = pin;
				sensor.pin.previousState = object["state"].as<bool>();

				pinMode(pin, INPUT);
			} else {
				sensor.name = "";
			}
			device.sensor = sensor;

			String path = "/" + String(key);
			devicesMap[path] = device;
		}
		pinsReady = true;
  } else {
		if (deserializedDoc.containsKey("schedule")) {
			JsonObject scheduleObj = deserializedDoc["schedule"].as<JsonObject>();

			JsonArray daysArray = scheduleObj["days"].as<JsonArray>();
			int dayCount = min((size_t)7, daysArray.size());
			
			for (int i = 0; i < dayCount; i++) {
				devicesMap[dataPath].schedule.days[i] = daysArray[i].as<String>();
			}

			devicesMap[dataPath].schedule.from = scheduleObj["from"].as<String>();
			devicesMap[dataPath].schedule.to = scheduleObj["to"].as<String>();
		}

    if (deserializedDoc.containsKey("state")) {
			devicesMap[dataPath].sensor.pin.previousState = deserializedDoc["state"];
		}

		if (deserializedDoc.containsKey("sensor")) {
			JsonObject sensorObj = deserializedDoc["sensor"].as<JsonObject>();

			if (sensorObj.containsKey("name")) {
				devicesMap[dataPath].sensor.name = sensorObj["name"].as<String>();
			}

			if (sensorObj.containsKey("pin")) {
				uint8_t pin = sensorObj["pin"].as<int>();
				devicesMap[dataPath].sensor.pin.pin = pin;
			}
		}
		
		if (deserializedDoc.containsKey("name")) {
			devicesMap[dataPath].name = deserializedDoc["name"].as<String>();
		}

		if (deserializedDoc.containsKey("sensorMode")) {
			devicesMap[dataPath].sensorMode = deserializedDoc["sensorMode"];
		}

		if (deserializedDoc.containsKey("scheduleMode")) {
			devicesMap[dataPath].scheduleMode = deserializedDoc["scheduleMode"];
		}
  } 
}

void setInstances(const char* serializedDoc) {
	StaticJsonDocument<512> doc;
	deserializeJson(doc, serializedDoc);

	instances.clear();

	if (doc.is<JsonArray>()) {
		JsonArray jsonArray = doc.as<JsonArray>();
		for (JsonVariant value : jsonArray) {
			instances.push_back(value.as<String>());
		}
	} else {
		if (doc.is<JsonObject>()) {
			for (JsonPair kv : doc.as<JsonObject>()) {
				instances.push_back(kv.value().as<String>());
				Serial.printf("[Omnisense Sensor] [New Instance Added]: %s\n", kv.value().as<String>().c_str());
				break;
			}
		} else {
			Serial.println("[Omnisense Sensor] [Error] Expected JSON object for new instance.");
		}
		return;
	}

	StaticJsonDocument<512> sendDoc;
	JsonArray broadcastArray = sendDoc.createNestedArray("instances");

	for (const auto& instance : instances) {
		broadcastArray.add(instance);
	}

	String message;
	serializeJson(sendDoc, message);
	webSocket.broadcastTXT(message);

	Serial.printf("[Omnisense Sensor] [WebSocket] Sent instances to clients: %s\n", message.c_str());
}

void authenticateUser(const String &apiKey, const String &email, const String &password, uint8_t num) {
	if (isAuthenticated) return;
	if (ssl_client.connected()) ssl_client.stop();

	String host = "www.googleapis.com";

	if (ssl_client.connect(host.c_str(), 443) > 0) {
		String payload = "{\"email\":\"";
		payload += email;
		payload += "\",\"password\":\"";
		payload += password;
		payload += "\",\"returnSecureToken\":true}";

		String header = "POST /identitytoolkit/v3/relyingparty/verifyPassword?key=";
		header += apiKey;
		header += " HTTP/1.1\r\n";
		header += "Host: ";
		header += host;
		header += "\r\n";
		header += "Content-Type: application/json\r\n";
		header += "Content-Length: ";
		header += payload.length();
		header += "\r\n\r\n";

		if (ssl_client.print(header) == header.length()) {
			if (ssl_client.print(payload) == payload.length()) {
				unsigned long ms = millis();
				while (ssl_client.connected() && ssl_client.available() == 0 && millis() - ms < 5000) {
					delay(1);
				}
				ms = millis();
				while (ssl_client.connected() && ssl_client.available() && millis() - ms < 5000) {
					String line = ssl_client.readStringUntil('\n');
					if (line.length()) {
						isAuthenticated = line.indexOf("HTTP/1.1 200 OK") > -1;
						break;
					}
				}
				ssl_client.stop();
			}
		}
	}

	if (isAuthenticated) {
		UserAuth user_auth(apiKey, email, password);
		connectFirebase(user_auth, num);
		sendAuthResult(num, isAuthenticated);
	} else {
		sendAuthResult(num, isAuthenticated);
	}
}

void connectFirebase(UserAuth &user_auth, uint8_t num) {
	Firebase.printf("[Omnisense Sensor] [Firebase] [v] %s\n", FIREBASE_CLIENT_VERSION);

	Serial.println("[Omnisense Sensor] [Firebase] Initializing app...");

	ssl_client1.setInsecure();
	ssl_client2.setInsecure();

	initializeApp(aClient2, app, getAuth(user_auth), asyncCB, "authTask");
}

void initializeRTDB() {
	app.getApp<RealtimeDatabase>(Database);

	Database.url(databaseUrl);

	Database.setSSEFilters("get,put,patch,keep-alive,cancel,auth_revoked");

	String devicesPath = String(app.getUid()) + "/" + instancePath + "/devices";
	Serial.printf("[Omnisense Sensor] [Devices Path] %s\n", devicesPath.c_str());

	Database.get(aClient, devicesPath.c_str(), asyncCB, true, "State Listener");
	Database.get(aClient2, (String(app.getUid()) + "/instances").c_str(), asyncCB1, true, "Instance Fetcher");

	Serial.println("[Omnisense Sensor] [Firebase] Initialized with two streams.");
}

void stopActiveListeners() {
	aClient.stopAsync(true);
	aClient2.stopAsync(true);
}

void handleSetInstance() {
	if (isAuthenticated) {
		String selectedInstance = server.arg("plain");
		stopActiveListeners();
		instancePath = selectedInstance;
		Serial.printf("[Omnisense Sensor] [Instance]: %s\n", instancePath);
		taskListenerReady = false;
		server.send(200);
	} else {
		server.send(403);
	}
}

void sendAuthResult(uint8_t num, bool status) {
	DynamicJsonDocument doc(1024);
	JsonObject auth_result = doc.createNestedObject("auth_result");

	auth_result["isAuthenticated"] = status;
	auth_result["message"] = status ? "Success." : "Invalid email or password.";

	String response;
	serializeJson(doc, response);

	webSocket.sendTXT(num, response);
}

void webSocketEvent(const uint8_t& num, const WStype_t& type, uint8_t * payload, const size_t& length) {
	(void) length;
	switch (type) {
		case WStype_CONNECTED: {
			DynamicJsonDocument doc(1024);
			JsonObject auth_result = doc.createNestedObject("auth");

			auth_result["status"] = isAuthenticated ? "authenticated" : "not_authenticated";

			String response;
			serializeJson(doc, response);

			webSocket.sendTXT(num, response);
			break;
		}
		case WStype_TEXT: {
			DynamicJsonDocument doc(1024);
			deserializeJson(doc, payload);
			Serial.printf("[Omnisense Sensor] [Socket] [Message] %s\n", doc);

			if (doc.containsKey("auth_request")) {
				JsonObject authRequest = doc["auth_request"];
				const String email = authRequest["email"];
				const String password = authRequest["password"];

				authenticateUser(apiKey, email, password, num);
			}
			break;
		}
		default:
			break;
	}
}

void setup() {
	Serial.begin(115200);

	WiFi.mode(WIFI_AP);
	WiFi.softAP("omnisense-sensor", "config-it");

	Serial.print("[Omnisense Sensor] [Wi-Fi] [AP] [IP] ");
	Serial.println(WiFi.softAPIP());

	server.on("/", handleAuth);
	server.on("/config", HTTP_POST, handleConfigureWifi);
	server.on("/instance", HTTP_POST, handleSetInstance);
	server.begin();

	webSocket.onEvent(webSocketEvent);
	webSocket.begin();

	ssl_client.setInsecure();
}

void sendStateChange(String path, String name, bool currentState) {
	JsonWriter writer;

	String action = currentState ? "on" : "off";
	time_t now = time(nullptr);
	long long currentTime = static_cast<long long>(now) * 1000;

	String formattedObject = "{\"actionType\":\"StateToggle\",";
  formattedObject += "\"action\":\"" + action + "\",";
  formattedObject += "\"name\":\"" + name + "\",";
  formattedObject += "\"sentBy\":\"Sensor Client\",";
  formattedObject += "\"message\":\"Turned " + action + " the " + name + ".\",";
  formattedObject += "\"timeSent\":" + String(currentTime) + "}";

  object_t message(formattedObject.c_str());

	String targetPath = String(app.getUid()) + "/" + instancePath + "/devices" + path;
	String messagePath = String(app.getUid()) + "/" + instancePath + "/messages" + "/" + String(currentTime);

	Database.set(aClient2, messagePath, message); 

	object_t state;
	writer.create(state, "state", currentState);

	Database.update(aClient2, targetPath, state);
}

bool isWithinSchedule(const Schedule& schedule) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return false;
  }
  
  int currentDayOfWeek = timeinfo.tm_wday;

  bool isDayScheduled = false;
  for (int i = 0; i < 7; i++) {
    if (schedule.days[i].length() == 0) continue;
    
    String day = schedule.days[i];
    
    if ((day == "Sunday" && currentDayOfWeek == 0) ||
        (day == "Monday" && currentDayOfWeek == 1) ||
        (day == "Tuesday" && currentDayOfWeek == 2) ||
        (day == "Wednesday" && currentDayOfWeek == 3) ||
        (day == "Thursday" && currentDayOfWeek == 4) ||
        (day == "Friday" && currentDayOfWeek == 5) ||
        (day == "Saturday" && currentDayOfWeek == 6)) {
      isDayScheduled = true;
      break;
    }
  }
  
  if (!isDayScheduled) return false;
  
  int fromHour, fromMinute, toHour, toMinute;
  bool fromIsPM, toIsPM;
  
  char fromAmPm[3] = {0};
  int fromResult = sscanf(schedule.from.c_str(), "%d:%d %2s", &fromHour, &fromMinute, fromAmPm);
  fromIsPM = (strcmp(fromAmPm, "PM") == 0);
  
  char toAmPm[3] = {0};
  int toResult = sscanf(schedule.to.c_str(), "%d:%d %2s", &toHour, &toMinute, toAmPm);
  toIsPM = (strcmp(toAmPm, "PM") == 0);
  
  // Check for parsing errors
  if (fromResult != 3 || toResult != 3) {
    Serial.println("[Schedule] Time parsing failed, returning false");
    return false;
  }

  // Convert to 24-hour format
  int originalFromHour = fromHour;
  int originalToHour = toHour;
  
  if (fromHour == 12) fromHour = fromIsPM ? 12 : 0;
  else if (fromIsPM) fromHour += 12;
  
  if (toHour == 12) toHour = toIsPM ? 12 : 0;
  else if (toIsPM) toHour += 12;

  int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int fromMinutes = fromHour * 60 + fromMinute;
  int toMinutes = toHour * 60 + toMinute;
  
  bool isWithinTime;
  if (fromMinutes <= toMinutes) {
    isWithinTime = (currentMinutes >= fromMinutes && currentMinutes < toMinutes);
  } else {
    isWithinTime = (currentMinutes >= fromMinutes || currentMinutes < toMinutes);
  }
  
  return isWithinTime;
}


void sensorListeners() {
  static const unsigned long debounceDelay = 500;
  
  for (auto &entry : devicesMap) {
    Device &device = entry.second;
		Pin &pin = device.sensor.pin;
		Schedule &schedule = device.schedule;
    
    if (device.sensor.name == "Sound sensor (KY-038)" && device.sensorMode) {
      Pin &pin = device.sensor.pin;
      
      int triggered = digitalRead(pin.pin);
      unsigned long currentTime = millis();
      
      if (triggered) {
        if ((currentTime - pin.lastDebounceTime) > debounceDelay) {
          sendStateChange(entry.first, device.name, !pin.previousState);
          pin.lastDebounceTime = currentTime;
        }
      }
    }

		if (schedule.from != "" && device.scheduleMode) {
			bool shouldBeOn = isWithinSchedule(schedule);
			if (shouldBeOn) {
				if (pin.previousState) continue;
				sendStateChange(entry.first, device.name, !pin.previousState);

				continue;
			} else {
				if (!pin.previousState) continue;
				sendStateChange(entry.first, device.name, !pin.previousState);
			}
		}
  }
}

void loop() {
	app.loop();
	Database.loop();
	server.handleClient();
	webSocket.loop();

	if (app.ready() && !taskListenerReady) {
		taskListenerReady = true;
		delay(250);
		initializeRTDB();
	}

	if (pinsReady)
		sensorListeners();
}