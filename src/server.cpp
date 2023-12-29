#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager

#define TRACK_PIN 4
#define H_BRIDGE_PIN_1 5
#define H_BRIDGE_PIN_2 6

String getContentType(String filename);
void getSpeed();
void setConfig();
void handleFileRequest();
void reverseDirection();

WebServer server(80);

int globalSpeed = 0;

void setup() {
    // put your setup code here, to run once:
    Serial.begin(115200);

    pinMode(H_BRIDGE_PIN_1, OUTPUT);
    pinMode(H_BRIDGE_PIN_2, OUTPUT);

    // Initialize LittleFS
    if(!LittleFS.begin()){
        Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }

    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;
    //reset saved settings
    //wifiManager.resetSettings();

    //set custom ip for portal
    //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

    //fetches ssid and pass from eeprom and tries to connect
    //if it does not connect it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration
    wifiManager.autoConnect("T-Gauge-Server-AP");
    //or use this for auto generated name ESP + ChipID
    //wifiManager.autoConnect();

    // wait for wifi
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    WiFi.hostname("train");
    Serial.println("WiFi connected");

    if (!MDNS.begin("train")) {
        Serial.println("Error setting up MDNS responder!");
    } else {
        Serial.println("mDNS responder started");
    }

    // Print the IP address
    Serial.println(WiFi.localIP());

    server.on("/config", HTTP_GET, setConfig);
    server.on("/reverse", HTTP_GET, reverseDirection);
    server.on("/getSpeed", HTTP_GET, getSpeed);
    server.onNotFound(handleFileRequest);

    // Start the server
    server.begin();
    Serial.println("Web Server started");

    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);

    while (!Serial)  // Wait for the serial connection to be established.
        delay(50);

    digitalWrite(H_BRIDGE_PIN_1, HIGH);

    Serial.print("T-Gauge-Server started");
}

void loop() {
    server.handleClient();
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
        Serial.println("Set Config " + server.arg("speed"));
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

        analogWrite(TRACK_PIN, speed);
        server.send(200, "text/plain", "Speed set to: " + String(speed));
    } else {
        server.send(200, "text/plain", "no arg");
    }
}

void reverseDirection() {
    String log = "reversed direction";
    Serial.println(log);
    
    bool currPin = (digitalRead(H_BRIDGE_PIN_1) == HIGH);
    digitalWrite(H_BRIDGE_PIN_1, !currPin);
    digitalWrite(H_BRIDGE_PIN_2, currPin);

    server.send(200, "text/plain", log);
}
