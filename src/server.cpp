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
    char ledCount[64];
    char ledBrightness[64];
};

void saveConfig(const Config& config);
void loadConfig(Config& config);
String generateZeroString(int amount);
void generateLightState(int currentHour, int currentMinute);
float calculatePercentage(int currentHour, int currentMinute, int startHour, int endHour, int percentage);
void turnOnLights(float percentage);
void turnOffLights(float percentage);
int countZeros(String inputString);
int countOnes(String inputString);
void turnOffAllLights();
String byteToBinaryString(byte value);
void updateShiftRegister(int brightness, String ledString);
void setBrightness(int b);
void initPins();
void initFS();
void saveConfigCallback();
void initWiFi();
void notFound(AsyncWebServerRequest *request);
void initWebserver();
void initLights();
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
Config config = {"255","32", "100"};
// Define custom parameters
WiFiManagerParameter speed_limit("speedLimit", "Speed Limit (0-255)", config.speedLimit, 64);
WiFiManagerParameter led_count("ledCount", "LED Count", config.ledCount, 64);
WiFiManagerParameter led_brightness("ledBrightness", "LED Brightness (0-255)", config.ledBrightness, 64);

unsigned long lastTimeUpdate = 0;
unsigned long updateInterval = 0.016666 * 60 * 1000; // Update interval: 30 minutes
String currentLights="0"; // Previous lights status

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
    strlcpy(config.ledCount, led_count.getValue(), sizeof(config.ledCount));
    strlcpy(config.ledBrightness, led_brightness.getValue(), sizeof(config.ledBrightness));

    Serial.println("config.speedLimit: "+String(config.speedLimit));
    Serial.println("config.ledCount: "+String(config.ledCount));
    Serial.println("config.ledBrightness: "+String(config.ledBrightness));
    saveConfig(config); // Save the config to LittleFS
}

void initWiFi() {
    String hostname = "train";
    WiFi.setHostname(hostname.c_str());

    loadConfig(config); // Load saved config

    // Add custom parameters to WiFiManager
    wifiManager.addParameter(&speed_limit);
    wifiManager.addParameter(&led_count);
    wifiManager.addParameter(&led_brightness);

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

void initLights() {
    unsigned long currentMillis = millis();

    // Calculate elapsed time in hours and minutes
    unsigned long elapsedHours = (currentMillis / (1000 * 60 * 60)) % 24;
    unsigned long elapsedMinutes = (currentMillis / (1000 * 60)) % 60;

    // Initialize currentLights based on the calculated time
    generateLightState(elapsedHours, elapsedMinutes);
    updateShiftRegister(atoi(config.ledBrightness), currentLights);
}

void setup() {
    // put your setup code here, to run once:
    Serial.begin(115200);

    while (!Serial)  // Wait for the serial connection to be established.
        delay(50);

    Serial.println("Train-Server initializing...");

    initPins();
    initFS();
    initWiFi();
    initWebserver();
    initLights();

    Serial.println("Train-Server started");
}

int simulatedHour = 0;
int intervalCount = 0;
void loop() {
    webSocket.loop();

    unsigned long currentMillis = millis();

    int interval = 1000 * 60 * 10; // every 10 minutes;
    int testInterval = 1000 / 6; // 1 second == 1 hour

    if (currentMillis - lastTimeUpdate >= interval) {
        String hour = String(simulatedHour);
        if(hour.length() == 1){
            hour = "0" + hour;
        }

        String minute = String(intervalCount*10);
        if(minute.length() == 1){
            minute = "0" + minute;
        }

        Serial.println("[" + hour + ":" + minute + "]");

        lastTimeUpdate = currentMillis;
        intervalCount++;

        if (intervalCount > 5) {
            intervalCount = 0;
            simulatedHour = (simulatedHour + 1) % 24;
        }

        generateLightState(simulatedHour, intervalCount*10);
        updateShiftRegister(atoi(config.ledBrightness), currentLights);

        Serial.println("--------------------------------------------------------------------------------");
    }
}

void saveConfig(const Config& config) {
    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println("Failed to open config file for writing");
        return;
    }

    StaticJsonDocument<256> jsonDocument;
    jsonDocument["speedLimit"] = config.speedLimit;
    jsonDocument["ledCount"] = config.ledCount;
    jsonDocument["ledBrightness"] = config.ledBrightness;

    if (serializeJson(jsonDocument, configFile) == 0) {
        Serial.println(F("Failed to write to file"));
    }

    configFile.close();
}

