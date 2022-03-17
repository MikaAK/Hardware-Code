#include <bluefruit.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#define BLE_APPEARANCE_MOTORIZED_SHADES 1859
#define BLE_GAP_WHITELIST_ADDR_MAX_COUNT 1
#define MOTOR_PIN_A 6
#define MOTOR_PIN_B 5

const uint8_t BLE_SERVICE_PROXUS_MOTORIZED_SHADES[] = {
  0xFE, 0x3A, 0xAF, 0xFD, 0x68, 0xE1, 0x43, 0xCD, 
  0x90, 0x66, 0xB5, 0xDB, 0xC3, 0x8A, 0x97, 0xA6  
};

const uint8_t BLE_CHR_PROXUS_MOTORIZED_SHADES[] = {
  0x36, 0x2A, 0xF7, 0xD5, 0x94, 0x6C, 0x40, 0x56, 
  0x91, 0x77, 0xA6, 0x2D, 0xE6, 0xA4, 0xF4, 0xF4
};

// BLE Service
BLEDfu  bledfu;  // OTA DFU service
BLEDis  bledis;  // device information

BLEService bleShadesService = BLEService(BLE_SERVICE_PROXUS_MOTORIZED_SHADES);
BLECharacteristic bleShadesPositionCharacteristic(BLE_CHR_PROXUS_MOTORIZED_SHADES);

String UNIQUE_ID = String(getMcuUniqueID());
char deviceName[24];

// This goes 0 - 100 as a percentage for how much the shades are open
int currentShadePosition = 0;

class Motor {
  public: 
    void setup() {
      Serial.println("Setting up motor output...");
      pinMode(MOTOR_PIN_A, OUTPUT);
      pinMode(MOTOR_PIN_B, OUTPUT);
    }

    void stop() {
      digitalWrite(MOTOR_PIN_A, LOW);
      digitalWrite(MOTOR_PIN_B, LOW);
    }

    void left() {
      Serial.println("Moving motor right..."); 
      digitalWrite(MOTOR_PIN_A, HIGH);
      digitalWrite(MOTOR_PIN_B, LOW);
    }

    void right() {
      Serial.println("Moving motor left..."); 
      digitalWrite(MOTOR_PIN_B, HIGH);
      digitalWrite(MOTOR_PIN_A, LOW);
    }
};

Motor shadeMotor = Motor();

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

  bleShadesService.begin();
  bleShadesPositionCharacteristic.setProperties(CHR_PROPS_WRITE | CHR_PROPS_READ);
  bleShadesPositionCharacteristic.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
  bleShadesPositionCharacteristic.setFixedLen(2);
  bleShadesPositionCharacteristic.setWriteCallback(write_callback);
  bleShadesPositionCharacteristic.begin();
  
  bledis.setManufacturer("Mika Software Inc.");
  bledis.setModel("Shades nRF52");
  bledis.begin();
}

void startAdv(void) {
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_MOTORIZED_SHADES);

  Bluefruit.Advertising.addService(bleShadesService);
  
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

void moveToCurrentShadePosition() {
  shadeMotor.right();
  delay(10000);
  shadeMotor.left();
  delay(10000);
}

void setShadePosition(int pos) {
  currentShadePosition = pos;
  Serial.println("Current Shade Position: " + String(currentShadePosition));
}

void write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len)
{
  (void) conn_hdl;
  (void) chr;
  (void) len; // len should be 1

  setShadePosition(strtol((const char*)data, NULL, 10));
  moveToCurrentShadePosition();
}

void setup() {
//  #if CFG_DEBUG
    // Blocking wait for connection when debug mode is enabled via IDE
    setupSerial();
//  #endif  

  String("Window Shades - " + UNIQUE_ID).toCharArray(deviceName, 24);

  Serial.println("Starting Up " + String(deviceName) + "...");
  shadeMotor.setup();
  shadeMotor.left(); 
  
  Serial.println("Setting up bluetooth...");
  setupBluetooth();

  Serial.println("Starting to advertise...");
  startAdv();

  uint16_t shadePos = bleShadesPositionCharacteristic.read16();
  uint16_t* shadePosPtr = &shadePos;

  setShadePosition(strtol((const char*)shadePosPtr, NULL, 10));
}

void loop() {
  delay(100000);
}
