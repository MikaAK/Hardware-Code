#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <FastLED.h>

using namespace Adafruit_LittleFS_Namespace;

#define FILENAME      "/necklace-color.txt"
#define DEFAULT_COLOR "FF00FF"

File file(InternalFS);

class ColorStorage { 
  public:
    void begin();
    void setColour(String color);
    String getColour();
};

void ColorStorage::begin() {
  InternalFS.begin();

  file.open(FILENAME, FILE_O_READ);
}

void ColorStorage::setColour(String color) {
  String str_color = String(color);
  
  if (file) {  
    Serial.println("Found file!");

    file.write(str_color.c_str(), str_color.length());
    file.close();
    Serial.println("File written");
  } else {
    Serial.print("Open " FILENAME " file to write ... ");

    if (file.open(FILENAME, FILE_O_WRITE)) {
      Serial.println("File opened");
      file.write(str_color.c_str(), str_color.length());
      file.close();
      Serial.println("File written");
    } else {
      Serial.println("Failed to write file!");
    }
  }
}

String ColorStorage::getColour() {
  if (!file) {  
    Serial.println("No file existing!");
    
    return DEFAULT_COLOR;
  }
  
  Serial.println("Found file!");
  
  uint32_t readlen;
  char buffer[64] = { 0 };
  readlen = file.read(buffer, sizeof(buffer));

  buffer[readlen] = 0;
  Serial.println("Found Color: " + String(buffer));
  file.close();

  return String(buffer);
}
