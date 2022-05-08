#include <bluefruit.h>
#include <FastLED.h>
//#include <Adafruit_LittleFS.h>
//#include <InternalFileSystem.h>

#define DEVICE_APPEARANCE 1361 // Proximity Device
#define SOFTWARE_VERSION "0.1.0"
#define MANUFACTURER_NAME "Mika Software Inc."
#define MODEL_NAME "Proximity Necklace nRF52"
#define ERROR_LED_INTERVAL 100
#define CONN_LED_INTERVAL 1000
#define FOUND_LED_DURATION 3000
#define LED_BLINK_INTERVAL_STEP 450
#define LED_RSSI_STEPS 6
#define LED_DATA_PIN 4
#define DISCONNECTION_BLINKS 4
#define RSSI_AVG_NUM 2
#define MAX_NECKLACES 30

const uint8_t PROXIMITY_NECKLACE_BLE_SERVICE_UUID[] = {
  0xFE, 0x3A, 0xAF, 0xFD, 0x68, 0xE1, 0x43, 0xCD,
  0x90, 0x66, 0xB5, 0xDB, 0xC3, 0x8A, 0x97, 0xA6
};

const uint8_t PROXIMITY_NECKLACE_BLE_LED_COLOR_CHARACTERISTIC[] = {
  0x36, 0x2A, 0xF7, 0xD5, 0x94, 0x6C, 0x40, 0x56,
  0x91, 0x77, 0xA6, 0x2D, 0xE6, 0xA4, 0xF4, 0xF4
};

// BLE Service
BLEDfu  bledfu;  // OTA DFU service
BLEDis  bledis;  // device information
BLEUart bleuart;
//BLEBas  blebas;

BLEService bleNecklackService = BLEService(PROXIMITY_NECKLACE_BLE_SERVICE_UUID);
BLECharacteristic bleNecklaceColorChar(PROXIMITY_NECKLACE_BLE_LED_COLOR_CHARACTERISTIC, BLERead);

BLEClientService        bleNecklaceClientService(PROXIMITY_NECKLACE_BLE_SERVICE_UUID);
BLEClientCharacteristic bleNecklaceColorClientChar(PROXIMITY_NECKLACE_BLE_LED_COLOR_CHARACTERISTIC);

String UNIQUE_ID = String(getMcuUniqueID());
char deviceName[24];
char deviceColour[] = "FF00FF";
char connectedColour[] = "0000FF";
int maxRSSI = 70;
int minRSSI = 40;
bool recentlyFound = false;
String recentlyConnectedMacs[MAX_NECKLACES];

CRGB leds[1];
CRGB currentLEDColour = CRGB::Green;
CRGBPalette16 rainbowPallet = CRGBPalette16(
  CRGB::Red,
  CRGB::Red,
  CRGB::Red,
  CRGB::Orange,
  CRGB::Orange,
  CRGB::Yellow,
  CRGB::Yellow,
  CRGB::Green,
  CRGB::Green,
  CRGB::Blue,
  CRGB::Blue,
  CRGB::Indigo,
  CRGB::Indigo,
  CRGB::Violet,
  CRGB::Violet,
  CRGB::Violet
);

void log(const String message) {
  Serial.println(message);

  if (bleuart.notifyEnabled()) {
    bleuart.write(message.c_str());
  }
}

void log(const char* message) {
  Serial.println(message);

  if (bleuart.notifyEnabled()) {
    bleuart.write(message);
  }
}

void blinkLED(int duration, const int interval) {
  while (duration > 0) {
    FastLED.setBrightness(100);
    FastLED.show();
    FastLED.delay(interval);
    FastLED.setBrightness(0);
    FastLED.show();
    FastLED.delay(interval);

    duration -= interval * 2;
  }
}

void setupSerial() {
  Serial.begin(115200);

  while (!Serial) delay(10);
}

