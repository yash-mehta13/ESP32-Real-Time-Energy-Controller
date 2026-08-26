// --- 0. BLYNK DEBUGGING (Ye sabse upar hona zaroori hai) ---
#define BLYNK_PRINT Serial

// --- 1. CONFIGURATION ---
#define BLYNK_TEMPLATE_ID "TMPL3MeLl5KgB"
#define BLYNK_TEMPLATE_NAME "Energy Monitor"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <PZEM004Tv30.h> 
#include <LiquidCrystal_I2C.h> 

char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "YOUR_WIFI_SSID";            
char pass[] = "YOUR_WIFI_PASSWORD";     

// --- ALERT SETTINGS ---
float powerThreshold = 485.0;          // <--- Aapka 484W Threshold
unsigned long lastNotifyTime = 0;       
const unsigned long notifyCooldown = 60000; // 1 minute gap between alerts

// --- 2. PIN DEFINITIONS & OBJECTS ---
#define PZEM_RX_PIN 16  
#define PZEM_TX_PIN 17  
#define LED_NORMAL 18    // Green LED 
#define LED_ALERT 19     // Red LED 

PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
SemaphoreHandle_t dataMutex; 

struct EnergyData {
    float voltage, current, power, energy;
};
EnergyData sensorReadings; 

void TaskSensor(void *pvParameters);
void TaskBlynk(void *pvParameters);

// --- 3. TASK A: SENSOR READING (Core 1) ---
void TaskSensor(void *pvParameters) {
  for (;;) { 
    float v = pzem.voltage();
    float i = pzem.current();
    float p = pzem.power();
    float e = pzem.energy();

    if (isnan(v)) {
        v = 0.0; i = 0.0; p = 0.0; e = 0.0;
        lcd.setCursor(0, 0); lcd.print("PZEM Error!     ");
        lcd.setCursor(0, 1); lcd.print("Check Wiring    ");
    } else {
        lcd.setCursor(0, 0);
        lcd.print("V:"); lcd.print(v, 1); lcd.print("V I:"); lcd.print(i, 2); lcd.print("A  ");
        lcd.setCursor(0, 1);
        lcd.print("P:"); lcd.print(p, 1); lcd.print("W          ");
    }

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        sensorReadings.voltage = v;
        sensorReadings.current = i;
        sensorReadings.power = p;
        sensorReadings.energy = e;
        xSemaphoreGive(dataMutex); 
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); 
  }
}

// --- 4. TASK B: BLYNK CLOUD & LED CONTROL (Core 0) ---
void TaskBlynk(void *pvParameters) {
  Serial.println("\n[Blynk Task] Attempting to connect to WiFi & Cloud...");
  
  // Ye line code ko block karegi jab tak WiFi connect nahi hota
  Blynk.begin(auth, ssid, pass);
  
  Serial.println("[Blynk Task] SUCCESS! Connected to Blynk Cloud.");
  
  for (;;) {
    Blynk.run(); 
    float p_up = 0, v_up = 0, i_up = 0, e_up = 0;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        v_up = sensorReadings.voltage;
        i_up = sensorReadings.current;
        p_up = sensorReadings.power;
        e_up = sensorReadings.energy;
        xSemaphoreGive(dataMutex);
    }

    Blynk.virtualWrite(V0, v_up); 
    Blynk.virtualWrite(V1, i_up); 
    Blynk.virtualWrite(V2, p_up); 
    Blynk.virtualWrite(V3, e_up); 

    // LED LOGIC
    if (v_up > 10.0) { 
        if (p_up > powerThreshold) {
            digitalWrite(LED_NORMAL, LOW);
            digitalWrite(LED_ALERT, HIGH); 
            
            // <--- YAHI HAI WOH MAGIC LINE JO CHANGE KI HAI --->
            if (lastNotifyTime == 0 || millis() - lastNotifyTime > notifyCooldown) {
                Blynk.logEvent("high_power_alert", String("Alert! Power spike: ") + String(p_up) + "W");
                lastNotifyTime = millis(); 
                Serial.println("-> Blynk Notification Sent!");
            }
        } else {
            digitalWrite(LED_NORMAL, HIGH); 
            digitalWrite(LED_ALERT, LOW);
        }
    } else {
        digitalWrite(LED_NORMAL, LOW);
        digitalWrite(LED_ALERT, LOW);
    }

    vTaskDelay(pdMS_TO_TICKS(2000)); 
  }
}

// --- 5. MAIN SETUP WITH HEARTBEAT TEST ---
void setup() {
  Serial.begin(115200);
  delay(1000); // Give Serial Monitor a second to wake up
  Serial.println("\n\n===========================");
  Serial.println("--- SYSTEM BOOTING UP ---");
  Serial.println("===========================");

  Serial.println("Step 1: Initializing LEDs...");
  pinMode(LED_NORMAL, OUTPUT);
  pinMode(LED_ALERT, OUTPUT);
  digitalWrite(LED_NORMAL, HIGH); // Green ON temporarily for testing
  delay(500);
  digitalWrite(LED_NORMAL, LOW);
  Serial.println("-> LEDs OK.");

  Serial.println("Step 2: Initializing LCD...");
  // AGAR CODE YAHAN FREEZE HOTA HAI TOH I2C WIRE LOOSE HAI
  lcd.init(); 
  lcd.backlight();
  lcd.print("Energy Monitor");
  lcd.setCursor(0,1);
  lcd.print("Connecting...");
  Serial.println("-> LCD OK.");
  
  Serial.println("Step 3: Starting RTOS Dual-Core Tasks...");
  dataMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(TaskSensor, "SensorTask", 10000, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskBlynk, "BlynkTask", 10000, NULL, 1, NULL, 0);
  
  Serial.println("-> Tasks Created! Handing over to Wi-Fi...");
}

void loop() { 
    vTaskDelete(NULL); 
}
