#define BLYNK_PRINT Serial

#define BLYNK_TEMPLATE_ID "YOUR_BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Energy Monitor"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>

// =====================================================
//                  NETWORK CREDENTIALS
// =====================================================

char auth[] = "YOUR_BLYNK_AUTH_TOKEN";
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

PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

// =====================================================
//                 SYSTEM PARAMETERS
// =====================================================

const float POWER_THRESHOLD_W = 485.0f;

const unsigned long SENSOR_PERIOD_MS    = 1000;
const unsigned long CONTROL_PERIOD_MS   = 100;
const unsigned long DISPLAY_PERIOD_MS   = 500;
const unsigned long TELEMETRY_PERIOD_MS = 2000;

// =====================================================
//                 MEASUREMENT STRUCTURE
// =====================================================

struct Measurement
{
    float voltage;          // RMS Voltage (V)
    float current;          // RMS Current (A)
    float power;            // Real Power (W)
    float energy;           // Energy (kWh)
    float apparentPower;    // Apparent Power (VA)
    float powerFactor;      // Power Factor
    bool valid;             // Measurement validity
    unsigned long timestamp;
};

// =====================================================
//              LATEST MEASUREMENT SNAPSHOT
// =====================================================

Measurement latestMeasurement =
{
    0.0f,       // voltage
    0.0f,       // current
    0.0f,       // power
    0.0f,       // energy
    0.0f,       // apparentPower
    0.0f,       // powerFactor
    false,      // valid
    0           // timestamp
};

// =====================================================
//                  SYSTEM STATE
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
//                    RTOS OBJECTS
// =====================================================

// Mutex protecting latestMeasurement
SemaphoreHandle_t measurementMutex;

// SensorTask -> ControlTask
QueueHandle_t measurementQueue;

// =====================================================
//                    TASK HANDLES
// =====================================================

TaskHandle_t sensorTaskHandle;
TaskHandle_t controlTaskHandle;
TaskHandle_t displayTaskHandle;
TaskHandle_t networkTaskHandle;

// =====================================================
//               TASK DECLARATIONS
// =====================================================

void sensorTask(void *parameter);
void controlTask(void *parameter);
void displayTask(void *parameter);
void networkTask(void *parameter);

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

        // -------------------------------------------------
        // Read PZEM measurements
        // -------------------------------------------------

        reading.voltage = pzem.voltage();
        reading.current = pzem.current();
        reading.power   = pzem.power();
        reading.energy  = pzem.energy();

        // -------------------------------------------------
        // Calculate Apparent Power
        //
        // S = V × I
        // -------------------------------------------------

        if (!isnan(reading.voltage) &&
            !isnan(reading.current) &&
            reading.voltage > 0.0f &&
            reading.current >= 0.0f)
        {
            reading.apparentPower =
                reading.voltage * reading.current;
        }
        else
        {
            reading.apparentPower = 0.0f;
        }

        // -------------------------------------------------
        // Calculate Power Factor
        //
        // PF = P / S
        //
        // P = Real Power
        // S = Apparent Power
        // -------------------------------------------------

        if (!isnan(reading.power) &&
            reading.apparentPower > 0.0f)
        {
            reading.powerFactor =
                reading.power / reading.apparentPower;

            // Prevent impossible numerical values
            if (reading.powerFactor < 0.0f)
            {
                reading.powerFactor = 0.0f;
            }

            if (reading.powerFactor > 1.0f)
            {
                reading.powerFactor = 1.0f;
            }
        }
        else
        {
            reading.powerFactor = 0.0f;
        }

        reading.timestamp = millis();

        // -------------------------------------------------
        // Validate complete measurement
        // -------------------------------------------------

        if (isnan(reading.voltage) ||
            isnan(reading.current) ||
            isnan(reading.power) ||
            isnan(reading.energy) ||
            isnan(reading.apparentPower) ||
            isnan(reading.powerFactor))
        {
            reading.valid = false;
        }
        else
        {
            reading.valid = true;
        }

        // -------------------------------------------------
        // Send measurement to ControlTask
        // -------------------------------------------------

        if (xQueueSend(
                measurementQueue,
                &reading,
                pdMS_TO_TICKS(50)
            ) != pdPASS)
        {
            Serial.println(
                "[SensorTask] WARNING: Measurement queue full"
            );
        }

        // -------------------------------------------------
        // Update latest measurement snapshot
        // -------------------------------------------------

        if (xSemaphoreTake(
                measurementMutex,
                pdMS_TO_TICKS(50)
            ) == pdTRUE)
        {
            latestMeasurement = reading;

            xSemaphoreGive(measurementMutex);
        }

        // -------------------------------------------------
        // Periodic execution
        // -------------------------------------------------

        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(SENSOR_PERIOD_MS)
        );
    }
}

// =====================================================
//                   CONTROL TASK
// =====================================================