void setupBluetooth() {
  Bluefruit.autoConnLed(true);

  log("Initializing bluefruit in Central & Periphiral mode...");

  if (!Bluefruit.begin(1, 1)) {
    log("Unable to init Bluefruit");

    while (1) {
      FastLED.setBrightness(0);
      FastLED.show();
      FastLED.delay(ERROR_LED_INTERVAL);
      FastLED.setBrightness(100);
      FastLED.show();
      FastLED.delay(ERROR_LED_INTERVAL);
    }
  } else {
    log("Bluefruit initialized");
  }

  Bluefruit.setTxPower(4);
  Bluefruit.setName(deviceName);
  Bluefruit.setConnLedInterval(CONN_LED_INTERVAL);
  Bluefruit.setAppearance(DEVICE_APPEARANCE);
  
  Bluefruit.Periph.setConnectCallback(periph_connect_callback);
  Bluefruit.Periph.setDisconnectCallback(periph_disconnect_callback);

  Bluefruit.Central.setConnectCallback(central_connect_callback);
  Bluefruit.Central.setDisconnectCallback(central_disconnect_callback);

  log("Starting UART...");
  bleuart.begin();

  log("Starting OTA ...");
  bledfu.begin();

  log("Starting device information...");
  bledis.begin();
  bledis.setManufacturer(MANUFACTURER_NAME);
  bledis.setModel(MODEL_NAME);
  bledis.setSoftwareRev(SOFTWARE_VERSION);
  bledis.setSerialNum(UNIQUE_ID.c_str());

//  log("Starting battery service...");
//  blebas.begin();
//  blebas.notify(100);
  
  log("Starting necklace client service...");
  bleNecklaceClientService.begin();
  bleNecklaceColorClientChar.begin();

  log("Starting necklace service...");
  bleNecklackService.begin();
  
//  bleNecklaceColorChar.addDescriptor(2902, "Necklace Color", BLERead);
  bleNecklaceColorChar.setUserDescriptor("Necklace Color");
  bleNecklaceColorChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  bleNecklaceColorChar.setProperties(CHR_PROPS_READ);
  bleNecklaceColorChar.setFixedLen(6);
  bleNecklaceColorChar.write(deviceColour, 6);
  bleNecklaceColorChar.begin();
}

void startScanner(void) {
  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(10000, 500); // in unit of 0.625 ms
  Bluefruit.Scanner.filterUuid(bleNecklackService.uuid);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.start(0);  // 0 = Don't stop scanning after n seconds
}

void startAdv(void) {
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addUuid(bleNecklackService.uuid);

  Bluefruit.Advertising.addService(bleNecklackService);
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.addService(bledis);
//  Bluefruit.Advertising.addService(blebas);
  Bluefruit.Advertising.addService(bledfu);

  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(10000, 500);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(5);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
}

void setupLEDs() {
  FastLED.addLeds<WS2812B, 9, GRB>(leds, 1);
  FastLED.setBrightness(0);
  fill_solid(leds, 1, currentLEDColour);
  FastLED.show();
}

void setupRecentlyConnected() {
  log ("Setting up and clearing recently connected necklaces");
  
  for (int i = 0; i < MAX_NECKLACES; i++) {
    recentlyConnectedMacs[i] = String("");
  }
}

void setup() {
  setupSerial();

  String("LED Necklace - " + UNIQUE_ID).toCharArray(deviceName, 24);

  log("Starting Up " + String(deviceName) + "...");

  setupLEDs();
  setupRecentlyConnected();
  
  log("\n---- Bluetooth Setup Start ----");
  setupBluetooth();
  log("---- Bluetooth Setup End ----\n");

  startScanner();
  log("---- Bluetooth Scanning Started ----");

  startAdv();
  log("---- Bluetooth Advertising Started ----");
}

bool isRecentlyFound(uint16_t connHandle) {
  char macBuffer[32];
  bool wasFound = false;
  ble_gap_addr_t peer_addr = Bluefruit.Connection(connHandle)->getPeerAddr();
  
  snprintf(macBuffer, 19, "%02X:%02X:%02X:%02X:%02X:%02X",
           peer_addr.addr[5],
           peer_addr.addr[4],
           peer_addr.addr[3],
           peer_addr.addr[2],
           peer_addr.addr[1],
           peer_addr.addr[0]
          );

  log("Checking recently found in " + String(MAX_NECKLACES) + " items : " + String(macBuffer));

  for (int i = 0; i < MAX_NECKLACES; i++) {    
    if (recentlyConnectedMacs[i] == macBuffer) {
      wasFound = true; 
    }
  }
  
  return wasFound;
}

