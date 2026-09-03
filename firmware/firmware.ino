#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>

// =====================================================
//                    CONFIGURATION
// =====================================================

#define BLYNK_PRINT Serial

#define BLYNK_TEMPLATE_ID "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Energy Monitor"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

// =====================================================
//                    HARDWARE
// =====================================================

#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17

#define LED_NORMAL 18
#define LED_ALERT  19

#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS    2

PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);

// =====================================================
//                 PROTECTION CONFIG
// =====================================================

const float POWER_THRESHOLD_W = 485.0f;

const unsigned long SENSOR_PERIOD_MS   = 1000;
const unsigned long DISPLAY_PERIOD_MS  = 500;
const unsigned long CONTROL_PERIOD_MS  = 100;
const unsigned long TELEMETRY_PERIOD_MS = 2000;

// =====================================================
//                 DATA STRUCTURES
// =====================================================

struct Measurement
{
    float voltage;
    float current;
    float power;
    float energy;

    bool valid;

    uint32_t timestamp;
};

Measurement latestMeasurement = {
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    false,
    0
};

// =====================================================
//                 SYSTEM STATE
// =====================================================

enum SystemState
{
    SYSTEM_INIT,
    SYSTEM_NORMAL,
    SYSTEM_WARNING,
    SYSTEM_FAULT
};

SystemState systemState = SYSTEM_INIT;

// =====================================================
//                 RTOS OBJECTS
// =====================================================

SemaphoreHandle_t measurementMutex;

TaskHandle_t sensorTaskHandle;
TaskHandle_t controlTaskHandle;
TaskHandle_t displayTaskHandle;
TaskHandle_t telemetryTaskHandle;

// =====================================================
//                 FUNCTION DECLARATIONS
// =====================================================

void sensorTask(void *parameter);
void controlTask(void *parameter);
void displayTask(void *parameter);
void telemetryTask(void *parameter);

void updateSystemState(const Measurement &measurement);
void updateIndicators(SystemState state);

const char* getStateName(SystemState state);

// =====================================================
//                    SENSOR TASK
// =====================================================

void sensorTask(void *parameter)
{
    Serial.println("[SensorTask] Started");

    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        Measurement reading;

        reading.voltage = pzem.voltage();
        reading.current = pzem.current();
        reading.power   = pzem.power();
        reading.energy  = pzem.energy();

        reading.timestamp = millis();

        // Validate PZEM measurement
        if (isnan(reading.voltage) ||
            isnan(reading.current) ||
            isnan(reading.power) ||
            isnan(reading.energy))
        {
            reading.valid = false;
        }
        else
        {
            reading.valid = true;
        }

        // Update shared measurement
        if (xSemaphoreTake(measurementMutex,
                           pdMS_TO_TICKS(50)) == pdTRUE)
        {
            latestMeasurement = reading;

            xSemaphoreGive(measurementMutex);
        }

        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(SENSOR_PERIOD_MS)
        );
    }
}

// =====================================================
//                  CONTROL TASK
// =====================================================

void controlTask(void *parameter)
{
    Serial.println("[ControlTask] Started");

    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        Measurement measurement;

        // Obtain latest sensor data
        if (xSemaphoreTake(measurementMutex,
                           pdMS_TO_TICKS(50)) == pdTRUE)
        {
            measurement = latestMeasurement;

            xSemaphoreGive(measurementMutex);
        }

        updateSystemState(measurement);

        updateIndicators(systemState);

        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(CONTROL_PERIOD_MS)
        );
    }
}

// =====================================================
//                  DISPLAY TASK
// =====================================================

void displayTask(void *parameter)
{
    Serial.println("[DisplayTask] Started");

    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        Measurement measurement;

        if (xSemaphoreTake(measurementMutex,
                           pdMS_TO_TICKS(50)) == pdTRUE)
        {
            measurement = latestMeasurement;

            xSemaphoreGive(measurementMutex);
        }

        lcd.clear();

        if (!measurement.valid)
        {
            lcd.setCursor(0, 0);
            lcd.print("Sensor Error");

            lcd.setCursor(0, 1);
            lcd.print("Check PZEM");
        }
        else
        {
            lcd.setCursor(0, 0);
            lcd.printf(
                "V:%.1f I:%.2f",
                measurement.voltage,
                measurement.current
            );

            lcd.setCursor(0, 1);
            lcd.printf(
                "P:%.1fW",
                measurement.power
            );
        }

        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(DISPLAY_PERIOD_MS)
        );
    }
}

// =====================================================
//                 TELEMETRY TASK
// =====================================================