void loadConfig(Config& config) {
    File configFile = LittleFS.open("/config.json", "r");
    if (!configFile) {
        Serial.println("Config file doesn't exist. Creating default configuration.");
        saveConfig(config); // create default config file
        return;
    }

    StaticJsonDocument<256> jsonDocument;
    DeserializationError error = deserializeJson(jsonDocument, configFile);
    if (error) {
        Serial.println("Failed to read file, using default configuration");
        return;
    }

    strlcpy(config.speedLimit, jsonDocument["speedLimit"] | "", sizeof(config.speedLimit));
    strlcpy(config.ledCount, jsonDocument["ledCount"] | "", sizeof(config.ledCount));
    strlcpy(config.ledBrightness, jsonDocument["ledBrightness"] | "", sizeof(config.ledBrightness));

    configFile.close();
    Serial.println("Loaded config successfully.");
}

String generateZeroString(int amount) {
    String zeros = "";
    for (int i = 0; i < amount; i++) {
        zeros += '0';
    }
    return zeros;
}

void generateLightState(int currentHour, int currentMinute) {
    if (currentLights.length() != atoi(config.ledCount)) {
        Serial.println("current lights mismatches house amount");

        currentLights = generateZeroString(atoi(config.ledCount));
    }

    if (currentHour >= 6 && currentHour < 8) {
        float percentage = calculatePercentage(currentHour, currentMinute, 6, 8, 30);
        turnOnLights(percentage); // 90% Off->On from 07:00 to 08:00
    } else if (currentHour >= 8 && currentHour < 9) {
        float percentage = calculatePercentage(currentHour, currentMinute, 8, 9, 40);
        turnOffLights(percentage); // 90% Off->On from 08:00 to 09:00
    } else if (currentHour >= 9 && currentHour < 17) {
        turnOffAllLights(); // Lights off from 7:00 to 17:00
    } else if (currentHour >= 17 && currentHour < 22) {
        float percentage = calculatePercentage(currentHour, currentMinute, 17, 22, 40);
        turnOnLights(percentage); // 90% Off->On from 17:00 to 22:00
    } else if (currentHour >= 22 && currentHour < 23) {
        float percentage = calculatePercentage(currentHour, currentMinute, 22, 23, 30);
        turnOnLights(percentage); // Other 10% Off->On from 22:00 to 23:00
    } else if (currentHour >= 23 || currentHour < 1) {
        float percentage = calculatePercentage(currentHour, currentMinute, 23, 1, 30);
        turnOffLights(percentage); // 90% On->Off from 23:00 to 01:00
    } else if (currentHour >= 1 && currentHour < 7) {
        float percentage = calculatePercentage(currentHour, currentMinute, 1, 7, 40);
        turnOffLights(percentage); // Other 10% On->Off from 01:00 to 07:00
    }
}

float calculatePercentage(int currentHour, int currentMinute, int startHour, int endHour, int percentage) {
    int durationHours = 0;

    if(endHour > startHour) {
        durationHours = endHour - startHour;
    } else {
        durationHours = (24+endHour) - startHour;
    }

    int elapsedHours = 0;

    if(currentHour >= startHour) {
        elapsedHours = currentHour-startHour;
    } else {
        elapsedHours = (24+currentHour) - startHour;
    }

    int elapsedMinutes = (elapsedHours * 60) + currentMinute;

    return floor(( float(percentage) / ( float(durationHours) * 6.0 )) * ( float(elapsedMinutes) / 10.0 ));
}