void pushFound(uint16_t connHandle) {
  char macBuffer[32];
  ble_gap_addr_t peer_addr = Bluefruit.Connection(connHandle)->getPeerAddr();
  
  snprintf(macBuffer, 19, "%02X:%02X:%02X:%02X:%02X:%02X",
           peer_addr.addr[5],
           peer_addr.addr[4],
           peer_addr.addr[3],
           peer_addr.addr[2],
           peer_addr.addr[1],
           peer_addr.addr[0]
          );

  log("Pushing found MAC address: " + String(macBuffer));
  
  for (int i = 0; i < MAX_NECKLACES; i++) {
    if (recentlyConnectedMacs[i] == macBuffer) {
      log("Already found this MAC address");
      
      return;
    } else if (recentlyConnectedMacs[i] == String("")) {
      log("Set " + String(macBuffer) + " address to idx: " + String(i));
      
      recentlyConnectedMacs[i] = String(macBuffer);

      return;
    }
  }

  log("Already found the maximum amount of necklaces, removing first 5 to be found again");

  recentlyConnectedMacs[0] = String("");
  recentlyConnectedMacs[1] = String("");
  recentlyConnectedMacs[2] = String("");
  recentlyConnectedMacs[3] = String("");
  recentlyConnectedMacs[4] = String("");
}

// Callbacks

void scan_callback(ble_gap_evt_adv_report_t* report) {
  // Since we configure the scanner with filterUuid()  
  // Scan callback only invoked for device with bleuart service advertised
  // Connect to the device with bleuart service in advertising packet
  //  Bluefruit.Central.connect(report);
  char macBuffer[32];
  uint8_t buffer[32];
  uint8_t len = 0;
  int rssi = abs(report->rssi);

  snprintf(macBuffer, 19, "%02X:%02X:%02X:%02X:%02X:%02X",
           report->peer_addr.addr[5],
           report->peer_addr.addr[4],
           report->peer_addr.addr[3],
           report->peer_addr.addr[2],
           report->peer_addr.addr[1],
           report->peer_addr.addr[0]
          );

  log("Found necklace " + String(report->rssi) + " far away, MAC: " + String(macBuffer));

  if (rssi > maxRSSI) {
    maxRSSI = rssi;
  } else if (rssi < minRSSI) {
    minRSSI = rssi;
  }

  if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, buffer, sizeof(buffer))) {
    log("Short Name: " + String((char *)buffer));
    memset(buffer, 0, sizeof(buffer));
  }

  /* Complete Local Name */
  if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, buffer, sizeof(buffer))) {
    log("Complete Name: " + String((char *)buffer));
    memset(buffer, 0, sizeof(buffer));
  }

  /* TX Power Level */
  if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_TX_POWER_LEVEL, buffer, sizeof(buffer))) {
    log("TX Power Level: " + String(buffer[0]));
    memset(buffer, 0, sizeof(buffer));
  }

  Bluefruit.Central.connect(report);
  
//  Bluefruit.Scanner.resume();

  memset(macBuffer, 0, sizeof(macBuffer));
}

bool inRange = false;
bool isConnected = false;

void periph_connect_callback(uint16_t connHandle) {
  if (isConnected) {
    log("[Peripheral] Already at max connections, exiting....");  

    Bluefruit.disconnect(connHandle);
    
    return;
  } else if (isRecentlyFound(connHandle)) {
    log("[Peripheral] Already found this necklace, exiting....");  

    Bluefruit.disconnect(connHandle);
    return;
  }
  
  char centralName[32] = { 0 };
  
  isConnected = true;
  inRange = false;
  
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(connHandle);
  
  connection->getPeerName(centralName, sizeof(centralName) - 1);
  
  connection->monitorRssi();
  
  setConnectedColor(connHandle, "Peripheral");
          
  log("[Peripheral] Connected to " + String(centralName) + ", monitoring till close or disconnected");
}