void telemetryTask(void *parameter)
{
    Serial.println("[TelemetryTask] Started");

    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        Measurement measurement;

        if (xSemaphoreTake(measurementMutex,
                           pdMS_TO_TICKS(50)) == pdTRUE)
        {
            measurement = latestMeasurement;

            xSemaphoreGive(measurementMutex);
        }

        if (measurement.valid)
        {
            Blynk.virtualWrite(V0, measurement.voltage);
            Blynk.virtualWrite(V1, measurement.current);
            Blynk.virtualWrite(V2, measurement.power);
            Blynk.virtualWrite(V3, measurement.energy);
        }

        Blynk.run();

        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(TELEMETRY_PERIOD_MS)
        );
    }
}

// =====================================================
//                SYSTEM STATE LOGIC
// =====================================================

void updateSystemState(const Measurement &measurement)
{
    SystemState previousState = systemState;

    if (!measurement.valid)
    {
        systemState = SYSTEM_FAULT;
    }
    else if (measurement.voltage <= 10.0f)
    {
        systemState = SYSTEM_INIT;
    }
    else if (measurement.power > POWER_THRESHOLD_W)
    {
        systemState = SYSTEM_WARNING;
    }
    else
    {
        systemState = SYSTEM_NORMAL;
    }

    // Print only when state changes
    if (systemState != previousState)
    {
        Serial.print("[SYSTEM] State changed: ");
        Serial.print(getStateName(previousState));
        Serial.print(" -> ");
        Serial.println(getStateName(systemState));
    }
}

// =====================================================
//                 INDICATOR CONTROL
// =====================================================

void updateIndicators(SystemState state)
{
    switch (state)
    {
        case SYSTEM_NORMAL:

            digitalWrite(LED_NORMAL, HIGH);
            digitalWrite(LED_ALERT, LOW);

            break;

        case SYSTEM_WARNING:

            digitalWrite(LED_NORMAL, LOW);
            digitalWrite(LED_ALERT, HIGH);

            break;

        case SYSTEM_FAULT:

            digitalWrite(LED_NORMAL, LOW);
            digitalWrite(LED_ALERT, HIGH);

            break;

        case SYSTEM_INIT:

        default:

            digitalWrite(LED_NORMAL, LOW);
            digitalWrite(LED_ALERT, LOW);

            break;
    }
}

// =====================================================
//                 STATE NAME HELPER
// =====================================================

const char* getStateName(SystemState state)
{
    switch (state)
    {
        case SYSTEM_INIT:
            return "INIT";

        case SYSTEM_NORMAL:
            return "NORMAL";

        case SYSTEM_WARNING:
            return "WARNING";

        case SYSTEM_FAULT:
            return "FAULT";

        default:
            return "UNKNOWN";
    }
}

// =====================================================
//                       SETUP
// =====================================================

void setup()
{
    Serial.begin(115200);
    Serial2.begin(9600);

    delay(500);

    Serial.println();
    Serial.println("================================");
    Serial.println(" REAL-TIME ENERGY CONTROLLER");
    Serial.println(" ESP32 + FreeRTOS");
    Serial.println("================================");

    // -------------------------------------------------
    // GPIO
    // -------------------------------------------------

    pinMode(LED_NORMAL, OUTPUT);
    pinMode(LED_ALERT, OUTPUT);

    digitalWrite(LED_NORMAL, LOW);
    digitalWrite(LED_ALERT, LOW);

    // -------------------------------------------------
    // LCD
    // -------------------------------------------------

    lcd.init();
    lcd.backlight();

    lcd.setCursor(0, 0);
    lcd.print("Energy Controller");

    lcd.setCursor(0, 1);
    lcd.print("System Booting");

    // -------------------------------------------------
    // RTOS Synchronization
    // -------------------------------------------------

    measurementMutex = xSemaphoreCreateMutex();

    if (measurementMutex == NULL)
    {
        Serial.println("[ERROR] Mutex creation failed!");

        while (true)
        {
            delay(1000);
        }
    }

    // -------------------------------------------------
    // Wi-Fi / Blynk
    // -------------------------------------------------

    Serial.println("[SYSTEM] Connecting to Blynk...");

    Blynk.begin(auth, ssid, pass);

    Serial.println("[SYSTEM] Blynk connected");

    // -------------------------------------------------
    // Create RTOS Tasks
    // -------------------------------------------------

    xTaskCreatePinnedToCore(
        sensorTask,
        "SensorTask",
        4096,
        NULL,
        3,
        &sensorTaskHandle,
        1
    );

    xTaskCreatePinnedToCore(
        controlTask,
        "ControlTask",
        4096,
        NULL,
        4,
        &controlTaskHandle,
        1
    );

    xTaskCreatePinnedToCore(
        displayTask,
        "DisplayTask",
        4096,
        NULL,
        1,
        &displayTaskHandle,
        1
    );

    xTaskCreatePinnedToCore(
        telemetryTask,
        "TelemetryTask",
        4096,
        NULL,
        2,
        &telemetryTaskHandle,
        0
    );

    Serial.println("[SYSTEM] All tasks created");

    systemState = SYSTEM_NORMAL;
}

// =====================================================
//                        LOOP
// =====================================================

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
