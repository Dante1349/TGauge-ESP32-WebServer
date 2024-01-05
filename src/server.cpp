#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
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
void initWebserver();
String getContentType(String filename);
void getSpeed();
void getSpeedLimit();
void forgetConfig();
void setConfig();
void handleFileRequest();
void reverseDirection();

WiFiManager wifiManager;
WebServer server(80);

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

void initWebserver() {
    server.on("/config", HTTP_GET, setConfig);
    server.on("/reverse", HTTP_GET, reverseDirection);
    server.on("/getSpeed", HTTP_GET, getSpeed);
    server.on("/getSpeedLimit", HTTP_GET, getSpeedLimit);
    server.on("/forgetConfig", HTTP_GET, forgetConfig);
    server.onNotFound(handleFileRequest);

    // Start the server
    server.begin();
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
    server.handleClient();
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

void getSpeed() {
    server.send(200, "text/plain",  String(globalSpeed));
}

void getSpeedLimit() {
    server.send(200, "text/plain",  String(config.speedLimit));
}

void forgetConfig() {
    wifiManager.resetSettings();
    Serial.println("Removed wifi settings");
    if(LittleFS.remove("/config.json")){
        Serial.println("Removed config file");
    } else {
        Serial.println("Could not remove config file");
    }
    server.send(200, "text/plain",  "deleted wifi config");
    ESP.restart();
}

void handleFileRequest() {
    String path = server.uri();
    Serial.println(path);
    if (path.endsWith("/")) { path += "index.html"; }

    if (LittleFS.exists(path)) {
        File file = LittleFS.open(path, "r");
        server.streamFile(file, getContentType(path)); // Get content type dynamically
        file.close();
    } else {
        server.send(404, "text/plain", "File Not Found");
    }
}

void setConfig() {
    if (server.hasArg("speed") == true) {
        Serial.println("Set Config -> speed: " + server.arg("speed"));
        int speed = server.arg("speed").toInt();
        if (speed > 0) {
            if(speed > 255) {
                speed = 255;
                Serial.println("Cannot raise Speed higher than 255.");
            }
        } else {
            speed = 0;
        }

        globalSpeed = speed;

        analogWrite(ENB, speed);
        server.send(200, "text/plain", "Set config -> speed: " + String(speed));
    } else if (server.hasArg("brightness") && server.hasArg("leds")) {
        int brightness = server.arg("brightness").toInt();
        String ledConfig = server.arg("leds");
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
        server.send(200, "text/plain", "Set config -> brightness: " + String(brightness) + " leds: " + ledConfig);
    }
    else {
        server.send(200, "text/plain", "No arg server provided");
    }
}

void reverseDirection() {
    String log = "reversed direction";
    Serial.println(log);
    
    bool currPin = (digitalRead(IN4) == HIGH);
    digitalWrite(IN4, !currPin);
    digitalWrite(IN3, currPin);

    server.send(200, "text/plain", log);
}
