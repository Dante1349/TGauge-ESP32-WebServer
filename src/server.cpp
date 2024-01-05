#include <WiFi.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <FS.h>
#include <LittleFS.h>
#include <Arduino.h>
#include <WebSocketsServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h> // needs to be imported after WiFiManager.h because of colliding definitions
#include <ESPmDNS.h>

#include <ArduinoJson.h>

#define ENB 4
#define IN4 5
#define IN3 6
#define SER 11
#define OE 12
#define RCLK 13
#define SRCLK 14

struct Config {
  char speedLimit[64];
  char ledConfig[64];
};

void saveConfig(const Config& config);
void loadConfig(Config& config);
void updateShiftRegister();
void setBrightness(int b);
void initPins();
void initFS();
void saveConfigCallback();
void initWiFi();
void notFound(AsyncWebServerRequest *request);
void initWebserver();
String getContentType(String filename);
void getLocalIP(AsyncWebServerRequest *request);
void getSpeed(AsyncWebServerRequest *request);
void getSpeedLimit(AsyncWebServerRequest *request);
void forgetConfig(AsyncWebServerRequest *request);
void setConfig(AsyncWebServerRequest *request);
void reverseDirection(AsyncWebServerRequest *request);

WiFiManager wifiManager;
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

int globalSpeed = 0;
byte* leds;
const int chunkSize = 8;
int numChunks = 0;
Config config ={"255","00000000"};
// Define custom parameters
WiFiManagerParameter speed_limit("speedLimit", "Speed Limit (0-255)", config.speedLimit, 64);
WiFiManagerParameter led_config("ledConfig", "LED-Config", config.ledConfig, 64);

void initPins() {
    pinMode(IN4, OUTPUT);
    pinMode(IN3, OUTPUT);

    pinMode(SER, OUTPUT);
    pinMode(OE, OUTPUT);
    pinMode(RCLK, OUTPUT);
    pinMode(SRCLK, OUTPUT);

    digitalWrite(IN4, HIGH);
    // disable shift register output
    digitalWrite(OE, HIGH);
}

void initFS() {
    // Initialize LittleFS
    if(!LittleFS.begin()){
        Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }
}

void saveConfigCallback() {
    Serial.println("Save config callback");

    strlcpy(config.speedLimit, speed_limit.getValue(), sizeof(config.speedLimit));
    strlcpy(config.ledConfig, speed_limit.getValue(), sizeof(config.ledConfig));

    Serial.println(String(config.speedLimit));
    Serial.println(String(config.ledConfig));
    saveConfig(config); // Save the config to LittleFS
}

void initWiFi() {
    String hostname = "train";
    WiFi.setHostname(hostname.c_str());

    loadConfig(config); // Load saved config

    // Add custom parameters to WiFiManager
    wifiManager.addParameter(&speed_limit);
    wifiManager.addParameter(&led_config);

    // Save custom parameter on save
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    //set custom ip for portal
    //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
    wifiManager.autoConnect("Train-Server-AP");

    // wait for wifi
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("WiFi connected.");
    Serial.print("Hostname: ");
    Serial.println(WiFi.getHostname());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin(hostname)) {
        Serial.println("Error setting up MDNS responder!");
    } else {
        Serial.println("mDNS responder started. Address: " + hostname + ".local");
    }

    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void initWebserver() {
    server.on("/config", HTTP_GET, setConfig);
    server.on("/reverse", HTTP_GET, reverseDirection);
    server.on("/getLocalIP", HTTP_GET, getLocalIP);
    server.on("/getSpeed", HTTP_GET, getSpeed);
    server.on("/getSpeedLimit", HTTP_GET, getSpeedLimit);
    server.on("/forgetConfig", HTTP_GET, forgetConfig);
    server.onNotFound(notFound);

    AsyncCallbackWebHandler* handler = new AsyncCallbackWebHandler();
    handler->onRequest( [](AsyncWebServerRequest *request) {
        String path = request->url();

        if (path.endsWith("/")) {
            path += "index.html";
        }

        if (LittleFS.exists(path)) {
            request->send(LittleFS, path, getContentType(path));
        } else {
            request->send(404, "text/plain", "File not found");
        }
    });
    server.addHandler(handler);

    // Start the server
    server.begin();
    webSocket.begin();
    Serial.println("Web Server started");
}

void setup() {
    // put your setup code here, to run once:
    Serial.begin(115200);

    while (!Serial)  // Wait for the serial connection to be established.
        delay(50);

    Serial.print("Train-Server initializing...");

    initPins();
    initFS();
    initWiFi();
    initWebserver();

    Serial.print("Train-Server started");
}

