
#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"
#include <esp_task_wdt.h>  // ESP32 watchdog API

#define WDT_TIMEOUT 60  // Timeout in seconds (1 minute)
#define LED         2

const char* ssid = "Celestial Wave";
const char* password = "wave1234!";
//const char* scriptUrl = "https://script.google.com/macros/s/YOUR_SCRIPT_ID/exec";
const char* scriptUrl = "https://script.google.com/macros/s/AKfycbyYzT9mdTqFOnVeTqKmJ1XeuNuzjzpThmhrYpD-3vjwWo81IC_6Kelru70PwhMQQQKF/exec";//?sensor=232&value=2.3456";

// NTP server and timezone config
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 6 * 3600;   // for GMT+6 (Bangladesh)
const int   daylightOffset_sec = 0;     // no daylight saving in BD


void setup() {
  pinMode(LED, OUTPUT);
  Serial.begin(115200);
  delay(100);
  
  watch_dog_init();

  connect_to_wifi();

  watch_dog_reset();


    // Init and get time from NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  sendData("Started_at", getLocalTime());
}

unsigned long long counter = 0;
unsigned long long last_time = millis();

void loop() {
  digitalWrite(LED, !digitalRead(LED));
  
  if (WiFi.status() != WL_CONNECTED)
    connect_to_wifi();
  
  // Optional: add timed sending logic
  sendData(String(counter++), getLocalTime());
  
  while (millis()-last_time < 10000);
  last_time = millis();

}




void connect_to_wifi(void){
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi: " + String(ssid));
}


void sendData(String sensor, String val) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(scriptUrl) + "?sensor=" + sensor + "&value=" + val;
    http.begin(url);
    
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Server response: " + response);
      
      watch_dog_reset();
    } 
    else {
      Serial.println("Error sending data. Code: " + String(httpResponseCode));
    }

    http.end();
  }
}

String getLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "0";
  }

//  Serial.println(&timeinfo, "Current time: %Y-%m-%d %H:%M:%S");
  char timeString[32];
  strftime(timeString, sizeof(timeString), "%Y.%m.%d_%H:%M:%S", &timeinfo);
  Serial.println(timeString);
    
  return String(timeString);  // if you want an Arduino String
}

void watch_dog_init(void){
    // Enable watchdog on current task (loopTask)
  esp_task_wdt_init(WDT_TIMEOUT, true);  // Enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);  // Add current task (loopTask) to watchdog
}

void watch_dog_reset(void){
  esp_task_wdt_reset();
}
