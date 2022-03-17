#include <SoftwareSerial.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include <Adafruit_NeoPixel.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>

#include <FuGPS.h>
#include <math.h>
#include <float.h>

#define SW_RXD 6
#define SW_TXD 5
#define LED_PIN 11
#define LED_COUNT 24
#define BNO055_SAMPLERATE_DELAY_MS 100

#define CALIBRATION_FILENAME "/calibration.txt"

using namespace Adafruit_LittleFS_Namespace;

SoftwareSerial gpsSerial(SW_RXD, SW_TXD);
Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_BNO055 bno = Adafruit_BNO055(55);

File file(InternalFS);

FuGPS gps(gpsSerial);
bool gpsAlive = false;

int degreeFacing = 180, hueToGreen = 65536 / 3;
int huesPerPixel = hueToGreen / LED_COUNT;

void setupRing() {
  Serial.println("Setting up NeoPixel Ring...");
  
  ring.begin();
  ring.show();
}

void setupGPS() {
  Serial.println("Setting up GPS...");
  gpsSerial.begin(9600);
  gps.sendCommand(FUGPS_PMTK_API_SET_NMEA_OUTPUT_RMCGGA);
}

void setupOrientationSensor() {
  Serial.println("Setting up orientation sensor...");

  if (!bno.begin(bno.OPERATION_MODE_COMPASS)) {
    Serial.print("Ooops, no BNO055 detected ... Check your wiring or I2C ADDR!");
    
    return;
  }

  setupCalibrationFile();
  calibrateBNO();
  
  Serial.println("Finished setting up orientation sensor");
}

void setupCalibrationFile() {
  InternalFS.begin();
  file.open(CALIBRATION_FILENAME, FILE_O_READ);
}

void displayCalStatus(void) {
  /* Get the four calibration values (0..3) */
  /* Any sensor data reporting 0 should be ignored, */
  /* 3 means 'fully calibrated" */
  uint8_t system, gyro, accel, mag;
  system = gyro = accel = mag = 0;
  bno.getCalibration(&system, &gyro, &accel, &mag);
 
  /* The data should be ignored until the system calibration is > 0 */
  Serial.print("\t");
  
  if (!system) {
    Serial.print("! ");
  }
 
  /* Display the individual values */
  Serial.print("Sysemcl:");
  Serial.print(system, DEC);
  Serial.print(" Gyro:");
  Serial.print(gyro, DEC);
  Serial.print(" Accel:");
  Serial.print(accel, DEC);
  Serial.print(" Mag:");
  Serial.println(mag, DEC);
}

void calibrateBNO() {
  bool foundCalib = false;

  adafruit_bno055_offsets_t *calibrationData;
  sensor_t sensor;

  /*
  *  Look for the sensor's unique ID at the beginning oF EEPROM.
  *  This isn't foolproof, but it's better than nothing.
  */
  bno.getSensor(&sensor);

  readCalibrationFile(calibrationData);

  if (calibrationData) {
    Serial.println("Found Calibration data for sensor");
    Serial.println("Restoring Calibration data to the BNO055...");
    bno.setSensorOffsets(*calibrationData);
    Serial.println("Calibration data loaded into BNO055");
    foundCalib = true;
  }
    
//  if (bnoID != sensor.sensor_id) {
//    Serial.println("\nNo Calibration Data for this sensor exists in EEPROM");
//    delay(500);
//  } else {
//    Serial.println("\nFound Calibration for this sensor in EEPROM.");
//    eeAddress += sizeof(long);
//    EEPROM.get(eeAddress, calibrationData);
//
//    Serial.println("\n\nRestoring Calibration data to the BNO055...");
//    bno.setSensorOffsets(calibrationData);
//
//    Serial.println("\n\nCalibration data loaded into BNO055");
//    foundCalib = true;
//  }

  delay(1000);

 /* Crystal must be configured AFTER loading calibration data into BNO055. */
  bno.setExtCrystalUse(true);

  sensors_event_t event;
  bno.getEvent(&event);
  /* always recal the mag as It goes out of calibration very often */
  if (foundCalib) {
    Serial.println("Move sensor slightly to calibrate magnetometers");
      
    while (!bno.isFullyCalibrated()) {
      bno.getEvent(&event);
      delay(BNO055_SAMPLERATE_DELAY_MS);
    }
  } else {
    Serial.println("Please Calibrate Sensor: ");
    while (!bno.isFullyCalibrated()) {
      bno.getEvent(&event);

      Serial.print("X: ");
      Serial.print(event.orientation.x, 4);
      Serial.print("\tY: ");
      Serial.print(event.orientation.y, 4);
      Serial.print("\tZ: ");
      Serial.print(event.orientation.z, 4);

      /* Optional: Display calibration status */
      displayCalStatus();

      /* New line for the next sample */
      Serial.println("");

      /* Wait the specified delay before requesting new data */
      delay(BNO055_SAMPLERATE_DELAY_MS);
    }
  }

  Serial.println("\nFully calibrated!");
  Serial.println("--------------------------------");
  Serial.println("Calibration Results: ");
  
  adafruit_bno055_offsets_t newCalib;
  bno.getSensorOffsets(newCalib);

  Serial.println("\n\nStoring calibration data to FS...");

//  eeAddress = 0;
  bno.getSensor(&sensor);
//  bnoID = sensor.sensor_id;

  writeCalibrationFile(newCalib);

//  eeAddress += sizeof(long);
//  EEPROM.put(eeAddress, newCalib);
  Serial.println("Data stored to FS.");

  Serial.println("\n--------------------------------\n");
}