void loop() {
    webSocket.loop();
}

void saveConfig(const Config& config) {
  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }

  StaticJsonDocument<256> jsonDocument;
  jsonDocument["speedLimit"] = config.speedLimit;
  jsonDocument["ledConfig"] = config.ledConfig;

  if (serializeJson(jsonDocument, configFile) == 0) {
    Serial.println(F("Failed to write to file"));
  }

  configFile.close();
}

void loadConfig(Config& config) {
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Config file doesn't exist. Creating default configuration.");
    saveConfig(config); // Save default configuration to create the file
    return;
  }

  StaticJsonDocument<256> jsonDocument;
  DeserializationError error = deserializeJson(jsonDocument, configFile);
  if (error) {
    Serial.println("Failed to read file, using default configuration");
    return;
  }

  strlcpy(config.speedLimit, jsonDocument["speedLimit"] | "", sizeof(config.speedLimit));
  strlcpy(config.ledConfig, jsonDocument["ledConfig"] | "", sizeof(config.ledConfig));

  configFile.close();
}

void updateShiftRegister() {
    digitalWrite(RCLK, LOW);
    Serial.println("LED config:");
    for(int i = 0; i < numChunks; i++) {
        Serial.println(String(i) + ": " + String(leds[i]));
        shiftOut(SER, SRCLK, MSBFIRST, leds[i]);
    }
    digitalWrite(RCLK, HIGH);
}

void setBrightness(int b) {
    analogWrite(OE, 255 - b);
}

String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) return "image/jpeg";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

void getLocalIP(AsyncWebServerRequest *request) {
    request->send(200, "text/plain", WiFi.localIP().toString());
}

void getSpeed(AsyncWebServerRequest *request) {
    request->send(200, "text/plain",  String(globalSpeed));
}

void getSpeedLimit(AsyncWebServerRequest *request) {
    request->send(200, "text/plain",  String(config.speedLimit));
}

void forgetConfig(AsyncWebServerRequest *request) {
    wifiManager.resetSettings();
    Serial.println("Removed wifi settings");
    if(LittleFS.remove("/config.json")){
        Serial.println("Removed config file");
    } else {
        Serial.println("Could not remove config file");
    }
    request->send(200, "text/plain",  "deleted wifi config");
    ESP.restart();
}

void setConfig(AsyncWebServerRequest *request) {
    if (request->hasArg("speed")) {
        Serial.println("Set Config -> speed: " + request->arg("speed"));
        int speed = request->arg("speed").toInt();
        if (speed > 0) {
            if(speed > 255) {
                speed = 255;
                Serial.println("Cannot raise Speed higher than 255.");
            }
        } else {
            speed = 0;
        }
        globalSpeed = speed;
        String speedTXT = String(speed);
        webSocket.broadcastTXT(speedTXT);
        analogWrite(ENB, speed);
        request->send(200, "text/plain", "Set config -> speed: " + String(speed));
    } else if (request->hasArg("brightness") && request->hasArg("leds")) {
        int brightness = request->arg("brightness").toInt();
        String ledConfig = request->arg("leds");
        Serial.println("Set config -> brightness: " + String(brightness) + " leds: " + ledConfig);

        // reset array pointer
        if (leds != nullptr) {
            delete[] leds;
            leds = nullptr;
            numChunks = 0;
        }

        // pad with zeros if the input is shorter than the chunk size
        while(ledConfig.length() < chunkSize) {
            ledConfig = "0" + ledConfig;
        }

        numChunks = ledConfig.length() / chunkSize;

        leds = new byte[numChunks];
        for(int i=0; i<numChunks; i++) {
            String chunk = ledConfig.substring( i * chunkSize, ( i + 1 ) * chunkSize);
            int intVal = strtol(chunk.c_str(), NULL, 2);
            leds[i] = static_cast<byte>(intVal);
        }


        setBrightness(brightness);
        updateShiftRegister();
        request->send(200, "text/plain", "Set config -> brightness: " + String(brightness) + " leds: " + ledConfig);
    }
    else {
        request->send(200, "text/plain", "No arg server provided");
    }
}

void reverseDirection(AsyncWebServerRequest *request) {
    String log = "reversed direction";
    Serial.println(log);
    
    bool currPin = (digitalRead(IN4) == HIGH);
    digitalWrite(IN4, !currPin);
    digitalWrite(IN3, currPin);

    request->send(200, "text/plain", log);
}
