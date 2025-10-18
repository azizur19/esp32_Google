#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"
#include <esp_task_wdt.h>

#define WDT_TIMEOUT 60
#define LED 2
#define LOG_BUSY_PIN 4   // GPIO pin for sync between cores
#define ADC_PIN 36       // ADC1_CH0
#define MAX_LOGS 6000     // max number of stored readings

// WiFi credentials
char* WIFI_SSID[] = {"Celestial Wave", "Durbol_WIFI", "Titumir"};
char* WIFI_PASS[] = {"wave1234!", "Bipul1000", "pl5weeks"};
#define NO_OF_SSID 3

const char* scriptUrl = "https://script.google.com/macros/s/AKfycbyYzT9mdTqFOnVeTqKmJ1XeuNuzjzpThmhrYpD-3vjwWo81IC_6Kelru70PwhMQQQKF/exec";

const char* ntpServer = "time.google.com";
const long gmtOffset_sec = 6 * 3600;
const int daylightOffset_sec = 0;

// ---------- STRUCT & GLOBAL DATA ----------
struct LogData {
  unsigned long uptimeSec;
  int adcValue;
};

LogData logs[MAX_LOGS];
volatile int logHead = 0;
volatile int logTail = 0;

// ---------- FORWARD DECLARATIONS ----------
void connect_to_wifi(byte);
void config_time();
String getLocalTime();
void sendData(String, String);
int get_reading();

// ---------- TASK HANDLES ----------
TaskHandle_t TaskLogHandle;
TaskHandle_t TaskWiFiHandle;

// ---------- WATCHDOG ----------
void watch_dog_init() {
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);
}

void watch_dog_reset() {
  esp_task_wdt_reset();
}

// ---------- TASK 1: SENSOR LOGGING (CORE 0) ----------
// void TaskLog(void* pvParameters) {
//   unsigned long lastLogTime = millis();

//   while (true) {
//     if (millis() - lastLogTime >= 1000) {  // Every 1 second
//       digitalWrite(LOG_BUSY_PIN, HIGH);    // Signal logging start

//       LogData sample;
//       sample.uptimeSec = millis() / 1000;
//       sample.adcValue = get_reading();

//       // Save to circular buffer
//       int nextHead = (logHead + 1) % MAX_LOGS;
//       if (nextHead != logTail) {  // prevent overwrite
//         logs[logHead] = sample;
//         logHead = nextHead;
//       }

//       digitalWrite(LOG_BUSY_PIN, LOW);  // Logging done
//       lastLogTime = millis();
//     }
//     vTaskDelay(10 / portTICK_PERIOD_MS);  // Yield to other tasks
//   }
// }

