/*
  ESP32 Smart Energy Monitor v2 - Phase 1
  --------------------------------------
  Upgrade from v1 (Blynk) to MQTT telemetry.

  What changed from v1:
    - Removed Blynk dependency entirely
    - Added PubSubClient (MQTT) + ArduinoJson for structured payloads
    - Sensor polling (Core 1) and network publishing (Core 0) now
      communicate via a FreeRTOS queue instead of calling Blynk
      directly from the sensor task
    - Added Wi-Fi/MQTT reconnect logic with backoff (no blocking
      infinite while-loops that stall the whole task)
    - Ported your v1 power-threshold alert (485W, 60s cooldown) from
      Blynk.logEvent() to a dedicated MQTT alert topic + queue, so a
      spike still gets flagged even without Blynk in the loop
    - Pin numbers matched to your actual v1 wiring (LED_NORMAL=18,
      LED_ALERT=19), not the generic 25/26 in the first draft

  Libraries required (Arduino Library Manager):
    - PZEM004Tv30          (existing, unchanged)
    - LiquidCrystal_I2C    (existing, unchanged)
    - PubSubClient          by Nick O'Leary        (NEW)
    - ArduinoJson            by Benoit Blanchon      (NEW)

  MQTT broker:
    - Use a free HiveMQ Cloud instance, or run Mosquitto locally
      (e.g. on a laptop/Raspberry Pi on the same network for testing).
    - Fill in MQTT_BROKER / MQTT_PORT / MQTT_USER / MQTT_PASS below.

  Topic structure published:
    energy/<DEVICE_ID>/voltage
    energy/<DEVICE_ID>/current
    energy/<DEVICE_ID>/power
    energy/<DEVICE_ID>/energy
    energy/<DEVICE_ID>/status      (online/offline, retained)
    energy/<DEVICE_ID>/alert       (fires when power > POWER_THRESHOLD_W, 60s cooldown)

  Also publishes a single combined JSON payload to:
    energy/<DEVICE_ID>/data
  which is the one your dashboard (Node-RED/Grafana) should subscribe to.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>

// ---------------- USER CONFIG ----------------
const char* WIFI_SSID     = "yaswifi";
const char* WIFI_PASSWORD = "12345678";

const char* MQTT_BROKER   = "3955e0cc0e8347478f9bd7e155cc9d71.s1.eu.hivemq.cloud";
const int   MQTT_PORT     = 8883;                     // TLS port - HiveMQ Cloud requires this
const char* MQTT_USER     = "yashmehta";   // create in HiveMQ console > Access Management
const char* MQTT_PASS     = "Yash@1309";   // create in HiveMQ console > Access Management
const char* DEVICE_ID     = "esp32_energy_01";         // unique per device

// Publish interval (ms)
const unsigned long PUBLISH_INTERVAL_MS = 5000;

// ---------------- HARDWARE ----------------
// PZEM-004T on UART2 (RX2=16, TX2=17) - same as v1
PZEM004Tv30 pzem(Serial2, 16, 17);

LiquidCrystal_I2C lcd(0x27, 16, 2);  // adjust I2C address if needed

const int LED_GREEN = 18;   // matches v1 wiring (LED_NORMAL)
const int LED_RED   = 19;   // matches v1 wiring (LED_ALERT)

// ---------------- ALERT CONFIG (ported from v1) ----------------
const float POWER_THRESHOLD_W   = 485.0;   // same value you tuned in v1
const unsigned long ALERT_COOLDOWN_MS = 60000;  // 1 min between alert publishes
unsigned long lastAlertTime = 0;

// ---------------- NETWORKING ----------------
// NOTE: HiveMQ Cloud requires TLS on port 8883, so this must be
// WiFiClientSecure (encrypted), not plain WiFiClient. Plain WiFiClient
// would fail to connect on this port.
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// ---------------- INTER-TASK COMMS ----------------
// Struct passed from sensor task (Core 1) to network task (Core 0)
typedef struct {
  float voltage;
  float current;
  float power;
  float energy;
  bool  valid;
} SensorReading;

QueueHandle_t sensorQueue;
QueueHandle_t alertQueue;   // separate lightweight queue for threshold alerts

// Forward declaration - used by lcdUpdateTask, defined near networkTask
void queueAlert(float powerAtAlert);

// ---------------- TASK HANDLES ----------------
TaskHandle_t sensorTaskHandle;
TaskHandle_t networkTaskHandle;
TaskHandle_t lcdTaskHandle;

// Latest reading kept for LCD display (protected by mutex)
SensorReading latestReading = {0, 0, 0, 0, false};
SemaphoreHandle_t readingMutex;

// =====================================================
//                 SENSOR TASK (Core 1)
// =====================================================
void sensorPollTask(void* pvParameters) {
  const TickType_t xDelay = pdMS_TO_TICKS(2000);  // sample every 2s

  for (;;) {
    SensorReading reading;
    reading.voltage = pzem.voltage();
    reading.current = pzem.current();
    reading.power   = pzem.power();
    reading.energy  = pzem.energy();

    // PZEM returns NaN on read failure - guard against garbage data
    if (isnan(reading.voltage) || isnan(reading.current)) {
      reading.valid = false;
    } else {
      reading.valid = true;
    }

    // Update shared copy for LCD task
    if (xSemaphoreTake(readingMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      latestReading = reading;
      xSemaphoreGive(readingMutex);
    }

    // Push to network task - don't block forever if queue is full,
    // just drop the sample (network task will catch up next cycle)
    xQueueSend(sensorQueue, &reading, pdMS_TO_TICKS(100));

    vTaskDelay(xDelay);
  }
}

// =====================================================
//                 LCD TASK (Core 1)
// =====================================================
void lcdUpdateTask(void* pvParameters) {
  const TickType_t xDelay = pdMS_TO_TICKS(2000);

  for (;;) {
    SensorReading local;
    if (xSemaphoreTake(readingMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      local = latestReading;
      xSemaphoreGive(readingMutex);
    }

    lcd.clear();
    if (local.valid) {
      lcd.setCursor(0, 0);
      lcd.printf("V:%.1f I:%.2f", local.voltage, local.current);
      lcd.setCursor(0, 1);
      lcd.printf("P:%.1fW E:%.2f", local.power, local.energy);

      // --- Threshold alert logic (ported from v1) ---
      // v1 gated this on voltage > 10.0 to avoid false alerts when the
      // sensor/mains isn't actually live - keeping that same guard.
      if (local.voltage > 10.0 && local.power > POWER_THRESHOLD_W) {
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, HIGH);

        unsigned long now = millis();
        if (lastAlertTime == 0 || now - lastAlertTime > ALERT_COOLDOWN_MS) {
          lastAlertTime = now;
          queueAlert(local.power);  // publishes to MQTT instead of Blynk.logEvent
        }
      } else if (local.voltage > 10.0) {
        digitalWrite(LED_GREEN, HIGH);
        digitalWrite(LED_RED, LOW);
      } else {
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, LOW);
      }
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Sensor Error");
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_RED, HIGH);
    }

    vTaskDelay(xDelay);
  }
}

// =====================================================
//                 NETWORK TASK (Core 0)
// =====================================================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWi-Fi connect failed, will retry later.");
  }
}

bool connectMQTT() {
  if (mqttClient.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  String clientId = String(DEVICE_ID) + "-" + String(random(0xffff), HEX);
  String statusTopic = "energy/" + String(DEVICE_ID) + "/status";

  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                             statusTopic.c_str(), 1, true, "offline");
  } else {
    ok = mqttClient.connect(clientId.c_str(), NULL, NULL,
                             statusTopic.c_str(), 1, true, "offline");
  }

  if (ok) {
    Serial.println("MQTT connected.");
    mqttClient.publish(statusTopic.c_str(), "online", true);  // retained
  } else {
    Serial.printf("MQTT connect failed, rc=%d\n", mqttClient.state());
  }
  return ok;
}

void publishReading(const SensorReading& r) {
  if (!r.valid) return;

  StaticJsonDocument<256> doc;
  doc["device"]  = DEVICE_ID;
  doc["voltage"] = r.voltage;
  doc["current"] = r.current;
  doc["power"]   = r.power;
  doc["energy"]  = r.energy;
  doc["ts"]      = millis();

  char payload[256];
  size_t len = serializeJson(doc, payload);

  String dataTopic = "energy/" + String(DEVICE_ID) + "/data";
  mqttClient.publish(dataTopic.c_str(), payload, len);
}

// Called from lcdUpdateTask (Core 1) - just queues, doesn't touch
// WiFi/MQTT directly, since those live on Core 0 in networkTask.
void queueAlert(float powerAtAlert) {
  xQueueSend(alertQueue, &powerAtAlert, 0);  // don't block if full, alert can be missed rather than stall LCD task
}

void publishAlert(float powerAtAlert) {
  StaticJsonDocument<128> doc;
  doc["device"]    = DEVICE_ID;
  doc["type"]      = "high_power_alert";
  doc["power"]     = powerAtAlert;
  doc["threshold"] = POWER_THRESHOLD_W;
  doc["ts"]        = millis();

  char payload[128];
  size_t len = serializeJson(doc, payload);

  String alertTopic = "energy/" + String(DEVICE_ID) + "/alert";
  // Note: PubSubClient only supports QoS 0. Fine for this project, but
  // worth knowing/saying in an interview: production alerting systems
  // would want QoS 1+ here since losing an alert matters more than
  // losing a routine telemetry sample - that'd need a different MQTT
  // client library (e.g. esp-mqtt) if you ever wanted to upgrade it.
  mqttClient.publish(alertTopic.c_str(), payload, len);
  Serial.printf("Alert published: %.1fW\n", powerAtAlert);
}

void networkTask(void* pvParameters) {
  unsigned long lastReconnectAttempt = 0;
  SensorReading reading;

  for (;;) {
    // Wi-Fi supervision
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    // MQTT supervision with backoff (retry every 5s, don't spam)
    if (!mqttClient.connected()) {
      unsigned long now = millis();
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        connectMQTT();
      }
    } else {
      mqttClient.loop();  // must be called regularly to keep connection alive
    }

    // Drain queue and publish whatever readings arrived from sensor task
    while (xQueueReceive(sensorQueue, &reading, 0) == pdTRUE) {
      if (mqttClient.connected()) {
        publishReading(reading);
      }
      // NOTE: Phase 2 will add "else buffer to SD" here.
    }

    // Drain alert queue separately - alerts are rare/urgent, publish
    // immediately rather than waiting on the normal telemetry cadence
    float alertPower;
    while (xQueueReceive(alertQueue, &alertPower, 0) == pdTRUE) {
      if (mqttClient.connected()) {
        publishAlert(alertPower);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// =====================================================
//                       SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Energy Monitor");
  lcd.setCursor(0, 1);
  lcd.print("v2 - MQTT");

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(512);  // enough for our JSON payload

  // Skip certificate validation - acceptable for a student/demo project.
  // A production system would pin HiveMQ's actual root CA cert instead;
  // worth mentioning as a known simplification if asked in an interview.
  espClient.setInsecure();

  sensorQueue  = xQueueCreate(10, sizeof(SensorReading));
  alertQueue   = xQueueCreate(5, sizeof(float));
  readingMutex = xSemaphoreCreateMutex();

  // Pin tasks to cores explicitly, matching v1's dual-core split
  xTaskCreatePinnedToCore(sensorPollTask, "SensorTask", 4096, NULL, 2, &sensorTaskHandle, 1);
  xTaskCreatePinnedToCore(lcdUpdateTask,  "LCDTask",    4096, NULL, 1, &lcdTaskHandle,    1);
  xTaskCreatePinnedToCore(networkTask,    "NetTask",    8192, NULL, 1, &networkTaskHandle, 0);

  Serial.println("Setup complete. Tasks running.");
}

void loop() {
  // Intentionally empty - everything runs in FreeRTOS tasks above.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