void controlTask(void *parameter)
{
    Serial.println("[ControlTask] Started");

    TickType_t lastWakeTime = xTaskGetTickCount();

    // Local measurement
    Measurement measurement =
    {
        0.0f,       // voltage
        0.0f,       // current
        0.0f,       // power
        0.0f,       // energy
        0.0f,       // apparentPower
        0.0f,       // powerFactor
        false,      // valid
        0           // timestamp
    };

    for (;;)
    {
        Measurement newMeasurement;

        // -------------------------------------------------
        // Receive fresh measurement
        // -------------------------------------------------

        if (xQueueReceive(
                measurementQueue,
                &newMeasurement,
                0
            ) == pdPASS)
        {
            measurement = newMeasurement;

            // -------------------------------------------------
            // Diagnostic output
            // -------------------------------------------------

            Serial.printf(
                "[ControlTask] "
                "V=%.1fV | "
                "I=%.2fA | "
                "P=%.1fW | "
                "S=%.1fVA | "
                "PF=%.3f | "
                "E=%.3fkWh | "
                "Valid=%d\n",

                measurement.voltage,
                measurement.current,
                measurement.power,
                measurement.apparentPower,
                measurement.powerFactor,
                measurement.energy,
                measurement.valid
            );

            // -------------------------------------------------
            // Run control logic
            // -------------------------------------------------

            updateSystemState(measurement);

            updateIndicators(systemState);
        }

        // -------------------------------------------------
        // Periodic execution
        // -------------------------------------------------

        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(CONTROL_PERIOD_MS)
        );
    }
}

// =====================================================
//                   DISPLAY TASK
// =====================================================

void displayTask(void *parameter)
{
    Serial.println("[DisplayTask] Started");

    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        Measurement measurement =
        {
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            false,
            0
        };

        // -------------------------------------------------
        // Read latest measurement
        // -------------------------------------------------

        if (xSemaphoreTake(
                measurementMutex,
                pdMS_TO_TICKS(50)
            ) == pdTRUE)
        {
            measurement = latestMeasurement;

            xSemaphoreGive(measurementMutex);
        }

        // -------------------------------------------------
        // LCD display
        // -------------------------------------------------

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
                "P:%.1fW E:%.2f",
                measurement.power,
                measurement.energy
            );
        }

        // -------------------------------------------------
        // Periodic execution
        // -------------------------------------------------

        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(DISPLAY_PERIOD_MS)
        );
    }
}

// =====================================================
//                  NETWORK TASK
// =====================================================

void networkTask(void *parameter)
{
    Serial.println("[NetworkTask] Started");

    unsigned long lastTelemetryTime = 0;

    for (;;)
    {
        // -------------------------------------------------
        // Service Blynk frequently
        // -------------------------------------------------

        Blynk.run();

        unsigned long now = millis();

        // -------------------------------------------------
        // Send telemetry every 2 seconds
        // -------------------------------------------------

        if (now - lastTelemetryTime >=
            TELEMETRY_PERIOD_MS)
        {
            lastTelemetryTime = now;

            Measurement measurement =
            {
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                false,
                0
            };

            if (xSemaphoreTake(
                    measurementMutex,
                    pdMS_TO_TICKS(50)
                ) == pdTRUE)
            {
                measurement = latestMeasurement;

                xSemaphoreGive(measurementMutex);
            }

            if (measurement.valid)
            {
                // Voltage
                Blynk.virtualWrite(
                    V0,
                    measurement.voltage
                );

                // Current
                Blynk.virtualWrite(
                    V1,
                    measurement.current
                );

                // Real Power
                Blynk.virtualWrite(
                    V2,
                    measurement.power
                );

                // Energy
                Blynk.virtualWrite(
                    V3,
                    measurement.energy
                );

                // Apparent Power
                Blynk.virtualWrite(
                    V4,
                    measurement.apparentPower
                );

                // Power Factor
                Blynk.virtualWrite(
                    V5,
                    measurement.powerFactor
                );
            }
        }

        // -------------------------------------------------
        // Blynk service interval
        // -------------------------------------------------

        vTaskDelay(
            pdMS_TO_TICKS(10)
        );
    }
}

// =====================================================
//                SYSTEM STATE LOGIC
// =====================================================

void updateSystemState(
    const Measurement &measurement)
{
    SystemState previousState = systemState;

    // -------------------------------------------------
    // Invalid sensor data
    // -------------------------------------------------

    if (!measurement.valid)
    {
        systemState = SYSTEM_FAULT;
    }

    // -------------------------------------------------
    // Very low / startup voltage
    // -------------------------------------------------

    else if (measurement.voltage <= 10.0f)
    {
        systemState = SYSTEM_INIT;
    }

    // -------------------------------------------------
    // Over-power warning
    // -------------------------------------------------

    else if (measurement.power >
             POWER_THRESHOLD_W)
    {
        systemState = SYSTEM_WARNING;
    }

    // -------------------------------------------------
    // Normal operation
    // -------------------------------------------------

    else
    {
        systemState = SYSTEM_NORMAL;
    }

    // -------------------------------------------------
    // State transition logging
    // -------------------------------------------------

    if (systemState != previousState)
    {
        Serial.print("[SYSTEM] ");

        Serial.print(
            getStateName(previousState)
        );

        Serial.print(" -> ");

        Serial.println(
            getStateName(systemState)
        );
    }
}

// =====================================================
//                 LED CONTROL
// =====================================================