void readCalibrationFile(adafruit_bno055_offsets_t *buffer) {  
  file.open(CALIBRATION_FILENAME, FILE_O_READ);

  if (file) {
    uint32_t readlen;
    char buffer[64] = { 0 };
    readlen = file.read(buffer, sizeof(buffer));
  
    buffer[readlen] = 0;
    Serial.println(buffer);
    file.close();
  } else {
    Serial.println("Calibration file not found");
  }
}

void writeCalibrationFile(adafruit_bno055_offsets_t calib) {
  if (file.open(CALIBRATION_FILENAME, FILE_O_WRITE)) {
    Serial.println("Opened calibration for writing....");
    
    String calibString = String(calib.accel_offset_x) + "," + String(calib.accel_offset_y) + "," + String(calib.accel_offset_z) + "," +
                         String(calib.mag_offset_x) + "," + String(calib.mag_offset_y) + "," + String(calib.mag_offset_z) + "," + 
                         String(calib.gyro_offset_x) + "," + String(calib.gyro_offset_y) + "," + String(calib.gyro_offset_z) + "," +
                         String(calib.accel_radius) + "," + String(calib.mag_radius);

    char calibrationChars[calibString.length()];

    calibString.toCharArray(calibrationChars, calibString.length());
                          
    file.write(calibrationChars, strlen(calibrationChars));
    file.close();
    Serial.println("Saved calibration successfuly....");
  } else {
    Serial.println("Failed to write file!");
  }
}

void setup() {
  Serial.begin(115200);
  
  while (!Serial) delay(10);  // for nrf52840 with native usb
  Serial.println("TEST");
  Serial.println("TEST");
  setupOrientationSensor();
//  setupGPS();
//  setupRing();
}
 
void loop() {
//  for (int i = 0; i <= 360; i++) {
//    ring.clear();
//    setLedDirection(i);
//    delay(10);
//  }
//  
//  if (gps.read()) {
//   checkGpsLocation();
//  }
//  
//  checkGpsAlive();
//  logPosition();
  delay(BNO055_SAMPLERATE_DELAY_MS);
}

void logPosition() {
    /* Get a new sensor event */ 
  sensors_event_t event; 
  bno.getEvent(&event);
  
  /* Display the floating point data */
  Serial.print("X: ");
  Serial.print(event.orientation.x, 4);
  Serial.print("\tY: ");
  Serial.print(event.orientation.y, 4);
  Serial.print("\tZ: ");
  Serial.print(event.orientation.z, 4);
  Serial.println("");
  Serial.println("Current Temperature: " + String(bno.getTemp()));
}

void setLedDirection(int degrees360) {
  if (degrees360 == 0) { degrees360 = 1; }
  int litLed = ratioValue(degrees360, 360, LED_COUNT) - 2;

  int hue;

  if (degrees360 <= 180) {
    hue = ratioValue(degrees360, 180, hueToGreen);
    
    setRingPixelColor(litLed, hue, -2);
    setRingPixelColor(litLed + 1, hue, -1);
    setRingPixelColor(litLed + 2, hue, 0);
    setRingPixelColor(litLed + 3, hue, 1);
    setRingPixelColor(litLed + 4, hue, 2);
    
  } else {
    hue = ratioValue(180 + (180 - degrees360), 180, hueToGreen);
    
    setRingPixelColor(litLed + 4, hue, -2);
    setRingPixelColor(litLed + 3, hue, -1);
    setRingPixelColor(litLed + 2, hue, 0);
    setRingPixelColor(litLed + 1, hue, 1);
    setRingPixelColor(litLed, hue, 2);
  }

  ring.setBrightness(4);
  ring.show();
}

void setRingPixelColor(int led, int hue, int hueModifier) {
  ring.setPixelColor(correctLedPin(led), createColor(hue, hueModifier));
}

int correctLedPin(int led) {
  if (led > (LED_COUNT - 1)) {
    return led - LED_COUNT;
  } else if (led < 0) {
    return LED_COUNT + led;
  } {
    return led;
  }
}

uint32_t createColor(int hue, int ledI) {
  return ring.gamma32(ring.ColorHSV(hue + (huesPerPixel * ledI), 255, 255));
}

float ratioValue(float currentValue, float maxValue, float wantedMaxValue) {
  return ((currentValue * 1.0) / maxValue) * wantedMaxValue;
}

// 0 <= stepNumber <= lastStepNumber
int interpolateValue(int startValue, int endValue, int stepNumber, int lastStepNumber) {
  return (endValue - startValue) * stepNumber / lastStepNumber + startValue;
}

void checkGpsAlive() {
  // Default is 10 seconds
  if (!gps.isAlive()) {
    if (gpsAlive) {
        gpsAlive = false;
        Serial.println("GPS module not responding with valid data.");
        Serial.println("Check wiring or restart.");
    }
  }
}

void checkGpsLocation() {
  // We don't know, which message was came first (GGA or RMC).
  // Thats why some fields may be empty.
  gpsAlive = true;

  Serial.print("Quality: ");
  Serial.println(gps.Quality);

  Serial.print("Satellites: ");
  Serial.println(gps.Satellites);

  if (gps.hasFix()) {
    // Data from GGA message
    Serial.print("Accuracy (HDOP): ");
    Serial.println(gps.Accuracy);

    Serial.print("Altitude (above sea level): ");
    Serial.println(gps.Altitude);

    // Data from GGA or RMC
    Serial.print("Location (decimal degrees): ");
    Serial.println("https://www.google.com/maps/search/?api=1&query=" + String(gps.Latitude, 6) + "," + String(gps.Longitude, 6));
  }
}
