// TODO:
// Preferences saving so that it can load them on reboot
// Store WiFi config in preferences
// Allow for WiFi config changes (softAP, bluetooth)?

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <FastLED.h>
#include <time.h>
#include <Preferences.h>
#include <esp_bt.h>
#include "secrets.h"

#define VERSION "0.0.0"

#define PREFERENCES_KEY "ledpreferences"

#define MAIN_LOOP_MS_TARGET 16 // 100ms per loop == 60 Loops a second

#define MAX_COLOR_INDEX 240
#define STARTING_COLOR_INDEX 0
#define STARTING_BRIGHTNESS 60
#define MAX_BRIGHTNESS 100

#define HOST_NAME "bedroom-led-lights"
#define OTA_PASSWORD SECRET_OTA_PASSWORD

#define WIFI_RECONNECTION_INTERVAL 30000

#define NUM_LEDS 54
#define DATA_PIN 22

#define NTP_SERVER "pool.ntp.org"
#define DAYLIGHT_SAVINGS_OFFSET_SEC 0 // DEFAULT WOKRS FINE

// Define the array of leds
CRGB leds[NUM_LEDS];
CRGBPalette16 currentPalette;
TBlendType    currentBlending;

int gmtOffsetSec = -25200; // GMT-7

String ssid = "Milka";
String password = SECRET_WIFI_PASSWORD;
int wifiLastReconnectionMS = 0;

bool isSunriseRunning = false;
int currentSunriseHour = 9;
int currentSunriseMinute = 30;
int currentSunriseDuration = 1800000; // 10 Sec
int transitionDuration = 2000; // 2 Sec

int currentBrightness = STARTING_BRIGHTNESS;
int maxBrightness = MAX_BRIGHTNESS;
int currentColor = CRGB::Black;

Preferences preferences;

WebServer server(80);

TaskHandle_t AltCoreTask;

void setupPreferences() {
  preferences.begin(PREFERENCES_KEY, false);

  Serial.println("CURRENT PREF: " + String(preferences.getInt("currBrt", currentBrightness)));

  currentSunriseHour = preferences.getInt("currSunHr", currentSunriseHour);
  currentSunriseMinute = preferences.getInt("currSunMin", currentSunriseMinute);
  currentSunriseDuration = preferences.getInt("currSunDur", currentSunriseDuration);
  transitionDuration = preferences.getInt("transDur", transitionDuration);
  currentBrightness = preferences.getInt("currBrt", currentBrightness);
  maxBrightness = preferences.getInt("maxBrightness", maxBrightness);
  currentColor = preferences.getInt("currentColor", currentColor);
  gmtOffsetSec = preferences.getInt("gmtOffsetSec", gmtOffsetSec);

  ssid = preferences.getString("ssid", ssid);
  password = preferences.getString("password", password);
}