void TaskLog(void* pvParameters) {
  // Track timings
  unsigned long lastLogTime = 0;         // time of last (attempted) logging check
  unsigned long lastActualLogTime = 0;   // time we actually wrote to buffer (used for 10s forced logging)
  
  // Track last logged ADC value for deviation check
  float lastLoggedAdc = NAN;
  float adc = 0.0f;
  bool shouldLog = false;

  // --- Log first sample immediately on start ---
  {
    digitalWrite(LOG_BUSY_PIN, HIGH);

    LogData firstSample;
    firstSample.uptimeSec = millis() / 1000;
    firstSample.adcValue = get_reading();

    // Save with overwrite-if-full
    logs[logHead] = firstSample;
    int nextHead = (logHead + 1) % MAX_LOGS;
    if (nextHead == logTail) {
      // buffer full, advance tail to overwrite oldest
      logTail = (logTail + 1) % MAX_LOGS;
    }
    logHead = nextHead;

    digitalWrite(LOG_BUSY_PIN, LOW);

    lastLoggedAdc = firstSample.adcValue;
    lastActualLogTime = millis();
    lastLogTime = millis();
  }
  
  unsigned long t_led_flag = millis();
  bool last_led_state = false;
  while (true) {
    unsigned long now = millis();
    // Check every 1 second
    if (now - lastLogTime >= 1000UL) {
      lastLogTime = now;

      adc = get_reading();

      bool within5Percent = false;
      if (!isnan(lastLoggedAdc)) {
        // relative deviation: |adc - lastLoggedAdc| / max(|lastLoggedAdc|, small_value)
        float denom = (fabs(lastLoggedAdc) < 1e-6f) ? 1.0f : fabs(lastLoggedAdc);
        float relDev = fabs(adc - lastLoggedAdc) / denom;
        within5Percent = (relDev <= 0.05f);
      }

      shouldLog = false;

      if (within5Percent) {
        // If within 5%: only log if 10 seconds have passed since the last actual log
        if (now - lastActualLogTime < 10000UL) {
          shouldLog = false;
        } else {
          shouldLog = true; // forced log after 10s
        }
      } else {
        // reading outside 5% -> log now
        shouldLog = true;
      }
    }

    if ((millis() - t_led_flag) >= 100UL) {
        t_led_flag = millis();
        last_led_state = !last_led_state;
        digitalWrite(LOG_BUSY_PIN, last_led_state);

        if (shouldLog && last_led_state) {
            shouldLog = false; // reset flag

            LogData sample;
            sample.uptimeSec = now / 1000;
            sample.adcValue = adc;

            // Save to circular buffer with overwrite-if-full
            logs[logHead] = sample;
            int nextHead = (logHead + 1) % MAX_LOGS;
            if (nextHead == logTail) {
                // Buffer full, advance tail to overwrite oldest
                logTail = (logTail + 1) % MAX_LOGS;
            }
            logHead = nextHead;

            // update last-logged info
            lastLoggedAdc = adc;
            lastActualLogTime = now;
        } 
    }   
    // yield to other tasks
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}


// ---------- TASK 2: WIFI UPLOADER (CORE 1) ----------
void TaskWiFi(void* pvParameters) {
  connect_to_wifi(0);
  config_time();

  unsigned long lastUploadTime = millis();
  bool data_available = false;
  LogData data;
  String timeStr = "";
  String adcStr = "";
  while (true) {
    // Try reconnecting if WiFi lost
    if (WiFi.status() != WL_CONNECTED) {
      connect_to_wifi(0);
    }

    // Check for new data to upload
    if (logTail != logHead && !data_available) {
      while(digitalRead(LOG_BUSY_PIN) == LOW); // wait for logging to complete
      while(digitalRead(LOG_BUSY_PIN) == HIGH); // wait for logging to complete
      for(int i =0; i<10; i++){ // small delay to ensure logging done
        data = logs[logTail];
        logTail = (logTail + 1) % MAX_LOGS;
        timeStr = timeStr + String(data.uptimeSec) + ":";
        adcStr = adcStr + String(data.adcValue) + ":";
        if(logTail == logHead) break;
      }
      data_available = true;
    }

    // Upload every 10s or if backlog exists
    if (millis() - lastUploadTime >= 5000 && WiFi.status()==WL_CONNECTED && data_available) {
      data_available = false;
      sendData(timeStr, adcStr);
      // Error handle later.
      timeStr = "";
      adcStr = "";
      lastUploadTime = millis();
      digitalWrite(LED, !digitalRead(LED)); // toggle LED
//      watch_dog_reset();
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(LOG_BUSY_PIN, OUTPUT);
  digitalWrite(LOG_BUSY_PIN, LOW);

//  watch_dog_init();

  // Start both tasks on different cores
  xTaskCreatePinnedToCore(TaskLog, "TaskLog", 4096, NULL, 1, &TaskLogHandle, 0);
  xTaskCreatePinnedToCore(TaskWiFi, "TaskWiFi", 8192, NULL, 1, &TaskWiFiHandle, 1);

  Serial.println("✅ Dual-core logging system started!");
}

// ---------- LOOP ----------
void loop() {
  // Nothing here — handled by tasks
}

// ---------- WIFI & UTILITIES ----------
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
    if(millis()-last_t__ > 7e3) 
      break;
  }
  if(WiFi.status() == WL_CONNECTED){
    Serial.println("\n✅ Connected to WiFi: " + String(WIFI_SSID[attemt]) + "\nIP-Address is: " + WiFi.localIP().toString().c_str());
    delay(100);
  }
  else connect_to_wifi((attemt+1) % NO_OF_SSID);
}

void config_time() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

String getLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "0";
  char timeString[32];
  strftime(timeString, sizeof(timeString), "%Y.%m.%d_%H:%M:%S", &timeinfo);
  return String(timeString);
}

void sendData(String sensor, String val) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(scriptUrl) + "?sensor=" + sensor + "&value=" + val;
    http.begin(url);
    Serial.println(String("Sent:  ") + "?sensor=" + sensor + "&value=" + val);
    
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Server response: " + response);
    } 
    else {
      Serial.println("Error sending data. Code: " + String(httpResponseCode));
    }

    http.end();
  }
}

int get_reading() {
  unsigned long startTime = millis();
  int minVal = 4095, maxVal = 0;
  unsigned long lastSampleTime = micros();

  while (millis() - startTime < 80) {
    unsigned long now = micros();
    if (now - lastSampleTime >= 1875) {
      lastSampleTime = now;
      int adcVal = analogRead(ADC_PIN);
      if (adcVal < minVal) minVal = adcVal;
      if (adcVal > maxVal) maxVal = adcVal;
    }
  }
  return maxVal - minVal;
}
}
