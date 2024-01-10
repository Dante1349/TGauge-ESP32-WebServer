#include <WiFi.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <FS.h>
#include <LittleFS.h>
#include <Arduino.h>
#include <WebSocketsServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h> // needs to be imported after WiFiManager.h because of colliding definitions
#include <ESPmDNS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

#define ENB 4
#define IN4 5
#define IN3 6
#define SER 11
#define OE 12
#define RCLK 13
#define SRCLK 14

struct Config {
    char timeZoneOffset[64];
    char speedLimit[64];
    char ledCount[64];
    char ledBrightness[64];
};

void printTime();
void saveConfig(const Config& config);
void loadConfig(Config& config);
String generateHouseLights(String input, int houseArray[], int arrayLength, float desiredPercentage);
float calcPercentage(float startPercentage, float targetPercentage, int intervalInMinutes, int hour, int minute, int startHour, int endHour);
String removeCharAtIndex(String str, int index);
String changeCharAtIndex(String str, int index, char newChar);
String generateLightState(String input, int houseArray[], int houseArrayLength, int commercialArray[], int commercialArrayLength, int streetArray[], int streetArrayLength, int currentHour, int currentMinute);
String generateZeroString(int amount);
String byteToBinaryString(byte value);
void updateShiftRegister(int brightness, String ledString);
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
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

int globalSpeed = 0;
byte* leds;
const int chunkSize = 8;
int numChunks = 0;
Config config = {"0", "255", "32", "100"};
int houses[] = {1,2,3};
int commercialBuildings[] = {0};
int streetLights[] = {4,5,6,7};
// Define custom parameters
WiFiManagerParameter time_zone_offset("timeZoneOffset", "UTC Timezone Offset in hours", config.timeZoneOffset, 64);
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

    strlcpy(config.timeZoneOffset, time_zone_offset.getValue(), sizeof(config.timeZoneOffset));
    strlcpy(config.speedLimit, speed_limit.getValue(), sizeof(config.speedLimit));
    strlcpy(config.ledCount, led_count.getValue(), sizeof(config.ledCount));
    strlcpy(config.ledBrightness, led_brightness.getValue(), sizeof(config.ledBrightness));

    Serial.println("config.timeZoneOffset: "+String(config.timeZoneOffset));
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
    wifiManager.addParameter(&time_zone_offset);
    wifiManager.addParameter(&speed_limit);
    wifiManager.addParameter(&led_count);
    wifiManager.addParameter(&led_brightness);

    // Save custom parameter on save
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    wifiManager.setConnectTimeout(180);
    wifiManager.setConnectRetries(20);

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
    timeClient.begin();
    int timeZoneOffsetInSeconds = atoi(config.timeZoneOffset)*3600;
    timeClient.setTimeOffset(timeZoneOffsetInSeconds);
    timeClient.update();
    Serial.println("time zone offset set to: " + String(timeZoneOffsetInSeconds));
    printTime();
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

    Serial.println("Train-Server initializing...");

    initPins();
    initFS();
    initWiFi();
    initWebserver();

    int houseArrayLength = sizeof(houses) / sizeof(houses[0]);
    int commercialArrayLength = sizeof(commercialBuildings) / sizeof(commercialBuildings[0]);
    int streetArrayLength = sizeof(streetLights) / sizeof(streetLights[0]);

    currentLights = generateLightState(generateZeroString(atoi(config.ledCount)), houses, houseArrayLength, commercialBuildings, commercialArrayLength, streetLights, streetArrayLength, timeClient.getHours(), timeClient.getMinutes());
    updateShiftRegister(atoi(config.ledBrightness), currentLights);

    Serial.println("Train-Server started");
}

void loop() {
    webSocket.loop();
    if (!timeClient.update()) {
    }

    unsigned long currentMillis = millis();

    int interval = 1000 * 60 * 10; // every 10 minutes;
    int testInterval = 1000 / 6; // 1 second == 1 hour

    if (currentMillis - lastTimeUpdate >= interval) {
        printTime();

        lastTimeUpdate = currentMillis;

        int houseArrayLength = sizeof(houses) / sizeof(houses[0]);
        int commercialArrayLength = sizeof(commercialBuildings) / sizeof(commercialBuildings[0]);
        int streetArrayLength = sizeof(streetLights) / sizeof(streetLights[0]);

        currentLights = generateLightState(generateZeroString(atoi(config.ledCount)), houses, houseArrayLength, commercialBuildings, commercialArrayLength, streetLights, streetArrayLength, timeClient.getHours(), timeClient.getMinutes());
        updateShiftRegister(atoi(config.ledBrightness), currentLights);

        Serial.println("--------------------------------------------------------------------------------");
    }
}