void setupWifi() {
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("");

  int connectionAttempt = 0;

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    leds[connectionAttempt++] = CRGB::Red;
    delay(500);
    Serial.print(".");
  }

  for (int i = 0; i < connectionAttempt; i++) {
    leds[connectionAttempt] = CRGB::Green;
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setupMDNS() {
  if (!MDNS.begin(HOST_NAME)) {
    Serial.println("Error setting up MDNS responder!");

    ESP.restart();
  }

  MDNS.addService("http", "tcp", 80);
  Serial.println("mDNS responder started");
}

void setupWebServer() {
  server.on("/heartbeat", handle_Heartbeat);

  server.on("/sunrise", handle_SunriseStart);
  server.on("/cancel-sunrise", handle_SunriseCancel);
  server.on("/set-sunrise-duration", handle_SetCurrentSunriseDuration);
  server.on("/set-sunrise-time", handle_SetSunriseTime);

  server.on("/color-palette-transition", handle_ColourPaletteTransition);
  server.on("/set-colour-palette", handle_SetColourPallet);

  server.on("/set-color", handle_SetToColor);
  server.on("/set-brightness", handle_SetBrightness);
  server.on("/set-max-brightness", handle_SetMaxBrightness);
  server.on("/set-timezone", handle_SetTimezone);
  server.on("/settings", handle_Settings);
  server.on("/calibration", handle_CalibrationCheck);

  server.begin();

  Serial.println("Web server started");
}

void setupLEDs() {
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  setAllToColor(currentColor);
  setCurrentBrightness();

  setCurrentPalletToSunrise();
  currentBlending = LINEARBLEND;

  Serial.println("Finished setting up LEDs");
}

void setupOTA() {
  ArduinoOTA.setHostname((String(HOST_NAME) + String("-OTA")).c_str());
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
      delay(2000);
      ESP.restart();
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  Serial.println("Started Arduino OTA");
}

void setupTimezone() {
  Serial.println("Setting up timezone");

  setTimezoneWithGmtOffset();
  printLocalTime();
}

void setup() {
  Serial.begin(115200);

  esp_bt_controller_disable();
  setupPreferences();
  setupWifi();
  setupTimezone();
  setupLEDs();
  setupWebServer();
  setupOTA();
  setupMDNS();
}

void loop() {
  unsigned long startMS = millis();

  ensureWifiConnected();
  ArduinoOTA.handle();
  server.handleClient();
  maybeRunSunriseIfTime();
  maybeDelayToReachTargetLoopMS(startMS);
}

// Potentially delays the device to reach target loop time (too many loops per second is bad)
void maybeDelayToReachTargetLoopMS(unsigned long startMS) {
  unsigned long durationMS = millis() - startMS;

  if (durationMS < MAIN_LOOP_MS_TARGET) {
    delay(MAIN_LOOP_MS_TARGET - durationMS);
  }
}

void handle_Heartbeat() {
  server.send(200, "text/plain", "OK");
}

void handle_SunriseStart() {
  runSunriseTransitionOnAltCore();

  server.send(200, "text/plain", "OK");
}

void handle_SunriseCancel() {
  cancelAltCoreSunrise();
  isSunriseRunning = false;

  server.send(200, "text/plain", "OK");
}

void handle_SetSunriseTime() {
  int hour = server.arg("hour").toInt();
  int minute = server.arg("minute").toInt();

  if (hour > 23 or hour < 0) {
    Serial.println("Can't set sunrise because hour is outside of 0 - 23, value: " + hour);

    server.send(400, "text/plain", "hour is outside of 0 - 23");
  } else if (minute < 0 or minute > 59) {
    Serial.println("Can't set sunrise because minute is outside of 0 - 59, value: " + minute);

    server.send(400, "text/plain", "minute is outside of 0 - 60");
  } else {
   Serial.println("Setting sunrise time to " + String(hour) + ":" + String(minute));

   currentSunriseHour = hour;
   currentSunriseMinute = minute;

   preferences.putInt("currSunHr", currentSunriseHour);
   preferences.putInt("currSunMin", currentSunriseMinute);

   server.send(200, "text/plain", String(hour) + ":" + String(minute));
  }
}

void handle_CalibrationCheck() {
  Serial.println("Running brightness check...");

  FastLED.setBrightness(MAX_BRIGHTNESS);
  setAllToColor(CRGB::Blue);
  FastLED.show();

  for (int i = MAX_BRIGHTNESS; i > 0; i--) {
    FastLED.setBrightness(i);
    Serial.println(i);
    FastLED.show();
    FastLED.delay(60);
  }

  Serial.println("Brightness check complete, running colour check...");

  FastLED.setBrightness(currentBrightness);

  int steps = 5;

  for (int r = 0x50; r <= 0xFF; r = r + steps) {
    for (int g = 0x00; g <= 0xFF; g = g + steps) {
      for (int b = 0x00; b <= 0xF0; b = b + steps) {
        String colour = String(r) + String(g) + String(b);

        setAllToColor(strtol(colour.c_str(), NULL, 16));
        FastLED.show();;
        FastLED.delay(10);
      }
    }
  }

  Serial.println("Colour check complete");

  setCurrentBrightness();

  preferences.putInt("currentColor", currentColor);
  preferences.putInt("currBrt", currentBrightness);

  server.send(200, "text/plain", "OK");
}

void handle_SetColourPallet() {
  Serial.println("Changing current colour pallet to: " + server.arg("value") + "...");

  server.send(200, "text/plain", String(server.arg("value")));
}

void handle_SetToColor() {
  Serial.println("Running transition to color " + server.arg("value") + "...");

  currentColor = strtol(server.arg("value").c_str(), NULL, 16);

  setAllToColor(currentColor);
  FastLED.show();

  preferences.putInt("currentColor", currentColor);

  Serial.println("Color transition complete");

  server.send(200, "text/plain", "OK");
}

void handle_SetBrightness() {
  int brightnessRequested = server.arg("value").toInt();

  if (brightnessRequested > MAX_BRIGHTNESS) {
    Serial.println("Brightness requested is too high");

    server.send(400, "text/plain", "Brightness requested is too high");
  } else if (brightnessRequested < 0) {
    server.send(400, "text/plain", "Brightness requested is too low");
  } else {
    transitionCurrentBrightness(brightnessRequested);

    preferences.putInt("currBrt", currentBrightness);

    server.send(200, "text/plain", String(currentBrightness));
  }
}

void handle_SetMaxBrightness() {
  int brightnessRequested = server.arg("value").toInt();

  if (brightnessRequested > MAX_BRIGHTNESS) {
    Serial.println("Max brightness requested is too high");

    server.send(200, "text/plain", "Max brightness requested is too high");
  } else {
    maxBrightness = brightnessRequested;
    
    preferences.putInt("maxBrightness", maxBrightness);

    Serial.println("Set max brightness to " + String(brightnessRequested));

    server.send(200, "text/plain", String(brightnessRequested));
  }
}

void handle_SetCurrentSunriseDuration() {
  currentSunriseDuration = server.arg("value").toInt();

  preferences.putInt("currentSunriseDuration", currentSunriseDuration);

  Serial.println("Set sunrise duration to " + String(currentSunriseDuration));

  server.send(200, "text/plain", String(currentSunriseDuration));
}

void handle_SetTimezone() {
  gmtOffsetSec = server.arg("value").toInt();

  Serial.println("Set GMT offset to " + String(gmtOffsetSec / 3600));

  setTimezoneWithGmtOffset();

  preferences.putInt("gmtOffsetSec", gmtOffsetSec);

  server.send(200, "text/plain", String(gmtOffsetSec));
}

void handle_ColourPaletteTransition() {
  int transitionDuration = server.arg("duration").toInt();
  int transitionStartStep = server.arg("startStep").toInt();
  int transitionEndStep = server.arg("endStep").toInt();

  if (transitionStartStep < 0 or transitionStartStep >= MAX_COLOR_INDEX or transitionEndStep < 0 or transitionEndStep >= MAX_COLOR_INDEX) {
    server.send(400, "text/plain", "End Step must be less than or equal to " + String(MAX_COLOR_INDEX) + " and greater than or equal to 0");
  } else {
    int delayPerStep = floor(transitionDuration / abs(transitionStartStep - transitionEndStep));

    if (transitionStartStep > transitionEndStep) {
      Serial.println("Transitioning colour palette down from " + String(transitionStartStep) + " to " + String(transitionEndStep) + "...");

      for (int i = transitionStartStep; i > transitionEndStep; i--) {
        fill_solid(leds, NUM_LEDS, colourForPalletIndex(i));
        FastLED.delay(delayPerStep);
      }
    }

    if (transitionStartStep < transitionEndStep) {
      Serial.println("Transitioning colour palette up from " + String(transitionStartStep) + " to " + String(transitionEndStep) + "...");

      for (int i = transitionStartStep; i < transitionEndStep; i++) {
        fill_solid(leds, NUM_LEDS, colourForPalletIndex(i));
        FastLED.delay(delayPerStep);
      }
    }

    currentColor = colourForPalletIndex(transitionEndStep);

    Serial.println("Transitioning colour palette complete");

    server.send(200, "text/plain", "OK");
  }
}

void handle_Settings() {
  server.send(200, "application/json", currentSettings());
}

String currentSettings() {
  return "{\"currentBrightness\": " + String(currentBrightness) +
    ", \"maxBrightness\": " + maxBrightness +
    ", \"MAX_BRIGHTNESS\": " + MAX_BRIGHTNESS +
    ", \"currentSunriseDuration\": " + currentSunriseDuration +
    ", \"sunriseTime\": " + "\"" + String(currentSunriseHour) + ":" + String(currentSunriseMinute) + "\"" +
    ", \"isSunriseRunning\": " + (isSunriseRunning ? "true" : "false") +
    ", \"version\": " + "\"" + VERSION + "\"" +
    ", \"wifiStrength\": " + "\"" + WiFi.RSSI() + "\"" +
    ", \"time\": " + "\"" + getLocalTimeString() + "\"" +
    ", \"timezone\": " + "\"" + "GMT-" + String(gmtOffsetSec / 3600) + "\"" +
    ", \"currentColor\": " + currentColor + "}";
}

void ensureWifiConnected() {
  unsigned long currentMS = millis();

  if ((WiFi.status() != WL_CONNECTED) && ((wifiLastReconnectionMS == 0) || (currentMS - wifiLastReconnectionMS >= WIFI_RECONNECTION_INTERVAL))) {
    Serial.println("Reconnecting to WiFi...");
    WiFi.disconnect();
    WiFi.reconnect();
    wifiLastReconnectionMS = currentMS;
  }

  if (WiFi.status() == WL_CONNECTED && wifiLastReconnectionMS != 0) {
    wifiLastReconnectionMS = 0;
  }
}

void maybeRunSunriseIfTime() {
  if (!isSunriseRunning) {
    time_t now = time(NULL);
    struct tm *tm_struct = localtime(&now);
  
    if (tm_struct->tm_hour == currentSunriseHour and tm_struct->tm_min == currentSunriseMinute) {
      Serial.println("It's sunrise time!!! Running sunrise transition");
  
      runSunriseTransitionOnAltCore();
    }
  }
}

CRGB colourForPalletIndex(int currentColorIndex) {
  return ColorFromPalette(currentPalette, currentColorIndex, currentBrightness, currentBlending);
}

void setAllToColor(int color) {
  currentColor = color;

  fill_solid(leds, NUM_LEDS, color);
}

void transitionCurrentBrightness(int wantedBrightness) {
  float scaledWantedBrightness = floor((float)wantedBrightness / (float)MAX_BRIGHTNESS * (float)maxBrightness);

  if (currentBrightness > scaledWantedBrightness) {
    int diff = currentBrightness - scaledWantedBrightness;
    int stepDuration = floor(transitionDuration / diff);

    Serial.println("Transitioning brightness down " + String(diff) + " steps from " + currentBrightness + "...");

    for (currentBrightness; currentBrightness > scaledWantedBrightness; currentBrightness--) {
      setCurrentBrightness();
      FastLED.delay(stepDuration);
    }
  }

  if (currentBrightness < scaledWantedBrightness) {
    int diff = scaledWantedBrightness - currentBrightness;
    int stepDuration = floor(transitionDuration / diff);

    Serial.println("Transitioning brightness up " + String(diff) + " steps from " + currentBrightness + "...");

    for (currentBrightness; currentBrightness < scaledWantedBrightness; currentBrightness++) {
      setCurrentBrightness();
      FastLED.delay(stepDuration);
    }
  }

  Serial.println("Brightness transition to " + String(wantedBrightness) + " complete");
}

void setCurrentBrightness() {
  FastLED.setBrightness(currentBrightness);
  FastLED.show();
}

void setCurrentPalletToSunrise() {
  CRGB black = CRGB::Black;
  CRGB candle = 0xFF3F04;
  CRGB candle_second = 0xFF580B;
  CRGB candle_third = 0xFF7417;
  CRGB candle_fourth = 0xFF8B27;
  CRGB candle_fifth = 0xFFA339;
  CRGB candle_sixth = 0xFFB54E;
  CRGB candle_seven = 0xFFC964;
  CRGB candle_eight = 0xFFD97A;
  CRGB candle_nine = 0xFFEB90;
  CRGB almost_day = 0xFFF8A7;
  CRGB full_day = 0xF8FFB7;

  setCurrentPallet16(
    black,  black,  candle,  candle,
    candle_second, candle_third, candle_fourth, candle_fifth, candle_fifth,
    candle_sixth, candle_sixth,  candle_seven,  candle_eight,  candle_nine,
    almost_day, full_day
  );
}

void setCurrentPallet16(
  CRGB colour_1, CRGB colour_2, CRGB colour_3, CRGB colour_4,
  CRGB colour_5, CRGB colour_6, CRGB colour_7, CRGB colour_8,
  CRGB colour_9, CRGB colour_10, CRGB colour_11, CRGB colour_12,
  CRGB colour_13, CRGB colour_14, CRGB colour_15, CRGB colour_16
) {
  currentPalette = CRGBPalette16(
    colour_1, colour_2, colour_3, colour_4,
    colour_5, colour_6, colour_7, colour_8,
    colour_9, colour_10, colour_11, colour_12,
    colour_13, colour_14, colour_15, colour_16
  );
}

void setTimezoneWithGmtOffset() {
  configTime(gmtOffsetSec, DAYLIGHT_SAVINGS_OFFSET_SEC, NTP_SERVER, NULL, NULL);
}

void printLocalTime(){
  struct tm timeInfo;

  if (!getLocalTime(&timeInfo)){
    Serial.println("Failed to obtain time");
    return;
  }

  Serial.println(&timeInfo, "%A, %B %d %Y %H:%M:%S");
}

String getLocalTimeString() {
  struct tm timeInfo;

  if (!getLocalTime(&timeInfo)){
    return "Failed to obtain time";
  } else {
    char timeInfoStr[64];

    strftime(timeInfoStr, 64, "%A, %B %d %Y %H:%M:%S", &timeInfo);

    return timeInfoStr;
  }
}

void runSunriseTransition() {
  // TODO: Make this brightness start at 0 and scale with day to MAX_BRIGHNESS
  currentBrightness = MAX_BRIGHTNESS;

  FastLED.setBrightness(currentBrightness);

  setCurrentPalletToSunrise();

  int delayPerStep = floor(currentSunriseDuration / MAX_COLOR_INDEX);

  for (int i = 0; i < MAX_COLOR_INDEX; i++) {
    fill_solid(leds, NUM_LEDS, colourForPalletIndex(i));
    FastLED.delay(delayPerStep);
  }

  currentColor = colourForPalletIndex(MAX_COLOR_INDEX);
  isSunriseRunning = false;
  FastLED.show();
}

void AltCorSunriseTransition(void * pvParameters) {
  Serial.println("Running color transition on core " + xPortGetCoreID());

  isSunriseRunning = true;   
  runSunriseTransition();
  isSunriseRunning = false;
  cancelAltCoreSunrise();
}

void runSunriseTransitionOnAltCore() {
  xTaskCreatePinnedToCore(
    AltCorSunriseTransition,   /* Task function. */
    "Sunrise Transition",     /* name of task. */
    10000,       /* Stack size of task */
    NULL,        /* parameter of the task */
    0,           /* priority of the task */
    &AltCoreTask,      /* Task handle to keep track of created task */
    0
  );
}

void cancelAltCoreSunrise() {
  vTaskDelete(AltCoreTask);
}