void periph_disconnect_callback(uint16_t connHandle, uint8_t reason) {
  (void) connHandle;
  (void) reason;
  isConnected = false;

  Bluefruit.Connection(connHandle)->stopRssi();
  log("[Peripheral] Disconnected, reason = 0x" + String(HEX));

  if (recentlyFound) {
    recentlyFound = false;
  } else {
    disconnectBlink();
  }
}

void central_connect_callback(uint16_t connHandle) {
  if (isConnected) {
    log("[Central] Already at max connections, exiting....");  

    Bluefruit.disconnect(connHandle);
    return;
  } else if (isRecentlyFound(connHandle)) {
    log("[Central] Already found this necklace, exiting....");  

    Bluefruit.disconnect(connHandle);
    return;
  }
  
  isConnected = true;
  inRange = false;

  char periphName[32] = { 0 };
  
  Bluefruit.Connection(connHandle)->getPeerName(periphName, sizeof(periphName) - 1);
  Bluefruit.Connection(connHandle)->monitorRssi();
  
  setConnectedColor(connHandle, "Central");
  
  log("[Central] Connected to " + String(periphName) + ", monitoring till close or disconnected");
  
}

void central_disconnect_callback(uint16_t connHandle, uint8_t reason) {
  (void) connHandle;
  (void) reason;

  isConnected = false;

  log("[Central] Disconnected");
  Bluefruit.Connection(connHandle)->stopRssi();

  if (recentlyFound) {
    recentlyFound = false;
  } else {
    disconnectBlink();
  }
}

int rssiForAvg[RSSI_AVG_NUM];

// 0 is Central BLE
// 1 is PeripheralBLE
// It's necessary to manually set this because it automatically
// infers the wrong connection otherwise (╯°□°）╯︵ ┻━┻
void loop() {
  if (Bluefruit.connected(0)) {    
    getRssiAndCheckForCloseness(0);
  } else if (Bluefruit.connected(1)) {    
    getRssiAndCheckForCloseness(1);
  }
}

void setConnectedColor(uint16_t connHandle, String side) {
  if (bleNecklaceClientService.discover(connHandle)) {
    log("[" + side + "] Found Necklace Service"); 
    
    if (bleNecklaceColorClientChar.discover()) {
      bleNecklaceColorClientChar.read(connectedColour, 6);
      currentLEDColour = strtol(connectedColour, NULL, 16);

      log("[" + side + "] Found Necklace Color Charactaristic: " + String(connectedColour));
    } else {
     log("[" + side + "] Couldn't find Necklace Color Charactaristic"); 
    }    
  } else {
    log("[" + side + "] Couldn't find Necklace Service");
  }
}

void getRssiAndCheckForCloseness(uint16_t conn_hdl) {
  // TODO: Change to check for not recently connected instead
  if (!inRange) {
    BLEConnection* connection = Bluefruit.Connection(conn_hdl);
    
    int8_t rssi = connection->getRssi();
    
    if (rssi != 0) {
      if (abs(rssi) > maxRSSI) {
        maxRSSI = abs(rssi);
      } else if (abs(rssi) < minRSSI) {
        minRSSI = abs(rssi);
      }
      
      runClosenessCheck(abs(rssi), conn_hdl);
    } else {
      delay(2500);
    }
  }
}

void resetRssi() {
  memset(rssiForAvg, 0, sizeof(rssiForAvg));
}

bool isRssiAvgFilled() {
  bool isFilled = true;
  
  for (int i = 0; i < RSSI_AVG_NUM; i++) {
    if (rssiForAvg[i] == 0) {
      isFilled = false;
    }
  }
  
  return isFilled;
}

void pushRssi(int rssi) {
  int currentIndex = 0;
  int unsetIndex = 0;
  
  while (true) {
    if (rssiForAvg[currentIndex] == 0) {
      unsetIndex = currentIndex;
      break;
    } else {
      currentIndex += 1;
    }
  }
  
  rssiForAvg[unsetIndex] = rssi;
}

