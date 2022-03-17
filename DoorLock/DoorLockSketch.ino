/*
    This sketch sends data via HTTP GET requests to data.sparkfun.com service.

    You need to get streamId and privateKey at data.sparkfun.com and paste them
    below. Or just customize this script to talk to other HTTP servers.

*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "secrets.h"

const char* WIFI_SSID       = "Mika";
const char* WIFI_PASSWORD   = SECRET_WIFI_PASSWORD;
const char* SERVER_USER     = "WIFI_DOOR_LOCK_SERVER";
const char* SERVER_ALIAS    = "wifi-door-lock";
const char* SERVER_PASSWORD = SECRET_SERVER_PASSWORD;
const char* OTA_USER        = "WifiDoorLockOTA";
const char* OTA_PASSWORD    = SECRET_OTA_PASSWORD;
const int BAUD_RATE = 74880;
const int PORT = 4562;
const int OUTPUT_PIN_LEFT = 12;
const int OUTPUT_PIN_RIGHT = 13;
const int OTA_CHECK_SECONDS = 1;

ESP8266WebServer server(PORT);

void setupMDNS() {
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

void resetPins() {
  digitalWrite(OUTPUT_PIN_LEFT, LOW);
  digitalWrite(OUTPUT_PIN_RIGHT, LOW);
}

void setupWIFI() {
  Serial.println();
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
  Serial.print(PORT);
  Serial.println("");
}

void setup() {
  // Stop bluetooth from draining battery
  btStop();

  pinMode(OUTPUT_PIN_LEFT, OUTPUT);
  pinMode(OUTPUT_PIN_RIGHT, OUTPUT);
  resetPins();

  Serial.println("");
  Serial.println("Pins set to (low) output mode");

  Serial.begin(BAUD_RATE);
  delay(10);

  setupOTA();

  for (int x = 0; x < OTA_CHECK_SECONDS; x++) {
    ArduinoOTA.handle();
  }

  Serial.println("");
//  setupWIFI();
//  setupMDNS();
  Serial.println("I'm awake, but I'm going into deep sleep mode for 30 seconds");
  ESP.deepSleep(30e6);

//  server.on("/", handleRoot);
//  server.begin();
//  Serial.println("");
//  Serial.println("HTTP Server Started");
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
    return;
  }

  String unlock = server.arg("unlock");
  String lock = server.arg("lock");

  if (unlock == "" && lock == "") {
    rootJson["error"] = serialized("{\"code\": 400, \"message\": \"Bad Request: Must supply lock or unlock\"}");
    responseCode = 400;
  } else {
    responseCode = 200;

    if (unlock == "true") {
      digitalWrite(OUTPUT_PIN_LEFT, HIGH);
      digitalWrite(OUTPUT_PIN_RIGHT, LOW);
      rootJson["unlocked"] = true;
      Serial.print("Unlocking...");
    } else {
      digitalWrite(OUTPUT_PIN_LEFT, LOW);
      digitalWrite(OUTPUT_PIN_RIGHT, HIGH);
      rootJson["locked"] = true;
      Serial.print("Locking...");
    }

    // Wait 5 seconds and turn off pins
    delay(1000);
    Serial.println("");
    Serial.println("Turning Both Pins Off...");
    resetPins();
  }

  serializeJson(rootJson, res);
  server.send(responseCode, "application/json", res);

  Serial.print("RES: ");
  Serial.print(res);
  Serial.println("");
}

void loop() {
//  ArduinoOTA.handle();
//  MDNS.update();
//  server.handleClient();
}

