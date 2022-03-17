#include <bluefruit.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#define BLE_APPEARANCE_PROXIMITY_DEVICE 1859

const uint8_t BLE_SERVICE_PROXIMITY_NECKLACE[] = {
  0xFE, 0x3A, 0xAF, 0xFD, 0x68, 0xE1, 0x43, 0xCD,
  0x90, 0x66, 0xB5, 0xDB, 0xC3, 0x8A, 0x97, 0xA6
};

const uint8_t BLE_CHR_PROXIMITY_NECKLACE[] = {
  0x36, 0x2A, 0xF7, 0xD5, 0x94, 0x6C, 0x40, 0x56,
  0x91, 0x77, 0xA6, 0x2D, 0xE6, 0xA4, 0xF4, 0xF4
};

// BLE Service
BLEDfu  bledfu;  // OTA DFU service
BLEDis  bledis;  // device information

BLEService bleNecklackService = BLEService(BLE_SERVICE_PROXIMITY_NECKLACE);
BLECharacteristic bleNecklaceProximityCharacteristic(BLE_CHR_PROXIMITY_NECKLACE);

String UNIQUE_ID = String(getMcuUniqueID());
char deviceName[24];

void setupSerial() {
  Serial.begin(115200);

  while (!Serial) delay(10);

  randomSeed(analogRead(0));
}

void setupBluetooth() {
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);

  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName(deviceName);

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  bledfu.begin();

  bleNecklackService.begin();
  bleNecklaceProximityCharacteristic.setProperties(CHR_PROPS_WRITE | CHR_PROPS_READ);
  bleNecklaceProximityCharacteristic.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
  bleNecklaceProximityCharacteristic.setFixedLen(2);
  bleNecklaceProximityCharacteristic.setWriteCallback(write_callback);
  bleNecklaceProximityCharacteristic.begin();

  bledis.setManufacturer("Mika Software Inc.");
  bledis.setModel("Proximity Necklace nRF52");
  bledis.begin();
}

void startAdv(void) {
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_PROXIMITY_DEVICE);

  Bluefruit.Advertising.addService(bleNecklackService);

  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 10240);    // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);      // number of seconds in fast mode
  Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising after n seconds
}


void connect_callback(uint16_t conn_handle) {
  // Get the reference to current connection
  BLEConnection* connection = Bluefruit.Connection(conn_handle);

  char central_name[32] = { 0 };
  connection->getPeerName(central_name, sizeof(central_name));

  Serial.print("Connected to ");
  Serial.println(central_name);
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  (void) reason;

  Serial.println();
  Serial.print("Disconnected, reason = 0x");
  Serial.println(reason, HEX);
}

void write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len)
{
  (void) conn_hdl;
  (void) chr;
  (void) len; // len should be 1

  Serial.println("WRITE CALLBACK HIT");
}

void setup() {
  setupSerial();


  String("PLURals - " + UNIQUE_ID).toCharArray(deviceName, 24);

  Serial.println("Starting Up " + String(deviceName) + "...");

  Serial.println("Setting up bluetooth...");
  setupBluetooth();

  Serial.println("Starting to advertise...");
  startAdv();
}

void loop() {
  // put your main code here, to run repeatedly:

}