void printTime() {
    String hour = String(timeClient.getHours());
    if (hour.length() == 1) {
        hour = '0' + hour;
    }

    String minute = String(timeClient.getMinutes());
    if (minute.length() == 1) {
        minute = '0' + minute;
    }

    Serial.println("[" + hour + ":" + minute + "]");
}

void saveConfig(const Config& config) {
    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println("Failed to open config file for writing");
        return;
    }

    StaticJsonDocument<256> jsonDocument;
    jsonDocument["timeZoneOffset"] = config.timeZoneOffset;
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

    strlcpy(config.timeZoneOffset, jsonDocument["timeZoneOffset"] | "", sizeof(config.timeZoneOffset));
    strlcpy(config.speedLimit, jsonDocument["speedLimit"] | "", sizeof(config.speedLimit));
    strlcpy(config.ledCount, jsonDocument["ledCount"] | "", sizeof(config.ledCount));
    strlcpy(config.ledBrightness, jsonDocument["ledBrightness"] | "", sizeof(config.ledBrightness));

    configFile.close();
    Serial.println("Loaded config successfully.");
}

String generateLightState(String input, int houseArray[], int houseArrayLength, int commercialArray[], int commercialArrayLength, int streetArray[], int streetArrayLength, int currentHour, int currentMinute) {
    String lightString = input;

    if ((currentHour >= 5) && (currentHour < 7)) {
        float percentage = calcPercentage(0, 100, 10, currentHour, currentMinute, 5, 7);
        lightString = generateHouseLights(input, houseArray, houseArrayLength, percentage);
    } else if ((currentHour >= 7) && (currentHour < 9)) {
        float percentage = calcPercentage(100, 0, 10, currentHour, currentMinute, 7, 9);
        lightString = generateHouseLights(input, houseArray, houseArrayLength, percentage);
    } else if ((currentHour >= 9) && (currentHour < 16)) {
        float percentage = 0;
        lightString = generateHouseLights(input, houseArray, houseArrayLength, percentage);
    } else if ((currentHour >= 16) && (currentHour < 18)) {
        float percentage = calcPercentage(0, 100, 10, currentHour, currentMinute, 16, 18);
        lightString = generateHouseLights(input, houseArray, houseArrayLength, percentage);
    } else if ((currentHour >= 18) && (currentHour < 22)) {
        float percentage = 100;
        lightString = generateHouseLights(input, houseArray, houseArrayLength, percentage);
    } else if ((currentHour >= 22) || (currentHour < 4)) {
        float percentage = calcPercentage(100, 0, 10, currentHour, currentMinute, 22, 4);
        lightString = generateHouseLights(input, houseArray, houseArrayLength, percentage);
    } else {
        float percentage = 0;
        lightString = generateHouseLights(input, houseArray, houseArrayLength, percentage);
    }

    // Commercial lights
    if (currentHour >= 9 && currentHour < 20) {
        // Commercial on
        for (int i = 0; i < commercialArrayLength; i++) {
            lightString = changeCharAtIndex(lightString, commercialArray[i], '1');
        }
    } else {
        // Commercial off
        for (int i = 0; i < commercialArrayLength; i++) {
            lightString = changeCharAtIndex(lightString, commercialArray[i], '0');
        }
    }

    // Street lights
    if (currentHour >= 17 && currentHour <= 23 || currentHour >= 5 && currentHour < 8) {
        // Street lights on
        for (int i = 0; i < streetArrayLength; i++) {
            lightString = changeCharAtIndex(lightString, streetArray[i], '1');
        }
    } else {
        // Street lights off
        for (int i = 0; i < streetArrayLength; i++) {
            lightString = changeCharAtIndex(lightString, streetArray[i], '0');
        }
    }

    return lightString;
}

