#include <WiFi.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <driver/adc.h>

#include <Stepper.h>
#include <EEPROM.h>

#include <"secrets.h">

#define MOTOR_1_A 33
#define MOTOR_1_B 32

#define MOTOR_2_A 19
#define MOTOR_2_B 21

#define EEPROM_SIZE 1

const char* WIFI_SSID     = "Mika";
const char* WIFI_PASSWORD = SECRET_WIFI_PASSWORD;
const int stepsPerRevolution = 1200;

Stepper stepper(stepsPerRevolution, MOTOR_1_A, MOTOR_1_B, MOTOR_2_A, MOTOR_2_B);

// This goes 0 - 100 as a percentage for how much the shades are open
int currentShadePosition = 0;

void setup() {
//  btStop();
//  setCpuFrequencyMhz(80);
//  adc_power_off();
//  esp_bt_controller_disable();

  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(10);

  EEPROM.begin(EEPROM_SIZE);

  currentShadePosition = EEPROM.read(0) || 0;

  Serial.println("Current Shade Position: " + String(currentShadePosition));
}

void loop() {
  setShadePosition(100);
  delay(10000);

  setShadePosition(50);
  delay(10000);x

  setShadePosition(0);
  delay(10000);

  EEPROM.update(0, currentShadePosition);
  EEPROM.end();
}

// SHADE CONTROL START
void setShadePosition(int newShadePosition) {
  int newStepPos = floor(stepsPerRevolution * (newShadePosition / 100));
  int oldStepPos = floor(stepsPerRevolution * (currentShadePosition / 100));
  int stepsTillPos = newStepPos - oldStepPos;

  Serial.println("---- MOVEMENT START----");
  Serial.println("Moving to " + String(newShadePosition) + "% open");
  Serial.println("Old Step Pos: " + String(oldStepPos));
  Serial.println("New Step Pos: " + String(newStepPos));
  Serial.println("Steps to get there: " + String(stepsTillPos));

  step(stepsTillPos);

  currentShadePosition = newShadePosition;
  Serial.println("---- MOVEMENT END ----\n\n");
}

void step(int n) {
  if (n >= 0) {
    Serial.println("Stepping Forward");

    for (int i = 0; i < n; i++) {
      stepper.step(1);
      delay(2);
    }
  } else {
    Serial.println("Stepping Backwards");

    for (int i = 0; i > n; i--) {
      stepper.step(-1);
      delay(2);
    }
  }
}
// SHADE CONTROL END

// WIFI CONTROL START
void connectToWifi() {
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void startMdnsService() {
  Serial.println("Starting MDNS....");

  // Initialize mDNS service
  esp_err_t err = mdns_init();

  if (err) {
    Serial.printf("MDNS Init failed: %d\n", err);
    return;
  }

  // Set hostname
  mdns_hostname_set("window-shade");
  //set default instance
  mdns_instance_name_set("Window Shade");
}
// WIFI CONTROL END
