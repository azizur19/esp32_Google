
#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"
#include <esp_task_wdt.h>  // ESP32 watchdog API

#define WDT_TIMEOUT 60  // Timeout in seconds (1 minute)
#define LED         2


char    *WIFI_SSID[]     =    {"Celestial Wave", "Durbol_WIFI", "Titumir"};    
char    *WIFI_PASS[]     =    {"wave1234!",      "Bipul1000",   "pl5weeks"};
#define NO_OF_SSID       3
#define FIRST_ATTEMPT    0

//const char* scriptUrl = "https://script.google.com/macros/s/YOUR_SCRIPT_ID/exec";
const char* scriptUrl = "https://script.google.com/macros/s/AKfycbyYzT9mdTqFOnVeTqKmJ1XeuNuzjzpThmhrYpD-3vjwWo81IC_6Kelru70PwhMQQQKF/exec";//?sensor=232&value=2.3456";

// NTP server and timezone config
const char* ntpServer = "time.google.com";
const long  gmtOffset_sec = 6 * 3600;   // for GMT+6 (Bangladesh)
const int   daylightOffset_sec = 0;     // no daylight saving in BD
const char* ntpServers[] = {
  "pool.ntp.org",           // Global NTP pool
  "time.google.com",        // Google NTP
  "time.cloudflare.com",    // Cloudflare NTP
  "time.windows.com",       // Microsoft NTP
  "time.apple.com",         // Apple NTP
  "time.nist.gov",          // U.S. National Institute of Standards
  "ntp.ubuntu.com",         // Canonical/Ubuntu
  "asia.pool.ntp.org",      // Asia regional pool
  "europe.pool.ntp.org",    // Europe pool
  "north-america.pool.ntp.org", // North America pool
  "bd.pool.ntp.org",        // Bangladesh pool
  "in.pool.ntp.org",        // India pool
  "sg.pool.ntp.org",        // Singapore pool
  "time1.google.com",       // Google load-balanced
  "time2.google.com",       // Google load-balanced
  "time3.google.com",       // Google load-balanced
  "time4.google.com"        // Google load-balanced
};



void setup() {
  pinMode(LED, OUTPUT);
  Serial.begin(115200);
  delay(100);
  
  watch_dog_init();

  connect_to_wifi(FIRST_ATTEMPT % NO_OF_SSID);

  watch_dog_reset();


    // Init and get time from NTP
  config_time();

  sendData("Started_at", getLocalTime());
}

unsigned long long counter = 0;
unsigned long long last_time = millis();

void loop() {
  digitalWrite(LED, !digitalRead(LED));
  
  if (WiFi.status() != WL_CONNECTED)
    connect_to_wifi(FIRST_ATTEMPT % NO_OF_SSID);

  
  // Optional: add timed sending logic
  sendData(String(counter++), getLocalTime());
  
  while (millis()-last_time < 10000);
  last_time = millis();

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

void config_time(void){
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

String getLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    config_time();
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

void connect_to_wifi(byte attemt)
{
  Serial.println("\n\nConnecting to WiFi: " + String(WIFI_SSID[attemt])); 
  WiFi.begin(WIFI_SSID[attemt], WIFI_PASS[attemt]);
  
  unsigned long last_t__ = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(LED, !digitalRead(LED));
    Serial.print(".");
    delay(250);
    if(millis()-last_t__ > 3e3) 
      break;
  }
  if(WiFi.status() == WL_CONNECTED){
    Serial.println("\n✅ Connected to WiFi: " + String(WIFI_SSID[attemt]) + "\nIP-Address is: " + WiFi.localIP().toString().c_str());
    delay(100);
  }
  else connect_to_wifi((attemt+1) % NO_OF_SSID);
}
