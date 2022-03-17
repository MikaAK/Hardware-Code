#include <WiFi.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <driver/rtc_io.h>
#include <rom/rtc.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include "secrets.h"

#define Threshold 10
#define MS_TO_uS_FACTOR 1000ULL
#define DRIVER_PIN 32
#define LEFT_PIN 25
#define RIGHT_PIN 12
#define RESET_PIN GPIO_NUM_13
#define MS_FOR_LOCK_TO_COMPLETE 9000
#define MS_SLEEP_DEFAULT 10000

const char* ssid       = "Mika";
const char* password   = SECRET_WIFI_PASSWORD;

String lock_state;

// Variables to validate response
long content_length = 0;
bool is_valid_content_type = false;
bool ota_updated = false;

const char* mdnsHost = "proxus";
String host = "";
String bin = "/door-sketch.ino.bin"; // bin file name with a slash in front.
String door_lock_url = "";
String FALLBACK_CONFIG = "0100"; // Only touchpad enabled

// Request Functions

String uint64ToString(uint64_t input) {
  String result = "";
  uint8_t base = 10;

  do {
    char c = input % base;
    input /= base;

    if (c < 10)
      c +='0';
    else
      c += 'A' - 10;
    result = c + result;
  } while (input);
  return result;
}

String CHIP_ID = uint64ToString(ESP.getEfuseMac());

int* check_unlock_status_and_get_config() {
  String payload;
  HTTPClient http;
  Serial.println("Fetching Config For: " + String(CHIP_ID));
  http.begin(door_lock_url + "?chip_id=" + String(CHIP_ID));

  int httpCode = http.GET();

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      payload = http.getString();
      Serial.println("[HTTP] GET... success " + payload);
    } else {
      Serial.println("[HTTP] GET... failed, error: \n" + http.getString() + "\n\n");
      payload = FALLBACK_CONFIG;
    }
  } else {
    Serial.println("[HTTP] GET... failed, error: \n" + String(http.errorToString(httpCode).c_str()) + "\n\n");
    payload = FALLBACK_CONFIG;
  }

  http.end();

  // {ota_check?, touchpad_check?, lock_door?, unlock_door?}
  static int res_config[4] = {(payload[0] - '0'), (payload[1] - '0'), (payload[2] - '0'), (payload[3] - '0')};

  Serial.println("OTA Check: " + String(payload[0]));
  Serial.println("Touchpad Check: " + String(payload[1]));
  Serial.println("Lock Door: " + String(payload[2]));
  Serial.println("Unlock Door: " + String(payload[3]));
  Serial.println("");

  return res_config;
}

int push_state_and_get_sleep_interval(String lock_state, bool ota_updated) {
  int payload;
  String ota_updated_string;
  HTTPClient http;

  if (ota_updated) { ota_updated_string = "true"; } else { ota_updated_string = "false"; }

  String json_payload = "{\"chip_id\": \"" + CHIP_ID + "\"," +
                        " \"status\": \"" + lock_state + "\"," +
                        " \"did_ota_run\": " + ota_updated_string + "}";


  Serial.println("Pushing state to server: " + lock_state + "\n" + json_payload);
  http.begin(door_lock_url);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.PUT(json_payload);

  if (httpCode > 0) {
    Serial.println("[HTTP] PUT... complete: " + String(httpCode));

    if (httpCode == HTTP_CODE_OK) {
      Serial.print("[HTTP] PUT... success ");
      payload = http.getString().toInt();
      Serial.println(String(payload));
    } else {
      payload = MS_SLEEP_DEFAULT;
      Serial.println("[HTTP] PUT... failed, error: \n" + String(http.getString()) + "\n\n");
    }
  } else {
    payload = MS_SLEEP_DEFAULT;
    Serial.println("[HTTP] PUT... failed, error: \n" + String(http.errorToString(httpCode).c_str()) + "\n\n");
  }

  http.end();

  return payload;
}

// Utility to extract header value from headers
String get_header_value(String header, String header_name) {
  return header.substring(strlen(header_name.c_str()));
}