void updateIndicators(SystemState state)
{
    switch (state)
    {
        case SYSTEM_NORMAL:

            digitalWrite(
                LED_NORMAL,
                HIGH
            );

            digitalWrite(
                LED_ALERT,
                LOW
            );

            break;

        case SYSTEM_WARNING:

            digitalWrite(
                LED_NORMAL,
                LOW
            );

            digitalWrite(
                LED_ALERT,
                HIGH
            );

            break;

        case SYSTEM_FAULT:

            digitalWrite(
                LED_NORMAL,
                LOW
            );

            digitalWrite(
                LED_ALERT,
                HIGH
            );

            break;

        case SYSTEM_INIT:

        default:

            digitalWrite(
                LED_NORMAL,
                LOW
            );

            digitalWrite(
                LED_ALERT,
                LOW
            );

            break;
    }
}

// =====================================================
//                  STATE NAME
// =====================================================

const char* getStateName(
    SystemState state)
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
    Serial.println(
        "======================================"
    );

    Serial.println(
        " REAL-TIME ENERGY CONTROLLER"
    );

    Serial.println(
        " ESP32 + FreeRTOS"
    );

    Serial.println(
        " STEP 2 - QUEUE DATA PIPELINE"
    );

    Serial.println(
        "======================================"
    );

    // -------------------------------------------------
    // GPIO
    // -------------------------------------------------

    pinMode(
        LED_NORMAL,
        OUTPUT
    );

    pinMode(
        LED_ALERT,
        OUTPUT
    );

    digitalWrite(
        LED_NORMAL,
        LOW
    );

    digitalWrite(
        LED_ALERT,
        LOW
    );

    // -------------------------------------------------
    // LCD
    // -------------------------------------------------

    lcd.init();

    lcd.backlight();

    lcd.setCursor(0, 0);

    lcd.print(
        "Energy Controller"
    );

    lcd.setCursor(0, 1);

    lcd.print(
        "Booting..."
    );

    // -------------------------------------------------
    // Create measurement mutex
    // -------------------------------------------------

    measurementMutex =
        xSemaphoreCreateMutex();

    if (measurementMutex == NULL)
    {
        Serial.println(
            "[FATAL] Mutex creation failed"
        );

        while (true)
        {
            delay(1000);
        }
    }

    Serial.println(
        "[SYSTEM] Measurement mutex created"
    );

    // -------------------------------------------------
    // Create measurement queue
    // -------------------------------------------------

    measurementQueue =
        xQueueCreate(
            5,
            sizeof(Measurement)
        );

    if (measurementQueue == NULL)
    {
        Serial.println(
            "[FATAL] Measurement queue creation failed"
        );

        while (true)
        {
            delay(1000);
        }
    }

    Serial.println(
        "[SYSTEM] Measurement queue created"
    );

    // -------------------------------------------------
    // Blynk
    // -------------------------------------------------

    Serial.println(
        "[SYSTEM] Connecting to Blynk..."
    );

    Blynk.begin(
        auth,
        ssid,
        pass
    );

    Serial.println(
        "[SYSTEM] Blynk connected"
    );

    // -------------------------------------------------
    // Create SensorTask
    // Core 1
    // Priority 3
    // -------------------------------------------------

    if (xTaskCreatePinnedToCore(
            sensorTask,
            "SensorTask",
            4096,
            NULL,
            3,
            &sensorTaskHandle,
            1
        ) != pdPASS)
    {
        Serial.println(
            "[FATAL] SensorTask creation failed"
        );
    }

    // -------------------------------------------------
    // Create ControlTask
    // Core 1
    // Priority 4
    // -------------------------------------------------

    if (xTaskCreatePinnedToCore(
            controlTask,
            "ControlTask",
            4096,
            NULL,
            4,
            &controlTaskHandle,
            1
        ) != pdPASS)
    {
        Serial.println(
            "[FATAL] ControlTask creation failed"
        );
    }

    // -------------------------------------------------
    // Create DisplayTask
    // Core 1
    // Priority 1
    // -------------------------------------------------

    if (xTaskCreatePinnedToCore(
            displayTask,
            "DisplayTask",
            4096,
            NULL,
            1,
            &displayTaskHandle,
            1
        ) != pdPASS)
    {
        Serial.println(
            "[FATAL] DisplayTask creation failed"
        );
    }

    // -------------------------------------------------
    // Create NetworkTask
    // Core 0
    // Priority 2
    // -------------------------------------------------

    if (xTaskCreatePinnedToCore(
            networkTask,
            "NetworkTask",
            4096,
            NULL,
            2,
            &networkTaskHandle,
            0
        ) != pdPASS)
    {
        Serial.println(
            "[FATAL] NetworkTask creation failed"
        );
    }

    Serial.println(
        "[SYSTEM] All RTOS tasks created"
    );

    Serial.println(
        "[SYSTEM] Queue pipeline active"
    );
}

// =====================================================
//                        LOOP
// =====================================================

void loop()
{
    // All application work is handled
    // by FreeRTOS tasks.

    vTaskDelay(
        pdMS_TO_TICKS(1000)
    );
}