void turnOnLights(float percentage) {
    int zerosCount = countZeros(currentLights);

    int lightsToTurnOn = 0;
    if (percentage > 1.0) {
        lightsToTurnOn = (float(atoi(config.ledCount))/100.0) * percentage;
    }
    if (lightsToTurnOn > zerosCount) {
        lightsToTurnOn = zerosCount;
    }

    for (int i = 0; i < lightsToTurnOn; i++) {
        int randomIndex;
        do {
            randomIndex = random(0, atoi(config.ledCount));
        } while (currentLights[randomIndex] == '1');

        currentLights[randomIndex] = '1';
    }
}

void turnOffLights(float percentage) {
    int onesCount = countOnes(currentLights);

    int lightsToTurnOff = 0;
    if (percentage > 1.0) {
        lightsToTurnOff = (float(atoi(config.ledCount))/100.0) * percentage;
    }

    if (lightsToTurnOff > onesCount) {
        lightsToTurnOff = onesCount;
    }

    for (int i = 0; i < lightsToTurnOff; i++) {
        int randomIndex;
        do {
            randomIndex = random(0, atoi(config.ledCount));
        } while (currentLights[randomIndex] == '0');

        currentLights[randomIndex] = '0';
    }
}

int countZeros(String inputString) {
    int zeroCount = 0;
    for (int i = 0; i < inputString.length(); i++) {
        if (inputString[i] == '0') {
            zeroCount++;
        }
    }
    return zeroCount;
}

int countOnes(String inputString) {
    int oneCount = 0;
    for (int i = 0; i < inputString.length(); i++) {
        if (inputString[i] == '1') {
            oneCount++;
        }
    }
    return oneCount;
}

void turnOffAllLights() {
    currentLights = generateZeroString(atoi(config.ledCount));
}

String byteToBinaryString(byte value) {
    String result = "";
    for( int i = 7; i >= 0; i--) {
        result += (value & (1 << i)) ? '1' : '0';
    }
    return result;
}

void updateShiftRegister(int brightness, String ledString) {
    // reset array pointer
    if (leds != nullptr) {
        delete[] leds;
        leds = nullptr;
        numChunks = 0;
    }

    String ledconf = ledString;
    // pad with zeros if the input is shorter than the chunk size
    while(ledconf.length() < chunkSize) {
        ledconf = "0" + ledconf;
    }

    numChunks = ledconf.length() / chunkSize;

    leds = new byte[numChunks];
    for(int i=0; i<numChunks; i++) {
        String chunk = ledconf.substring( i * chunkSize, ( i + 1 ) * chunkSize);
        int intVal = strtol(chunk.c_str(), NULL, 2);
        leds[i] = static_cast<byte>(intVal);
    }

    setBrightness(brightness);

    digitalWrite(RCLK, LOW);
    for(int i = 0; i < numChunks; i++) {
        Serial.println("    LEDS-" + String(i) + ": " + byteToBinaryString(leds[i]));
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
    request->send(200, "text/plain", String(globalSpeed));
}

void getSpeedLimit(AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(config.speedLimit));
}

void forgetConfig(AsyncWebServerRequest *request) {
    wifiManager.resetSettings();
    Serial.println("Removed wifi settings");
    if(LittleFS.remove("/config.json")){
        Serial.println("Removed config file");
    } else {
        Serial.println("Could not remove config file");
    }
    request->send(200, "text/plain", "deleted wifi config");
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
        String ledArgConfig = request->arg("leds");
        Serial.println("Set config -> brightness: " + String(brightness) + " leds: " + ledArgConfig);

        updateShiftRegister(brightness, ledArgConfig);

        request->send(200, "text/plain", "Set config -> brightness: " + String(brightness) + " leds: " + ledArgConfig);
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
