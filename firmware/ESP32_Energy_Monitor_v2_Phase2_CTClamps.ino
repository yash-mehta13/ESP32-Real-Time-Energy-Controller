/*
  ESP32 Smart Energy Monitor v2 - Phase 2
  --------------------------------------
  Adds dual CT-clamp sensing (2 extra circuits) on top of the
  verified-working Phase 1 MQTT/TLS base.

  What's new vs Phase 1:
    - Two bare SCT-013 CT clamps read via ESP32 ADC (GPIO34, GPIO35)
    - RMS current computed by sampling a full AC cycle (20ms @ 50Hz)
      and subtracting the DC bias, per clamp
    - Calibration constant (CT_RATIO) converts ADC-domain RMS back to
      real-world amps - MUST be tuned against a multimeter (see notes)
    - New CT sampling task runs on Core 1 alongside the PZEM sensor
      task, since ADC sampling is also a tight-timing local operation
    - MQTT payload extended: circuit1/circuit2 current now included
      in the combined /data JSON, plus separate /circuit1 and
      /circuit2 topics for dashboard convenience

  Hardware assumption (confirm against your clamp's printed label):
    - Bare CT clamp, 100A : 50mA ratio (2000:1 turns)
    - 22 ohm burden resistor across each clamp's leads
    - 10k/10k divider + 10uF cap biasing ADC input to 1.65V
    - If your clamp's rating differs, change CT_RATIO below accordingly:
        CT_RATIO = (primary_max_amps) / (secondary_max_amps)
        e.g. 100A:50mA -> CT_RATIO = 100 / 0.05 = 2000

  CALIBRATION IS REQUIRED before trusting these readings:
    1. Clamp CT1 around a load with a KNOWN current draw (measure with
       a multimeter in series, or use a load with known wattage).
    2. Compare the Serial-printed raw current to the multimeter reading.
    3. Adjust CALIBRATION_FACTOR (starts at 1.0) until they match:
         CALIBRATION_FACTOR = true_current_multimeter / raw_reported_current
    4. Repeat for CT2 separately - each clamp can have slightly
       different calibration due to component tolerances.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>

// ---------------- USER CONFIG (unchanged from Phase 1) ----------------
const char* WIFI_SSID     = "yaswifi";
const char* WIFI_PASSWORD = "12345678";

const char* MQTT_BROKER   = "3955e0cc0e8347478f9bd7e155cc9d71.s1.eu.hivemq.cloud";
const int   MQTT_PORT     = 8883;
const char* MQTT_USER     = "yashmehta";
const char* MQTT_PASS     = "Yash@1309";
const char* DEVICE_ID     = "esp32_energy_01";

// ---------------- HARDWARE (Phase 1, unchanged) ----------------
PZEM004Tv30 pzem(Serial2, 16, 17);
LiquidCrystal_I2C lcd(0x27, 16, 2);

const int LED_GREEN = 18;
const int LED_RED   = 19;

const float POWER_THRESHOLD_W   = 485.0;
const unsigned long ALERT_COOLDOWN_MS = 60000;
unsigned long lastAlertTime = 0;

// ---------------- NEW: CT CLAMP CONFIG (Phase 2) ----------------
const int CT1_PIN = 34;   // ADC1 channel, input-only, Wi-Fi safe
const int CT2_PIN = 35;   // ADC1 channel, input-only, Wi-Fi safe

const float CT_RATIO = 2000.0;        // 100A:50mA bare CT -> confirm against your clamp's label
const float ADC_VREF = 3.3;           // ESP32 ADC reference voltage
const int   ADC_RESOLUTION = 4095;    // 12-bit ADC (0-4095)
const int   SAMPLES_PER_CYCLE = 200;  // sampling density for RMS calc over one 20ms cycle

// Per-clamp calibration factors - START AT 1.0, tune per the notes above
float CT1_CALIBRATION = 1.0;
float CT2_CALIBRATION = 1.0;

// ---------------- NETWORKING (unchanged) ----------------
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// ---------------- INTER-TASK COMMS ----------------
typedef struct {
  float voltage;
  float current;
  float power;
  float energy;
  bool  valid;
} SensorReading;

// NEW: struct for the two CT circuits
typedef struct {
  float ct1_current;
  float ct2_current;
} CTReading;

QueueHandle_t sensorQueue;
QueueHandle_t ctQueue;      // NEW
QueueHandle_t alertQueue;

void queueAlert(float powerAtAlert);

TaskHandle_t sensorTaskHandle;
TaskHandle_t ctTaskHandle;   // NEW
TaskHandle_t networkTaskHandle;
TaskHandle_t lcdTaskHandle;

SensorReading latestReading = {0, 0, 0, 0, false};
CTReading latestCTReading = {0, 0};   // NEW
SemaphoreHandle_t readingMutex;
SemaphoreHandle_t ctMutex;            // NEW

// =====================================================
//     NEW: CT CLAMP RMS SAMPLING (Core 1)
// =====================================================
// Samples one clamp for one full AC cycle (~20ms @ 50Hz) and returns
// the calibrated RMS current in amps.
float readCTRms(int pin, float calibration) {
  long sumSq = 0;
  int sample;

  // Rough DC bias estimate: average a batch of samples first
  long sumRaw = 0;
  for (int i = 0; i < SAMPLES_PER_CYCLE; i++) {
    sumRaw += analogRead(pin);
  }
  float dcBias = (float)sumRaw / SAMPLES_PER_CYCLE;

  // Now sample again and accumulate sum of squares of the
  // bias-subtracted signal - this is the actual RMS calculation
  for (int i = 0; i < SAMPLES_PER_CYCLE; i++) {
    sample = analogRead(pin);
    float centered = sample - dcBias;
    sumSq += (long)(centered * centered);
    delayMicroseconds(100);  // spread samples across ~20ms total
  }

  float meanSq = (float)sumSq / SAMPLES_PER_CYCLE;
  float rmsADCCounts = sqrt(meanSq);

  // Convert ADC counts -> volts -> secondary-side amps -> primary-side amps
  float rmsVoltage = (rmsADCCounts / ADC_RESOLUTION) * ADC_VREF;
  float secondaryAmps = rmsVoltage / 22.0;   // 22 ohm burden resistor
  float primaryAmps = secondaryAmps * CT_RATIO;

  return primaryAmps * calibration;
}

void ctSamplingTask(void* pvParameters) {
  const TickType_t xDelay = pdMS_TO_TICKS(2000);  // sample every 2s, matches PZEM cadence

  for (;;) {
    CTReading reading;
    reading.ct1_current = readCTRms(CT1_PIN, CT1_CALIBRATION);
    reading.ct2_current = readCTRms(CT2_PIN, CT2_CALIBRATION);

    if (xSemaphoreTake(ctMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      latestCTReading = reading;
      xSemaphoreGive(ctMutex);
    }

    xQueueSend(ctQueue, &reading, pdMS_TO_TICKS(100));

    // Debug print - remove once calibration is confirmed stable
    Serial.printf("CT1: %.3fA | CT2: %.3fA\n", reading.ct1_current, reading.ct2_current);

    vTaskDelay(xDelay);
  }
}

// =====================================================
//                 SENSOR TASK (Core 1) - unchanged from Phase 1
// =====================================================
void sensorPollTask(void* pvParameters) {
  const TickType_t xDelay = pdMS_TO_TICKS(2000);

  for (;;) {
    SensorReading reading;
    reading.voltage = pzem.voltage();
    reading.current = pzem.current();
    reading.power   = pzem.power();
    reading.energy  = pzem.energy();

    if (isnan(reading.voltage) || isnan(reading.current)) {
      reading.valid = false;
    } else {
      reading.valid = true;
    }

    if (xSemaphoreTake(readingMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      latestReading = reading;
      xSemaphoreGive(readingMutex);
    }

    xQueueSend(sensorQueue, &reading, pdMS_TO_TICKS(100));
    vTaskDelay(xDelay);
  }
}

// =====================================================
//                 LCD TASK (Core 1) - unchanged from Phase 1
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

      if (local.voltage > 10.0 && local.power > POWER_THRESHOLD_W) {
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, HIGH);

        unsigned long now = millis();
        if (lastAlertTime == 0 || now - lastAlertTime > ALERT_COOLDOWN_MS) {
          lastAlertTime = now;
          queueAlert(local.power);
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
    mqttClient.publish(statusTopic.c_str(), "online", true);
  } else {
    Serial.printf("MQTT connect failed, rc=%d\n", mqttClient.state());
  }
  return ok;
}

// UPDATED: now includes ct1/ct2 in the combined payload
void publishReading(const SensorReading& r, const CTReading& ct) {
  if (!r.valid) return;

  StaticJsonDocument<320> doc;
  doc["device"]       = DEVICE_ID;
  doc["voltage"]      = r.voltage;
  doc["current"]      = r.current;
  doc["power"]        = r.power;
  doc["energy"]       = r.energy;
  doc["ct1_current"]  = ct.ct1_current;
  doc["ct2_current"]  = ct.ct2_current;
  doc["ts"]           = millis();

  char payload[320];
  size_t len = serializeJson(doc, payload);

  String dataTopic = "energy/" + String(DEVICE_ID) + "/data";
  mqttClient.publish(dataTopic.c_str(), payload, len);
}

void queueAlert(float powerAtAlert) {
  xQueueSend(alertQueue, &powerAtAlert, 0);
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
  mqttClient.publish(alertTopic.c_str(), payload, len);
  Serial.printf("Alert published: %.1fW\n", powerAtAlert);
}

void networkTask(void* pvParameters) {
  unsigned long lastReconnectAttempt = 0;
  SensorReading reading;
  CTReading ctReading;
  bool haveCTReading = false;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    if (!mqttClient.connected()) {
      unsigned long now = millis();
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        connectMQTT();
      }
    } else {
      mqttClient.loop();
    }

    // Grab the latest CT reading if one arrived (non-blocking)
    if (xQueueReceive(ctQueue, &ctReading, 0) == pdTRUE) {
      haveCTReading = true;
    }

    while (xQueueReceive(sensorQueue, &reading, 0) == pdTRUE) {
      if (mqttClient.connected() && haveCTReading) {
        publishReading(reading, ctReading);
      }
    }

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

  // NEW: ADC config for CT clamps
  analogReadResolution(12);           // 0-4095
  analogSetPinAttenuation(CT1_PIN, ADC_11db);  // full 0-3.3V range
  analogSetPinAttenuation(CT2_PIN, ADC_11db);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Energy Monitor");
  lcd.setCursor(0, 1);
  lcd.print("v2 - Phase 2");

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(512);
  espClient.setInsecure();

  sensorQueue  = xQueueCreate(10, sizeof(SensorReading));
  ctQueue      = xQueueCreate(10, sizeof(CTReading));    // NEW
  alertQueue   = xQueueCreate(5, sizeof(float));
  readingMutex = xSemaphoreCreateMutex();
  ctMutex      = xSemaphoreCreateMutex();                // NEW

  xTaskCreatePinnedToCore(sensorPollTask, "SensorTask", 4096, NULL, 2, &sensorTaskHandle, 1);
  xTaskCreatePinnedToCore(ctSamplingTask, "CTTask",     4096, NULL, 2, &ctTaskHandle,     1);  // NEW
  xTaskCreatePinnedToCore(lcdUpdateTask,  "LCDTask",    4096, NULL, 1, &lcdTaskHandle,    1);
  xTaskCreatePinnedToCore(networkTask,    "NetTask",    8192, NULL, 1, &networkTaskHandle, 0);

  Serial.println("Setup complete. Tasks running (Phase 2: CT clamps active).");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
