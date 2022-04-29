#include <bluefruit.h>
#include <FastLED.h>
//#include <Adafruit_LittleFS.h>
//#include <InternalFileSystem.h>

#define BLE_APPEARANCE_PROXIMITY_DEVICE 1859
#define SOFTWARE_VERSION "0.1.0"
#define MANUFACTURER_NAME "Mika Software Inc."
#define MODEL_NAME "Proximity Necklace nRF52"
#define CONN_LED_INTERVAL 1000
#define LED_DATA_PIN 4

const uint8_t BLE_SERVICE_PROXIMITY_NECKLACE[] = {
  0xFE, 0x3A, 0xAF, 0xFD, 0x68, 0xE1, 0x43, 0xCD,
  0x90, 0x66, 0xB5, 0xDB, 0xC3, 0x8A, 0x97, 0xA6
};

const uint8_t BLE_CHR_PROXIMITY_NECKLACE[] = {
  0x36, 0x2A, 0xF7, 0xD5, 0x94, 0x6C, 0x40, 0x56,
  0x91, 0x77, 0xA6, 0x2D, 0xE6, 0xA4, 0xF4, 0xF4
};

// BLE Service
//BLEDfu  bledfu;  // OTA DFU service
BLEDis  bledis;  // device information
BLEUart bleuart;
BLEBas  blebas;

BLEService bleNecklackService = BLEService(BLE_SERVICE_PROXIMITY_NECKLACE);

String UNIQUE_ID = String(getMcuUniqueID());
char deviceName[24];

CRGB leds[1];
CRGB currentLEDColour = CRGB::Blue;
int currentLEDBrightness = 30;

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

void setupSerial() {
  Serial.begin(115200);

  while (!Serial) delay(10);
}

void setupBluetooth() {
  Bluefruit.autoConnLed(true);

  log("Begin bluefruit in Central & Periphiral mode...");
  Bluefruit.begin(1, 1);
  Bluefruit.setTxPower(4);
  Bluefruit.setName(deviceName);
  Bluefruit.setConnLedInterval(CONN_LED_INTERVAL);

  Bluefruit.Periph.setConnectCallback(periph_connect_callback);
  Bluefruit.Periph.setDisconnectCallback(periph_disconnect_callback);

  Bluefruit.Central.setConnectCallback(central_connect_callback);
  Bluefruit.Central.setDisconnectCallback(central_disconnect_callback);

  
  log("Starting UART...");
  bleuart.begin();
  
//  log("Starting OTA ...");
//  bledfu.begin();

  log("Starting device information...");
  
  bledis.setManufacturer(MANUFACTURER_NAME);
  bledis.setModel(MODEL_NAME);
  bledis.setSoftwareRev(SOFTWARE_VERSION);
  bledis.begin();

  log("Starting battery service...");
  blebas.begin();
  blebas.notify(100);
}

void startScanner(void) {
  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(10000, 500); // in unit of 0.625 ms
  Bluefruit.Scanner.filterUuid(bleuart.uuid);
  Bluefruit.Scanner.useActiveScan(true);
  Bluefruit.Scanner.start(0);                   // 0 = Don't stop scanning after n seconds
}

void startAdv(void) {
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();

  Bluefruit.Advertising.addService(bleNecklackService);
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.addService(bledis);
  Bluefruit.Advertising.addService(blebas);
//  Bluefruit.Advertising.addService(bledfu);

  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(10000, 500);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
}

void setupLEDs() {
  FastLED.addLeds<WS2812B, 9>(leds, 1);
  FastLED.setBrightness(currentLEDBrightness);
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
  
  snprintf(macBuffer, 19, "%02X:%02X:%02X:%02X:%02X:%02X", 
    report->peer_addr.addr[5],
    report->peer_addr.addr[4],
    report->peer_addr.addr[3],
    report->peer_addr.addr[2],
    report->peer_addr.addr[1],
    report->peer_addr.addr[0]
  );
  
  log("\nMac: " + String(macBuffer));
  log("RSSI: " + String(report->rssi));

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

  
  if (String(macBuffer).indexOf("56:16:56:B1:9A:5A") != -1) {
    Bluefruit.Central.connect(report);
  } else {
    Bluefruit.Scanner.resume();
  }
  // For Softdevice v6: after received a report, scanner will be paused
  // We need to call Scanner resume() to continue scanning
  memset(buffer, 0, sizeof(macBuffer));
}

void periph_connect_callback(uint16_t conn_handle) {
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  char central_name[32] = { 0 };
  connection->getPeerName(central_name, sizeof(central_name));

  log("[Peripheral] Connected to  " + String(central_name));
}

void periph_disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  (void) reason;

  log("[Peripheral] Disconnected, reason = 0x" + String(HEX));
}

void central_connect_callback(uint16_t conn_handle) {
  // Get the reference to current connection
  BLEConnection* connection = Bluesfruit.Connection(conn_handle);

  char peer_name[32] = { 0 };
  connection->getPeerName(peer_name, sizeof(peer_name));

  log("[Central] Connected to " + String(peer_name));
}

void central_disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  (void) reason;
  
  log("[Central] Disconnected");
}

void loop() { }