String generateHouseLights(String input, int houseArray[], int arrayLength, float desiredPercentage) {
    String result = input;

    // Only include house array indexes
    String tempArr = "";
    for (int i = 0; i < arrayLength; i++) {
        if (std::find(houseArray, houseArray + arrayLength, i) != houseArray + arrayLength) {
            tempArr += input[i];
        }
    }

    // Count number of house lights that are off
    int noOnes = std::count(tempArr.begin(), tempArr.end(), '1');
    // Count number of house lights that are on
    int noZeros = std::count(tempArr.begin(), tempArr.end(), '0');
    // Count number of house lights
    int allHouseLights = tempArr.length();

    float currPercentage = (noOnes / (float)allHouseLights) * 100;

    if (currPercentage >= desiredPercentage) {
        float percentageDiff = currPercentage - desiredPercentage;
        float lightsPercentageDiff = (allHouseLights / 100.0) * percentageDiff;
        Serial.print("Too many lights are turned on. ");
        Serial.print(percentageDiff);
        Serial.print("% too many Lights. That are ");
        Serial.print(lightsPercentageDiff);
        Serial.println(" Lights");

        int lightsToTurnOff = 0;
        float randomValue = random(1000) / 1000.0;
        if(randomValue > 0.50){
            lightsToTurnOff = ceil(lightsPercentageDiff);
        } else {
            lightsToTurnOff = floor(lightsPercentageDiff);
        }

        if (lightsToTurnOff > noOnes) {
            lightsToTurnOff = noOnes;
        }

        if (lightsToTurnOff >= 1) {
            Serial.print("Turning off ");
            Serial.print(lightsToTurnOff);
            Serial.println(" lights.");

            int lightsTurnedOff = 0;
            while (lightsTurnedOff < lightsToTurnOff) {
                int randomIndex = random(0, allHouseLights);

                if (result[randomIndex] == '1' && std::find(houseArray, houseArray + arrayLength, randomIndex) != houseArray + arrayLength) {
                    result[randomIndex] = '0';
                    lightsTurnedOff++;
                }
            }

            return result;
        } else {
            return result;
        }
    } else {
        float percentageDiff = currPercentage + (desiredPercentage - currPercentage);
        float lightsPercentageDiff = (allHouseLights / 100.0) * percentageDiff;
        Serial.print("Not enough lights are turned on. ");
        Serial.print(percentageDiff);
        Serial.print("% too few Lights. That are ");
        Serial.print(lightsPercentageDiff);
        Serial.println(" Lights");

        int lightsToTurnOn = 0;
        float randomValue = random(1000) / 1000.0;
        if(randomValue > 0.50){
            lightsToTurnOn = floor(lightsPercentageDiff);
        } else {
            lightsToTurnOn = ceil(lightsPercentageDiff);
        }

        if (lightsToTurnOn > noZeros) {
            lightsToTurnOn = noZeros;
        }

        if (lightsToTurnOn >= 1) {
            Serial.print("Turning on ");
            Serial.print(lightsToTurnOn);
            Serial.println(" lights.");

            int lightsTurnedOn = 0;
            while (lightsTurnedOn < lightsToTurnOn) {
                int randomIndex = random(0, allHouseLights);

                if (result[randomIndex] == '0' && std::find(houseArray, houseArray + arrayLength, randomIndex) != houseArray + arrayLength) {
                    result[randomIndex] = '1';
                    lightsTurnedOn++;
                }
            }

            return result;
        } else {
            return result;
        }
    }
}

float calcPercentage(float startPercentage, float targetPercentage, int intervalInMinutes, int hour, int minute, int startHour, int endHour) {
    float duration = 0;
    if (startHour <= endHour) {
        duration = endHour - startHour;
    } else {
        duration = (24 + endHour) - startHour;
    }

    duration = duration * 60; // 60 minutes per hour

    float steps = duration / intervalInMinutes;

    int elapsedHours = 0;
    if (startHour <= hour) {
        elapsedHours = hour - startHour;
    } else {
        elapsedHours = (24 + hour) - startHour;
    }

    int currentStep = ((elapsedHours * 60) + minute) / intervalInMinutes;

    float newPercentage = 0;
    if (startPercentage > targetPercentage) {
        float diff = startPercentage - targetPercentage;
        float stepPercentage = diff / steps;
        float percentageChange = stepPercentage * currentStep;
        newPercentage = startPercentage - percentageChange;
    } else {
        float diff = targetPercentage - startPercentage;
        float stepPercentage = diff / steps;
        float percentageChange = stepPercentage * currentStep;
        newPercentage = startPercentage + percentageChange;
    }

    return newPercentage;
}

// Function to change a character at a specific index in a string
String changeCharAtIndex(String str, int index, char newChar) {
    if (index < 0 || index >= str.length()) {
        // If the index is out of bounds, return the original string
        return str;
    }

    // Convert the string to a character array
    char strArray[str.length() + 1];
    str.toCharArray(strArray, str.length() + 1);

    // Replace the character at the specified index
    strArray[index] = newChar;

    // Convert the character array back to a string and return
    return String(strArray);
}

// Function to remove a character at a specific index from a string
String removeCharAtIndex(String str, int index) {
    if (index < 0 || index >= str.length()) {
        // If the index is out of bounds, return the original string
        return str;
    }

    // Create a new string by excluding the character at the specified index
    return str.substring(0, index) + str.substring(index + 1);
}

String generateZeroString(int amount) {
    String zeros = "";
    for (int i = 0; i < amount; i++) {
        zeros += '0';
    }
    return zeros;
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