// OTA Logic
void check_and_do_ota() {
  WiFiClient client;
  Serial.println("Connecting to: " + String(host));
  // Connect to S3
  if (client.connect(host.c_str(), 80)) {
    // Connection Succeed.
    // Fecthing the bin
    Serial.println("Fetching Bin: " + String(bin));

    // Get the contents of the bin file
    client.print(String("GET ") + bin + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Cache-Control: no-cache\r\n" +
                 "Connection: close\r\n\r\n");

    // Check what is being sent
    Serial.print(String("GET ") + bin + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Cache-Control: no-cache\r\n" +
                 "Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println("Client Timeout !");
        client.stop();
        return;
      }
    }

    while (client.available()) {
      // read line till /n
      String line = client.readStringUntil('\n');
      // remove space, to check if the line is end of headers
      line.trim();

      // if the the line is empty,
      // this is end of headers
      // break the while and feed the
      // remaining `client` to the
      // Update.writeStream();
      if (!line.length()) {
        //headers ended
        break; // and get the OTA started
      }

      // Check if the HTTP Response is 200
      // else break and Exit Update
      if (line.startsWith("HTTP/1.1")) {
        if (line.indexOf("200") < 0) {
          Serial.println("Got a non 200 status code from server. Exiting OTA Update.");
          break;
        }
      }

      // extract headers here
      // Start with content length
      if (line.startsWith("content-length: ")) {
        content_length = atol((get_header_value(line, "content-length: ")).c_str());
        Serial.println("Got " + String(content_length) + " bytes from server");
      }

      // Next, the content type
      if (line.startsWith("content-type: ")) {
        String content_type = get_header_value(line, "content-type: ");
        Serial.println("Got " + content_type + " payload.");
        if (content_type == "application/octet-stream") {
          is_valid_content_type = true;
        }
      }
    }
  } else {
    // Connect to S3 failed
    // May be try?
    // Probably a choppy network?
    Serial.println("Connection to " + String(host) + " failed. Please check your setup");
    // retry??
    // execOTA();
  }

  // Check what is the content_length and if content type is `application/octet-stream`
  Serial.println("content_length : " + String(content_length) + ", is_valid_content_type : " + String(is_valid_content_type));

  // check content_length and content type
  if (content_length && is_valid_content_type) {
    // Check if there is enough to OTA Update
    bool can_begin = Update.begin(content_length);

    // If yes, begin
    if (can_begin) {
      Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
      // No activity would appear on the Serial monitor
      // So be patient. This may take 2 - 5mins to complete
      size_t written = Update.writeStream(client);

      if (written == content_length) {
        Serial.println("Written : " + String(written) + " successfully");
      } else {
        Serial.println("Written only : " + String(written) + "/" + String(content_length) + ". Retry?" );
        // retry??
        // check_and_do_ota();
      }

      if (Update.end()) {
        Serial.println("OTA done!");
        if (Update.isFinished()) {
          Serial.println("Update successfully completed.");

          ota_updated = true;
        } else {
          Serial.println("Update not finished? Something went wrong!");
        }
      } else {
        Serial.println("Error Occurred. Error #: " + String(Update.getError()));
      }
    } else {
      // not enough space to begin OTA
      // Understand the partitions and
      // space availability
      Serial.println("Not enough space to begin OTA");
      client.flush();
    }
  } else {
    Serial.println("There was no content in the response");
    ota_updated = true;
    client.flush();
  }
}

void touch_callback() {
}

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0  : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1  : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER  : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP  : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.println("Wakeup was not caused by deep sleep"); break;
  }
}


void setup_pins() {
  pinMode(DRIVER_PIN, OUTPUT);
  pinMode(LEFT_PIN, OUTPUT);
  pinMode(RIGHT_PIN, OUTPUT);
//  esp_sleep_enable_ext0_wakeup(RESET_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
}

