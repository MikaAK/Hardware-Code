#include <bluefruit.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#define BLE_APPEARANCE_PROXIMITY_DEVICE 1859
#define SOFTWARE_VERSION "0.1.0"
#define MANUFACTURER_NAME "Mika Software Inc."
#define MODEL_NAME "Proximity Necklace nRF52"

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

//BLEService bleNecklackService = BLEService(BLE_SERVICE_PROXIMITY_NECKLACE);

String UNIQUE_ID = String(getMcuUniqueID());
char deviceName[24];

void log(String& message) {
  Serial.println(message);
  
  if (bleuart.available()) {
    bleuart.write(message.c_str()); 
  }
}

void log(const char* message) {
  Serial.println(message);
  
  if (bleuart.available()) {
    bleuart.write(message); 
  }
}

void setupSerial() {
  Serial.begin(115200);

  while (!Serial) delay(10);
}

void setupBluetooth() {
  Bluefruit.autoConnLed(true);
    
  log("Config prph bandwidth...");
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);


  log("Begin bluefruit...");
//  Bluefruit.begin(1, 1);
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName(deviceName);

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
  
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

  log("Starting UART...");
  bleuart.begin();
}

void startAdv(void) {
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();

//  Bluefruit.Advertising.addService(bleNecklackService);
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.addService(bledis);
  Bluefruit.Advertising.addService(blebas);
//  Bluefruit.Advertising.addService(bledfu);

  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
}


void connect_callback(uint16_t conn_handle) {
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  char central_name[32] = { 0 };
  connection->getPeerName(central_name, sizeof(central_name));

  Serial.print("Connected to ");
  log(central_name);
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  (void) reason;

  log("Disconnected, reason = 0x" + String(HEX));
}

void write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len)
{
  (void) conn_hdl;
  (void) chr;
  (void) len; // len should be 1

  log("WRITE CALLBACK HIT");
}

void setup() {
  setupSerial();

  String("LED Necklace - " + UNIQUE_ID).toCharArray(deviceName, 24);

  log("Starting Up " + String(deviceName) + "...");

  log("\n---- Bluetooth Setup Start ----");
  setupBluetooth();
  log("---- Bluetooth Setup End ----\n");

  startAdv();
  log("---- Bluetooth Advertising Started ----");
}

void loop() {
  writeSerialToBleUart();
}

void writeSerialToBleUart() {
  if (Serial.available()) {
    delay(2);

    uint8_t buf[64];
    int count = Serial.readBytes(buf, sizeof(buf));
    bleuart.write( buf, count );
  }
}
