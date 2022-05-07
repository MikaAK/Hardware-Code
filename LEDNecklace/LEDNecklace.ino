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
#define RSSI_AVG_NUM 3

const uint8_t PROXIMITY_NECKLACE_BLE_SERVICE_UUID[] = {
  0xFE, 0x3A, 0xAF, 0xFD, 0x68, 0xE1, 0x43, 0xCD,
  0x90, 0x66, 0xB5, 0xDB, 0xC3, 0x8A, 0x97, 0xA6
};

const uint8_t PROXIMITY_NECKLACE_BLE_LED_COLOR_CHARACTERISTIC[] = {
  0x36, 0x2A, 0xF7, 0xD5, 0x94, 0x6C, 0x40, 0x56,
  0x91, 0x77, 0xA6, 0x2D, 0xE6, 0xA4, 0xF4, 0xF4
};

const uint16_t LED_COLOR[3] = {255, 120, 200};

// BLE Service
BLEDfu  bledfu;  // OTA DFU service
BLEDis  bledis;  // device information
BLEUart bleuart;
//BLEBas  blebas;

BLEService bleNecklackService = BLEService(PROXIMITY_NECKLACE_BLE_SERVICE_UUID);
BLECharacteristic bleNecklaceDeviceColorCharacteristic(PROXIMITY_NECKLACE_BLE_LED_COLOR_CHARACTERISTIC, BLERead);

String UNIQUE_ID = String(getMcuUniqueID());
char deviceName[24];
int maxRSSI = 70;
int minRSSI = 40;
bool recentlyFound = false;

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

  log("Starting necklace service...");
  bleNecklackService.begin();
//  bleNecklaceDeviceColorCharacteristic.addDescriptor(2902, "Necklace Color", BLERead);
  bleNecklaceDeviceColorCharacteristic.setUserDescriptor("Necklace Colorssssss");
  bleNecklaceDeviceColorCharacteristic.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  bleNecklaceDeviceColorCharacteristic.setProperties(CHR_PROPS_READ);
  bleNecklaceDeviceColorCharacteristic.setFixedLen(2 * 3);
  bleNecklaceDeviceColorCharacteristic.write(LED_COLOR, 2 * 3);

  bleNecklaceDeviceColorCharacteristic.begin();
}

void startScanner(void) {
  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(10000, 500); // in unit of 0.625 ms
  Bluefruit.Scanner.filterUuid(bleNecklackService.uuid);
  Bluefruit.Scanner.useActiveScan(true);
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

void setup() {
  setupSerial();

  String("LED Necklace - " + UNIQUE_ID).toCharArray(deviceName, 24);

  log("Starting Up " + String(deviceName) + "...");

  setupLEDs();
  
  log("\n---- Bluetooth Setup Start ----");
  setupBluetooth();
  log("---- Bluetooth Setup End ----\n");

  startScanner();
  log("---- Bluetooth Scanning Started ----");

  startAdv();
  log("---- Bluetooth Advertising Started ----");
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

void periph_connect_callback(uint16_t conn_handle) {
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  connection->monitorRssi();
  
  char central_name[32] = { 0 };
  char macBuffer[32];
  
  connection->getPeerName(central_name, sizeof(central_name));
          
  log("[Peripheral] Connected to " + String(central_name) + ", monitoring till close or disconnected");

  inRange = false;
  
  memset(central_name, 0, sizeof(central_name));
}

void periph_disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  (void) reason;
  
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  log("[Peripheral] Disconnected, reason = 0x" + String(HEX));

  connection->stopRssi();

  if (recentlyFound) {
    recentlyFound = false;
  } else {
    disconnectBlink();
  }
}

void central_connect_callback(uint16_t conn_handle) {
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  connection->monitorRssi();
  
  char peer_name[32] = { 0 };
  connection->getPeerName(peer_name, sizeof(peer_name));

  log("[Central] Connected to " + String(peer_name) + ", monitoring till close or disconnected");
  
  inRange = false;

  memset(peer_name, 0, sizeof(peer_name));
}

void central_disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  (void) reason;

  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  log("[Central] Disconnected");
  connection->stopRssi();
}

int rssiForAvg[RSSI_AVG_NUM];

// 0 is Central BLE
// 1 is PeripheralBLE
void loop() {
  if (Bluefruit.connected(0)) {    
    getRssiAndCheckForCloseness(0);
  } else if (Bluefruit.connected(1)) {    
    getRssiAndCheckForCloseness(1);
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
  return is_filled;
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
    BLEConnection* connection = Bluefruit.Connection(conn_hdl);
    int avgRssi = averageRssi();
    int rssiDelay = blinkDelayInterval(avgRssi);  
    char deviceName[32] = { 0 };
    
    resetRssi();
    connection->getPeerName(deviceName, sizeof(deviceName));  
    
    log(String(deviceName) + " is " + String(avgRssi) + " away " + String(rssiDelay) + "ms delay");
    
    turnOnLED(rssiDelay / 3 * 2);
       
    if (avgRssi <= minRSSI) {
      log("Necklace found!!");
      
      inRange = true;
      recentlyFound = true;
      
      foundBlink();

      Bluefruit.disconnect(conn_hdl);
    } else {
      turnOffLED(rssiDelay / 3);
    }
  } else {
    pushRssi(rssi);
  }

  memset(deviceName, 0, sizeof(deviceName));
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