void connect_to_wifi() {
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup_touchpad_wakeup() {
  touchAttachInterrupt(T3, touch_callback, Threshold);
//  touchAttachInterrupt(T2, callback2, Threshold);

  esp_sleep_enable_touchpad_wakeup();
}


void setup_timer_wakeup(int ms_to_sleep) {
  esp_sleep_enable_timer_wakeup(ms_to_sleep * MS_TO_uS_FACTOR);
}

void change_lock_state(bool is_open) {
  digitalWrite(DRIVER_PIN, HIGH);

  if (is_open) {
    Serial.println("Unlocking Door...");

    digitalWrite(LEFT_PIN, LOW);
    digitalWrite(RIGHT_PIN, HIGH);
  } else {
    Serial.println("Locking Door...");

    digitalWrite(LEFT_PIN, HIGH);
    digitalWrite(RIGHT_PIN, LOW);
  }

  delay(MS_FOR_LOCK_TO_COMPLETE);

  Serial.println("Door State Changed");
  digitalWrite(DRIVER_PIN, LOW);
  digitalWrite(RIGHT_PIN, LOW);
  digitalWrite(LEFT_PIN, LOW);
}

void start_mdns_service() {
  Serial.println("Starting MDNS....");
  //initialize mDNS service
  esp_err_t err = mdns_init();

  if (err) {
    Serial.printf("MDNS Init failed: %d\n", err);
    return;
  }

  //set hostname
  mdns_hostname_set("door-lock");
  //set default instance
  mdns_instance_name_set("Door Lock");
}

String resolve_mdns_host(const char * host_name) {
  Serial.printf("Query A: %s.local\n", host_name);

  struct ip4_addr addr;
  addr.addr = 0;

  esp_err_t err = mdns_query_a(host_name, 20000,  &addr);
  if(err) {
    if (err == ESP_ERR_NOT_FOUND) {
      Serial.printf("Host was not found!\b");
    } else {
      Serial.printf("Query Failed\n");
    }
    return "";
  } else {
    return ip_to_str(IP2STR(&addr));
  }
}

String ip_to_str(int a, int b, int c, int d) {
  return String(String(a) + "." + String(b) + "." + String(c) + "." + String(d));
}

void maybe_set_host_from_mdns() {
  if (mdnsHost != "") {
    host = resolve_mdns_host(mdnsHost);
    door_lock_url = "http://" + host + "/api/door-lock";

    Serial.println("Resolved MDNS for " + String(mdnsHost) + "\nHost:" + host);
  }
}

bool is_internal_unlock() {
  return rtc_get_reset_reason(1) == 1 || rtc_get_reset_reason(0) == 1;
}


RTC_DATA_ATTR bool was_internal_unlock = false;

void setup() {
  btStop();
  setCpuFrequencyMhz(80);
  adc_power_off();
  esp_bt_controller_disable();

  Serial.begin(115200);
  delay(10);

  Serial.println("Current CHIP_ID: " + String(CHIP_ID));

  print_wakeup_reason();
  setup_pins();

  if (was_internal_unlock) {
    was_internal_unlock = false;
    change_lock_state(false);
    ESP.restart();
  } else if (is_internal_unlock()) {
    Serial.println("Reset button triggered");
    change_lock_state(true);
    was_internal_unlock = true;

    Serial.println("Sleeping for 30 seconds...");
    setup_timer_wakeup(30000);
    esp_deep_sleep_start();
  } else {
    connect_to_wifi();

    start_mdns_service();
    maybe_set_host_from_mdns();

    if (host == "") {
      Serial.println("Host is empty, aborting....");
      ESP.restart();
      return;
    }

    int* config_list = check_unlock_status_and_get_config();

    bool should_ota_check    = config_list[0];
    bool should_use_touchpad = config_list[1];
    bool should_lock_door    = config_list[2];
    bool should_unlock_door  = config_list[3];

    if (should_unlock_door) {
      change_lock_state(true);
      lock_state = String("unlocked");
    } else if (should_lock_door) {
      change_lock_state(false);
      lock_state = String("locked");
    }

    if (should_ota_check) {
      check_and_do_ota();
    }

    if (should_use_touchpad) {
      setup_touchpad_wakeup();
    }

    int sleep_interval = push_state_and_get_sleep_interval(lock_state, ota_updated);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();


    setup_timer_wakeup(sleep_interval);



    Serial.print("Going to sleep now for " + String(sleep_interval / 1000) + " sec");

    if (should_use_touchpad) {
      Serial.print(" or till touch");
    }

    Serial.println("");
    Serial.flush();

    esp_deep_sleep_start();
  }
}

void loop() {
}