int averageRssi() {
  int sum = 0;
  
  for (int i = 0; i < RSSI_AVG_NUM; i++) {
    sum += rssiForAvg[i];  
  }
  
  return sum / RSSI_AVG_NUM;
}

void runClosenessCheck(const int rssi, uint16_t conn_hdl) {
  if (isRssiAvgFilled()) {
    toggleLightWithAvgRssi(conn_hdl);
  } else {
    pushRssi(rssi);

    if (isRssiAvgFilled()) {
      toggleLightWithAvgRssi(conn_hdl);
    }
  }
}

void toggleLightWithAvgRssi(uint16_t conn_hdl) {
  BLEConnection* connection = Bluefruit.Connection(conn_hdl);
  int avgRssi = averageRssi();
  int rssiDelay = blinkDelayInterval(avgRssi);  
  char connectedDeviceName[32];
  
  resetRssi();
  connection->getPeerName(connectedDeviceName, sizeof(connectedDeviceName));  
  
  log(String(connectedDeviceName) + " is " + String(avgRssi) + " away " + String(rssiDelay) + "ms delay");

  fill_solid(leds, 1, currentLEDColour);
  turnOnLED(rssiDelay / 3 * 2);
     
  if (avgRssi <= minRSSI) {
    log("Necklace found!!");
    
    inRange = true;
    recentlyFound = true;
    
    pushFound(conn_hdl);
    foundBlink();

    if (avgRssi < minRSSI) {
      minRSSI = avgRssi;
    }

    Bluefruit.disconnect(conn_hdl);
  } else if (isConnected) {
    turnOffLED(rssiDelay / 3);
  } else {
    turnOffLED(0);
  }

  memset(connectedDeviceName, 0, sizeof(connectedDeviceName));
}

//
//      char macBuffer[32];
//      ble_gap_addr_t peer_addr = connection->getPeerAddr();
//    
//      snprintf(macBuffer, 19, "%02X:%02X:%02X:%02X:%02X:%02X",
//               peer_addr.addr[5],
//               peer_addr.addr[4],
//               peer_addr.addr[3],
//               peer_addr.addr[2],
//               peer_addr.addr[1],
//               peer_addr.addr[0]
//              );

void turnOnLED(const int duration) {
  const int stepsPerTick = duration / 100;
  
  for (int i = 0; i < 100; i++) {
    FastLED.setBrightness(i);
    FastLED.show();
    delay(stepsPerTick);
  }
  
  FastLED.setBrightness(100);
  FastLED.show();
}

void turnOffLED(const int duration) {
  const int stepsPerTick = duration / 100;
  
  for (int i = 100; i > 0; i--) {
    FastLED.setBrightness(i);
    FastLED.show();
    delay(stepsPerTick);
  }

  FastLED.setBrightness(0);
  FastLED.show();
}

int blinkDelayInterval(int8_t rssi) {
  rssi = max(rssi, minRSSI);
  
  const float percentageToMaxRSSI = ((float)rssi - minRSSI) / (float)(maxRSSI - minRSSI);
  int maxLedBlinkInterval = LED_BLINK_INTERVAL_STEP * (maxRSSI / LED_RSSI_STEPS);
  
  return max(ceil(percentageToMaxRSSI * maxLedBlinkInterval), 25);
}

void disconnectBlink() {
  fill_solid(leds, 1, CRGB::Red);

  for (int i = 0; i < DISCONNECTION_BLINKS; i++) {
    turnOnLED(200);
    turnOffLED(200);
  }

  fill_solid(leds, 1, currentLEDColour);
  FastLED.setBrightness(0);
  FastLED.show();
}

void foundBlink() {
  int steps = 255;
  int delayPerStep = FOUND_LED_DURATION / steps;
  
  for (int i = 0; i < 255; i++) {
    fill_solid(leds, 1, ColorFromPalette(rainbowPallet, i, 100, LINEARBLEND));
    FastLED.show();
    delay(delayPerStep);
  }
  
  turnOffLED(1000);
  fill_solid(leds, 1, currentLEDColour);
}
