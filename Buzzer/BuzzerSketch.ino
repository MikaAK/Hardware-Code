
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "secrets.h"

const char* WIFI_SSID       = "Mika";
const char* WIFI_PASSWORD   = SECRET_WIFI_PASSWORD;
const char* SERVER_USER     = "WIFI_BUZZER_SERVER";
const char* SERVER_ALIAS    = "wifi-buzzer";
const char* SERVER_PASSWORD = SECRET_SERVER_PASSWORD;
const char* OTA_USER        = "WifiBuzzerOTA";
const char* OTA_PASSWORD    = SECRET_OTA_PASSWORD;
const int BAUD_RATE         = 74880;
const int OUTPUT_PIN        = 12;
const int port              = 3221;

ESP8266WebServer server(port);

void setupMDNS() {
  btStop();

  if (MDNS.begin(SERVER_ALIAS)) {              // Start the mDNS responder for esp8266.local
    Serial.print("mDNS responder started");
    Serial.print(" @ ");
    Serial.print(SERVER_ALIAS);
    Serial.print(".local");
    Serial.println();
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
}

void setupOTA() {
  ArduinoOTA.setHostname(OTA_USER);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("OTA ready");
}

void setupWifi() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
     would try to act as both a client and an access-point and could cause
     network-issues with your other WiFi-devices on your WiFi-network. */
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.print(WiFi.localIP());
  Serial.println("");
  Serial.print("Port: ");
  Serial.print(port);
  Serial.println("");
}

void setup() {
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW);
  Serial.println("");
  Serial.println("Pin Output Set");

  Serial.begin(BAUD_RATE);
  delay(10);

  setupOTA();
  setupWifi();
  setupMDNS();

  server.on("/", handleRoot);
  server.begin();
  Serial.println("");
  Serial.println("HTTP Server Started");
}

void handleRoot() {
  String res = "";
  StaticJsonDocument<200> jsonBuffer;
  DynamicJsonDocument rootJson(1024);
  int responseCode;

  if (!server.authenticate(SERVER_USER, SERVER_PASSWORD)) {
    responseCode = 401;
    rootJson["error"] = serialized("{\"code\": 401, \"message\": \"Unauthorized: Must login with basic auth\"}");

    serializeJson(rootJson, res);
    server.send(responseCode, "application/json", res);
    Serial.println("Blocked Open Request");
    return;
  }

  rootJson["buzzerRan"] = true;

  serializeJson(rootJson, res);

  server.send(responseCode, "application/json", res);
  Serial.println("Opening Gate");
  digitalWrite(OUTPUT_PIN, HIGH);
  delay(1000);
  digitalWrite(OUTPUT_PIN, LOW);
}

void loop() {
  ArduinoOTA.handle();
  MDNS.update();
  server.handleClient();
}

