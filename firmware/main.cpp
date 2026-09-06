#define BLYNK_PRINT Serial

// =====================================================
//                  BLYNK CONFIGURATION
// =====================================================

#define BLYNK_TEMPLATE_ID   "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Energy Monitor"
#define BLYNK_AUTH_TOKEN    "YOUR_BLYNK_AUTH_TOKEN"

#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_task_wdt.h"
#include "esp_system.h"

// =====================================================
//                  NETWORK CREDENTIALS
// =====================================================

char auth[] = BLYNK_AUTH_TOKEN;

char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

// =====================================================
//                  FIRMWARE INFORMATION
// =====================================================

constexpr const char* FIRMWARE_NAME =
    "ESP32 Real-Time Energy Controller";

constexpr const char* FIRMWARE_VERSION =
    "1.0.0";

constexpr const char* FIRMWARE_BUILD =
    "STEP-6-UART-CLI";

// =====================================================
//                       HARDWARE
// =====================================================

constexpr uint8_t PZEM_RX_PIN = 16;
constexpr uint8_t PZEM_TX_PIN = 17;

constexpr uint8_t LED_NORMAL = 18;
constexpr uint8_t LED_ALERT  = 19;

constexpr uint8_t LCD_ADDRESS = 0x27;

PZEM004Tv30 pzem(
    Serial2,
    PZEM_RX_PIN,
    PZEM_TX_PIN
);

LiquidCrystal_I2C lcd(
    LCD_ADDRESS,
    16,
    2
);

// =====================================================
//                  SYSTEM CONFIGURATION
// =====================================================

// 0 = real PZEM measurements
// 1 = synthetic protection test
#define PROTECTION_TEST_MODE 0

// =====================================================
//                  PROTECTION CONFIG
// =====================================================

constexpr float POWER_WARNING_THRESHOLD_W = 485.0f;
constexpr float POWER_FAULT_THRESHOLD_W   = 550.0f;

constexpr uint32_t FAULT_CONFIRM_TIME_MS =
    2000;

// =====================================================
//                     TASK PERIODS
// =====================================================

constexpr uint32_t SENSOR_PERIOD_MS =
    1000;

constexpr uint32_t CONTROL_PERIOD_MS =
    100;

constexpr uint32_t DISPLAY_PERIOD_MS =
    500;

constexpr uint32_t NETWORK_PERIOD_MS =
    10;

constexpr uint32_t TELEMETRY_PERIOD_MS =
    2000;

// =====================================================
//                    QUEUE CONFIG
// =====================================================

constexpr uint8_t MEASUREMENT_QUEUE_LENGTH =
    5;

// =====================================================
//                  WATCHDOG CONFIG
// =====================================================

constexpr uint32_t WATCHDOG_TIMEOUT_MS =
    8000;

constexpr uint32_t HEARTBEAT_TIMEOUT_MS =
    6000;

constexpr uint32_t WATCHDOG_STARTUP_GRACE_MS =
    10000;

constexpr uint32_t WATCHDOG_SUPERVISOR_PERIOD_MS =
    1000;

// =====================================================
//                  NETWORK CONFIG
// =====================================================

constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS =
    10000;

constexpr uint32_t BLYNK_RECONNECT_INTERVAL_MS =
    5000;

// =====================================================
//                    CLI CONFIG
// =====================================================

constexpr uint32_t CLI_TASK_PERIOD_MS =
    20;

constexpr uint8_t CLI_BUFFER_SIZE =
    40;

constexpr uint8_t COMMAND_QUEUE_LENGTH =
    8;

// =====================================================
//                 MEASUREMENT STRUCTURE
// =====================================================

struct Measurement
{
    float voltage;
    float current;
    float power;
    float energy;

    float apparentPower;
    float powerFactor;

    bool valid;

    uint32_t timestamp;
};

// =====================================================
//              LATEST MEASUREMENT SNAPSHOT
// =====================================================

Measurement latestMeasurement =
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

// =====================================================
//                    SYSTEM STATE
// =====================================================

enum SystemState
{
    SYSTEM_INIT,
    SYSTEM_NORMAL,
    SYSTEM_WARNING,
    SYSTEM_FAULT
};

SystemState systemState =
    SYSTEM_INIT;

// =====================================================
//                    FAULT CODES
// =====================================================

enum FaultCode
{
    FAULT_NONE,
    FAULT_SENSOR_INVALID,
    FAULT_OVERPOWER
};

// =====================================================
//                  FAULT MANAGER
// =====================================================

struct FaultManager
{
    FaultCode activeFault;
    FaultCode lastFault;

    uint32_t activeSince;
    uint32_t lastFaultTime;

    uint32_t totalFaults;

    bool active;
    bool acknowledged;
};

FaultManager faultManager =
{
    FAULT_NONE,
    FAULT_NONE,
    0,
    0,
    0,
    false,
    false
};

// =====================================================
//              PROTECTION TIMER VARIABLES
// =====================================================

bool faultTimerActive =
    false;

uint32_t faultStartTime =
    0;

// =====================================================
//                    RTOS OBJECTS
// =====================================================

SemaphoreHandle_t measurementMutex =
    nullptr;

QueueHandle_t measurementQueue =
    nullptr;

QueueHandle_t commandQueue =
    nullptr;

// =====================================================
//                    TASK HANDLES
// =====================================================

TaskHandle_t sensorTaskHandle =
    nullptr;

TaskHandle_t controlTaskHandle =
    nullptr;

TaskHandle_t displayTaskHandle =
    nullptr;

TaskHandle_t networkTaskHandle =
    nullptr;

TaskHandle_t watchdogTaskHandle =
    nullptr;

TaskHandle_t cliTaskHandle =
    nullptr;

// =====================================================
//                 HEARTBEAT MONITORING
// =====================================================

enum HeartbeatId
{
    HEARTBEAT_SENSOR,
    HEARTBEAT_CONTROL,
    HEARTBEAT_DISPLAY,
    HEARTBEAT_NETWORK,
    HEARTBEAT_WATCHDOG,
    HEARTBEAT_CLI,

    HEARTBEAT_COUNT
};

volatile uint32_t heartbeatTimestamp[
    HEARTBEAT_COUNT
] =
{
    0,
    0,
    0,
    0,
    0,
    0
};

// =====================================================
//                 NETWORK STATE
// =====================================================

bool wifiConnected =
    false;

bool blynkConnected =
    false;

uint32_t lastWiFiAttempt =
    0;

uint32_t lastBlynkAttempt =
    0;

uint32_t lastTelemetryTime =
    0;

// =====================================================
//                    CLI COMMANDS
// =====================================================

enum CommandType
{
    CMD_NONE,

    CMD_HELP,
    CMD_STATUS,
    CMD_MEASURE,
    CMD_FAULT,
    CMD_WATCHDOG,
    CMD_TASKS,
    CMD_UPTIME,
    CMD_RESET,
    CMD_ACK,
    CMD_VERSION
};

struct CliCommand
{
    CommandType type;
};

// =====================================================
//               FUNCTION DECLARATIONS
// =====================================================

// -----------------------------------------------------
// Tasks
// -----------------------------------------------------

void sensorTask(void *parameter);
void controlTask(void *parameter);
void displayTask(void *parameter);
void networkTask(void *parameter);
void watchdogTask(void *parameter);
void cliTask(void *parameter);

// -----------------------------------------------------
// Watchdog
// -----------------------------------------------------

bool initializeWatchdog();

bool registerCurrentTaskWithWatchdog(
    const char *taskName
);

void watchdogHeartbeat(
    HeartbeatId id
);

uint32_t getHeartbeatAge(
    HeartbeatId id
);

// -----------------------------------------------------
// CLI
// -----------------------------------------------------

CommandType parseCommand(
    const char *command
);

void printCliPrompt();

void printCliHelp();

void processCliCommand(
    CommandType command
);

void processControlCommand(
    CommandType command
);

// -----------------------------------------------------
// Protection
// -----------------------------------------------------

void updateSystemState(
    const Measurement &measurement
);

void updateIndicators(
    SystemState state
);

// -----------------------------------------------------
// Fault manager
// -----------------------------------------------------

void raiseFault(
    FaultCode fault
);

void clearFault();

void resetFault();

void acknowledgeFault();

void printFaultStatus();

// -----------------------------------------------------
// Diagnostics
// -----------------------------------------------------

void printFaultRaised(
    FaultCode fault
);

void printFaultCleared(
    FaultCode fault
);

void printSystemStatus(
    const Measurement &measurement
);

void printWatchdogStatus();

void printTaskStatus();

void printMeasurement();

void printNetworkStatus();

void printUptime();

void printVersion();

// -----------------------------------------------------
// Helpers
// -----------------------------------------------------

const char* getStateName(
    SystemState state
);

const char* getFaultName(
    FaultCode fault
);

const char* getHeartbeatName(
    HeartbeatId id
);

// =====================================================
//                  STATE NAME
// =====================================================

const char* getStateName(
    SystemState state
)
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
//                  FAULT NAME
// =====================================================

const char* getFaultName(
    FaultCode fault
)
{
    switch (fault)
    {
        case FAULT_NONE:
            return "NONE";

        case FAULT_SENSOR_INVALID:
            return "SENSOR_INVALID";

        case FAULT_OVERPOWER:
            return "OVERPOWER";

        default:
            return "UNKNOWN";
    }
}

// =====================================================
//                HEARTBEAT NAME
// =====================================================

const char* getHeartbeatName(
    HeartbeatId id
)
{
    switch (id)
    {
        case HEARTBEAT_SENSOR:
            return "SensorTask";

        case HEARTBEAT_CONTROL:
            return "ControlTask";

        case HEARTBEAT_DISPLAY:
            return "DisplayTask";

        case HEARTBEAT_NETWORK:
            return "NetworkTask";

        case HEARTBEAT_WATCHDOG:
            return "WatchdogTask";

        case HEARTBEAT_CLI:
            return "CliTask";

        default:
            return "UnknownTask";
    }
}

// =====================================================
//                 WATCHDOG INIT
// =====================================================

bool initializeWatchdog()
{
    esp_task_wdt_config_t config =
    {
        .timeout_ms =
            WATCHDOG_TIMEOUT_MS,

        .idle_core_mask =
            (1U << portNUM_PROCESSORS) - 1U,

        .trigger_panic =
            true
    };

    esp_err_t result =
        esp_task_wdt_init(
            &config
        );

    if (result == ESP_OK)
    {
        Serial.println(
            "[WATCHDOG] TWDT initialized"
        );

        return true;
    }

    if (result == ESP_ERR_INVALID_STATE)
    {
        result =
            esp_task_wdt_reconfigure(
                &config
            );

        if (result == ESP_OK)
        {
            Serial.println(
                "[WATCHDOG] TWDT reconfigured"
            );

            return true;
        }
    }

    Serial.print(
        "[WATCHDOG] Initialization failed. Error: "
    );

    Serial.println(
        static_cast<int>(result)
    );

    return false;
}

// =====================================================
//          REGISTER CURRENT TASK WITH WATCHDOG
// =====================================================

bool registerCurrentTaskWithWatchdog(
    const char *taskName
)
{
    esp_err_t result =
        esp_task_wdt_add(
            nullptr
        );

    if (result == ESP_OK)
    {
        Serial.print(
            "[WATCHDOG] Registered: "
        );

        Serial.println(
            taskName
        );

        return true;
    }

    if (result == ESP_ERR_INVALID_ARG)
    {
        Serial.print(
            "[WATCHDOG] Already registered: "
        );

        Serial.println(
            taskName
        );

        return true;
    }

    Serial.print(
        "[WATCHDOG] Registration failed: "
    );

    Serial.println(
        taskName
    );

    return false;
}

// =====================================================
//                  HEARTBEAT UPDATE
// =====================================================

void watchdogHeartbeat(
    HeartbeatId id
)
{
    if (id >= HEARTBEAT_COUNT)
    {
        return;
    }

    heartbeatTimestamp[id] =
        millis();
}

// =====================================================
//              HEARTBEAT AGE CALCULATION
// =====================================================

uint32_t getHeartbeatAge(
    HeartbeatId id
)
{
    if (id >= HEARTBEAT_COUNT)
    {
        return UINT32_MAX;
    }

    const uint32_t now =
        millis();

    const uint32_t lastHeartbeat =
        heartbeatTimestamp[id];

    if (lastHeartbeat == 0)
    {
        return UINT32_MAX;
    }

    // Protect against an impossible/future timestamp
    // so unsigned subtraction cannot produce a huge value.
    if (lastHeartbeat > now)
    {
        return 0;
    }

    return now - lastHeartbeat;
}

// =====================================================
//                    SENSOR TASK
// =====================================================

void sensorTask(void *parameter)
{
    Serial.println(
        "[SensorTask] Started"
    );

    registerCurrentTaskWithWatchdog(
        "SensorTask"
    );

    watchdogHeartbeat(
        HEARTBEAT_SENSOR
    );

    TickType_t lastWakeTime =
        xTaskGetTickCount();

    for (;;)
    {
        Measurement reading{};

        // -------------------------------------------------
        // Read PZEM
        // -------------------------------------------------

        reading.voltage =
            pzem.voltage();

        reading.current =
            pzem.current();

        reading.power =
            pzem.power();

        reading.energy =
            pzem.energy();

        // -------------------------------------------------
        // Apparent power
        // -------------------------------------------------

        if (!isnan(reading.voltage) &&
            !isnan(reading.current) &&
            reading.voltage > 0.0f &&
            reading.current >= 0.0f)
        {
            reading.apparentPower =
                reading.voltage *
                reading.current;
        }
        else
        {
            reading.apparentPower =
                0.0f;
        }

        // -------------------------------------------------
        // Power factor
        // -------------------------------------------------

        if (!isnan(reading.power) &&
            reading.apparentPower > 0.0f)
        {
            reading.powerFactor =
                reading.power /
                reading.apparentPower;

            if (reading.powerFactor < 0.0f)
            {
                reading.powerFactor =
                    0.0f;
            }

            if (reading.powerFactor > 1.0f)
            {
                reading.powerFactor =
                    1.0f;
            }
        }
        else
        {
            reading.powerFactor =
                0.0f;
        }

        // -------------------------------------------------
        // Timestamp
        // -------------------------------------------------

        reading.timestamp =
            millis();

        // -------------------------------------------------
        // Validate measurement
        // -------------------------------------------------

        reading.valid =
            !isnan(reading.voltage) &&
            !isnan(reading.current) &&
            !isnan(reading.power) &&
            !isnan(reading.energy) &&
            !isnan(reading.apparentPower) &&
            !isnan(reading.powerFactor);

        // -------------------------------------------------
        // Send to ControlTask
        // -------------------------------------------------

        if (xQueueSend(
                measurementQueue,
                &reading,
                pdMS_TO_TICKS(50)
            ) != pdPASS)
        {
            Serial.println(
                "[SensorTask] WARNING: Queue full"
            );
        }

        // -------------------------------------------------
        // Update shared snapshot
        // -------------------------------------------------

        if (xSemaphoreTake(
                measurementMutex,
                pdMS_TO_TICKS(50)
            ) == pdTRUE)
        {
            latestMeasurement =
                reading;

            xSemaphoreGive(
                measurementMutex
            );
        }

        // -------------------------------------------------
        // Watchdog
        // -------------------------------------------------

        watchdogHeartbeat(
            HEARTBEAT_SENSOR
        );

        esp_task_wdt_reset();

        // -------------------------------------------------
        // Periodic execution
        // -------------------------------------------------

        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(
                SENSOR_PERIOD_MS
            )
        );
    }
}

// =====================================================
//                   CONTROL TASK
// =====================================================

void controlTask(void *parameter)
{
    Serial.println(
        "[ControlTask] Started"
    );

    registerCurrentTaskWithWatchdog(
        "ControlTask"
    );

    watchdogHeartbeat(
        HEARTBEAT_CONTROL
    );

#if PROTECTION_TEST_MODE

    const uint32_t testStartTime =
        millis();

    for (;;)
    {
        Measurement measurement{};

        const uint32_t elapsed =
            millis() -
            testStartTime;

        if (elapsed < 5000)
        {
            measurement.voltage =
                230.0f;

            measurement.current =
                0.20f;

            measurement.power =
                40.0f;

            measurement.energy =
                0.0f;

            measurement.apparentPower =
                46.0f;

            measurement.powerFactor =
                0.87f;

            measurement.valid =
                true;
        }
        else if (elapsed < 10000)
        {
            measurement.voltage =
                230.0f;

            measurement.current =
                2.17f;

            measurement.power =
                500.0f;

            measurement.energy =
                0.0f;

            measurement.apparentPower =
                499.0f;

            measurement.powerFactor =
                0.96f;

            measurement.valid =
                true;
        }
        else
        {
            measurement.voltage =
                230.0f;

            measurement.current =
                2.60f;

            measurement.power =
                600.0f;

            measurement.energy =
                0.0f;

            measurement.apparentPower =
                650.0f;

            measurement.powerFactor =
                0.92f;

            measurement.valid =
                true;
        }

        measurement.timestamp =
            millis();

        if (xSemaphoreTake(
                measurementMutex,
                pdMS_TO_TICKS(10)
            ) == pdTRUE)
        {
            latestMeasurement =
                measurement;

            xSemaphoreGive(
                measurementMutex
            );
        }

        updateSystemState(
            measurement
        );

        // Process queued CLI commands
        CliCommand command{};

        while (xQueueReceive(
                   commandQueue,
                   &command,
                   0
               ) == pdPASS)
        {
            processControlCommand(
                command.type
            );
        }

        watchdogHeartbeat(
            HEARTBEAT_CONTROL
        );

        esp_task_wdt_reset();

        vTaskDelay(
            pdMS_TO_TICKS(
                CONTROL_PERIOD_MS
            )
        );
    }

#else

    for (;;)
    {
        Measurement newMeasurement{};

        if (xQueueReceive(
                measurementQueue,
                &newMeasurement,
                pdMS_TO_TICKS(
                    CONTROL_PERIOD_MS
                )
            ) == pdPASS)
        {
            updateSystemState(
                newMeasurement
            );
        }

        // -------------------------------------------------
        // Process CLI commands
        // -------------------------------------------------

        CliCommand command{};

        while (xQueueReceive(
                   commandQueue,
                   &command,
                   0
               ) == pdPASS)
        {
            processControlCommand(
                command.type
            );
        }

        watchdogHeartbeat(
            HEARTBEAT_CONTROL
        );

        esp_task_wdt_reset();
    }

#endif
}

// =====================================================
//                   DISPLAY TASK
// =====================================================

void displayTask(void *parameter)
{
    Serial.println(
        "[DisplayTask] Started"
    );

    registerCurrentTaskWithWatchdog(
        "DisplayTask"
    );

    watchdogHeartbeat(
        HEARTBEAT_DISPLAY
    );

    TickType_t lastWakeTime =
        xTaskGetTickCount();

    for (;;)
    {
        Measurement measurement{};

        if (xSemaphoreTake(
                measurementMutex,
                pdMS_TO_TICKS(50)
            ) == pdTRUE)
        {
            measurement =
                latestMeasurement;

            xSemaphoreGive(
                measurementMutex
            );
        }

        // -------------------------------------------------
        // LCD update
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

        watchdogHeartbeat(
            HEARTBEAT_DISPLAY
        );

        esp_task_wdt_reset();

        vTaskDelayUntil(
            &lastWakeTime,
            pdMS_TO_TICKS(
                DISPLAY_PERIOD_MS
            )
        );
    }
}

// =====================================================
//                  NETWORK TASK
// =====================================================

void networkTask(void *parameter)
{
    Serial.println(
        "[NetworkTask] Started"
    );

    registerCurrentTaskWithWatchdog(
        "NetworkTask"
    );

    watchdogHeartbeat(
        HEARTBEAT_NETWORK
    );

    for (;;)
    {
        const uint32_t now =
            millis();

        // =================================================
        // WIFI
        // =================================================

        if (WiFi.status() != WL_CONNECTED)
        {
            if (wifiConnected)
            {
                wifiConnected =
                    false;

                blynkConnected =
                    false;

                Serial.println(
                    "[NETWORK] WiFi disconnected"
                );
            }

            if (now - lastWiFiAttempt >=
                WIFI_RECONNECT_INTERVAL_MS)
            {
                lastWiFiAttempt =
                    now;

                Serial.println(
                    "[NETWORK] Attempting WiFi reconnect"
                );

                WiFi.begin(
                    ssid,
                    pass
                );
            }
        }
        else
        {
            if (!wifiConnected)
            {
                wifiConnected =
                    true;

                Serial.print(
                    "[NETWORK] WiFi connected. IP: "
                );

                Serial.println(
                    WiFi.localIP()
                );
            }

            // =================================================
            // BLYNK
            // =================================================

            if (!Blynk.connected())
            {
                blynkConnected =
                    false;

                if (now - lastBlynkAttempt >=
                    BLYNK_RECONNECT_INTERVAL_MS)
                {
                    lastBlynkAttempt =
                        now;

                    Serial.println(
                        "[NETWORK] Attempting Blynk connection"
                    );

                    if (Blynk.connect(1000))
                    {
                        blynkConnected =
                            true;

                        Serial.println(
                            "[NETWORK] Blynk connected"
                        );
                    }
                }
            }
            else
            {
                if (!blynkConnected)
                {
                    blynkConnected =
                        true;

                    Serial.println(
                        "[NETWORK] Blynk connection restored"
                    );
                }

                Blynk.run();

                // -------------------------------------------------
                // Telemetry
                // -------------------------------------------------

                if (now - lastTelemetryTime >=
                    TELEMETRY_PERIOD_MS)
                {
                    lastTelemetryTime =
                        now;

                    Measurement measurement{};

                    if (xSemaphoreTake(
                            measurementMutex,
                            pdMS_TO_TICKS(50)
                        ) == pdTRUE)
                    {
                        measurement =
                            latestMeasurement;

                        xSemaphoreGive(
                            measurementMutex
                        );
                    }

                    if (measurement.valid)
                    {
                        Blynk.virtualWrite(
                            V0,
                            measurement.voltage
                        );

                        Blynk.virtualWrite(
                            V1,
                            measurement.current
                        );

                        Blynk.virtualWrite(
                            V2,
                            measurement.power
                        );

                        Blynk.virtualWrite(
                            V3,
                            measurement.energy
                        );

                        Blynk.virtualWrite(
                            V4,
                            measurement.apparentPower
                        );

                        Blynk.virtualWrite(
                            V5,
                            measurement.powerFactor
                        );
                    }
                }
            }
        }

        watchdogHeartbeat(
            HEARTBEAT_NETWORK
        );

        esp_task_wdt_reset();

        vTaskDelay(
            pdMS_TO_TICKS(
                NETWORK_PERIOD_MS
            )
        );
    }
}

// =====================================================
//                 WATCHDOG SUPERVISOR
// =====================================================

void watchdogTask(void *parameter)
{
    Serial.println(
        "[WatchdogTask] Started"
    );

    registerCurrentTaskWithWatchdog(
        "WatchdogTask"
    );

    const uint32_t supervisorStartTime =
        millis();

    watchdogHeartbeat(
        HEARTBEAT_WATCHDOG
    );

    for (;;)
    {
        const uint32_t now =
            millis();

        watchdogHeartbeat(
            HEARTBEAT_WATCHDOG
        );

        esp_task_wdt_reset();

        // -------------------------------------------------
        // Startup grace period
        // -------------------------------------------------

        if (now - supervisorStartTime <
            WATCHDOG_STARTUP_GRACE_MS)
        {
            vTaskDelay(
                pdMS_TO_TICKS(
                    WATCHDOG_SUPERVISOR_PERIOD_MS
                )
            );

            continue;
        }

        // -------------------------------------------------
        // Heartbeat supervision
        // -------------------------------------------------

        bool systemHealthy =
            true;

        for (uint8_t i = 0;
             i < HEARTBEAT_COUNT;
             i++)
        {
            const uint32_t lastHeartbeat =
                heartbeatTimestamp[i];

            if (lastHeartbeat == 0)
            {
                Serial.print(
                    "[WATCHDOG] Missing heartbeat: "
                );

                Serial.println(
                    getHeartbeatName(
                        static_cast<HeartbeatId>(i)
                    )
                );

                systemHealthy =
                    false;

                continue;
            }

            const uint32_t heartbeatAge =
                getHeartbeatAge(
                    static_cast<HeartbeatId>(i)
                );

            if (heartbeatAge >
                HEARTBEAT_TIMEOUT_MS)
            {
                Serial.print(
                    "[WATCHDOG] STALE TASK: "
                );

                Serial.print(
                    getHeartbeatName(
                        static_cast<HeartbeatId>(i)
                    )
                );

                Serial.print(
                    " | Age = "
                );

                Serial.print(
                    heartbeatAge
                );

                Serial.println(
                    " ms"
                );

                systemHealthy =
                    false;
            }
        }

        // -------------------------------------------------
        // Recovery
        // -------------------------------------------------

        if (!systemHealthy)
        {
            Serial.println();
            Serial.println(
                "================================"
            );

            Serial.println(
                "       WATCHDOG FAILURE"
            );

            Serial.println(
                "================================"
            );

            printWatchdogStatus();

            Serial.println(
                "[WATCHDOG] Restarting system..."
            );

            delay(100);

            esp_restart();
        }

        vTaskDelay(
            pdMS_TO_TICKS(
                WATCHDOG_SUPERVISOR_PERIOD_MS
            )
        );
    }
}

// =====================================================
//                     CLI TASK
// =====================================================
void printCliPrompt()
{
    Serial.print("\r\ndiag> ");
}

void cliTask(void *parameter)
{
    Serial.println(
        "[CliTask] Started"
    );

    registerCurrentTaskWithWatchdog(
        "CliTask"
    );

    watchdogHeartbeat(
        HEARTBEAT_CLI
    );

    char commandBuffer[
        CLI_BUFFER_SIZE
    ];

    uint8_t bufferIndex =
        0;

    bool commandOverflow =
        false;

    printCliHelp();

    printCliPrompt();

    for (;;)
    {
        while (Serial.available() > 0)
        {
            const char c =
                static_cast<char>(
                    Serial.read()
                );

            // -------------------------------------------------
            // End of command
            // -------------------------------------------------

            if (c == '\r' ||
                c == '\n')
            {
                if (bufferIndex == 0 &&
                    !commandOverflow)
                {
                    continue;
                }

                Serial.println();

                if (commandOverflow)
                {
                    Serial.println(
                        "[CLI] ERROR: Command too long"
                    );
                }
                else
                {
                    commandBuffer[
                        bufferIndex
                    ] = '\0';

                    CommandType command =
                        parseCommand(
                            commandBuffer
                        );

                    if (command == CMD_NONE)
                    {
                        Serial.print(
                            "[CLI] Unknown command: "
                        );

                        Serial.println(
                            commandBuffer
                        );

                        Serial.println(
                            "[CLI] Type 'help' for commands"
                        );
                    }
                    else
                    {
                        processCliCommand(
                            command
                        );
                    }
                }

                bufferIndex =
                    0;

                commandOverflow =
                    false;

                printCliPrompt();

                continue;
            }

            // -------------------------------------------------
            // Backspace
            // -------------------------------------------------

            if (c == '\b' ||
                c == 127)
            {
                if (bufferIndex > 0)
                {
                    bufferIndex--;

                    Serial.print(
                        "\b \b"
                    );
                }

                continue;
            }

            // -------------------------------------------------
            // Ignore non-printable characters
            // -------------------------------------------------

            if (c < 32 ||
                c > 126)
            {
                continue;
            }

            // -------------------------------------------------
            // Buffer overflow protection
            // -------------------------------------------------

            if (bufferIndex <
                CLI_BUFFER_SIZE - 1)
            {
                commandBuffer[
                    bufferIndex++
                ] = c;

                Serial.print(c);
            }
            else
            {
                commandOverflow =
                    true;
            }
        }

        watchdogHeartbeat(
            HEARTBEAT_CLI
        );

        esp_task_wdt_reset();

        vTaskDelay(
            pdMS_TO_TICKS(
                CLI_TASK_PERIOD_MS
            )
        );
    }
}

// =====================================================
//                 COMMAND PARSER
// =====================================================

CommandType parseCommand(
    const char *command
)
{
    if (strcmp(command, "help") == 0)
        return CMD_HELP;

    if (strcmp(command, "status") == 0)
        return CMD_STATUS;

    if (strcmp(command, "measure") == 0)
        return CMD_MEASURE;

    if (strcmp(command, "fault") == 0)
        return CMD_FAULT;

    if (strcmp(command, "watchdog") == 0)
        return CMD_WATCHDOG;

    if (strcmp(command, "tasks") == 0)
        return CMD_TASKS;

    if (strcmp(command, "uptime") == 0)
        return CMD_UPTIME;

    if (strcmp(command, "reset") == 0)
        return CMD_RESET;

    if (strcmp(command, "ack") == 0)
        return CMD_ACK;

    if (strcmp(command, "version") == 0)
        return CMD_VERSION;

    return CMD_NONE;
}

// =====================================================
//                    CLI HELP
// =====================================================

void printCliHelp()
{
    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "     ESP32 ENERGY CONTROLLER CLI"
    );

    Serial.println(
        "========================================"
    );

    Serial.println(
        "help       - Show available commands"
    );

    Serial.println(
        "status     - Complete system status"
    );

    Serial.println(
        "measure    - Latest energy measurement"
    );

    Serial.println(
        "fault      - Fault manager status"
    );

    Serial.println(
        "watchdog   - Watchdog/heartbeat status"
    );

    Serial.println(
        "tasks      - RTOS task status"
    );

    Serial.println(
        "uptime     - System uptime"
    );

    Serial.println(
        "reset      - Reset active fault"
    );

    Serial.println(
        "ack        - Acknowledge active fault"
    );

    Serial.println(
        "version    - Firmware information"
    );

    Serial.println(
        "========================================"
    );
}

// =====================================================
//                  CLI PROCESSOR
// =====================================================

void processCliCommand(
    CommandType command
)
{
    switch (command)
    {
        case CMD_HELP:

            printCliHelp();

            break;

        case CMD_STATUS:

            // Status is requested from CLI but
            // executed in ControlTask to maintain
            // protection-state ownership.
            {
                CliCommand request{
                    CMD_STATUS
                };

                if (xQueueSend(
                        commandQueue,
                        &request,
                        pdMS_TO_TICKS(50)
                    ) != pdPASS)
                {
                    Serial.println(
                        "[CLI] Command queue full"
                    );
                }
            }

            break;

        case CMD_MEASURE:

            printMeasurement();

            break;

        case CMD_FAULT:

            {
                CliCommand request{
                    CMD_FAULT
                };

                if (xQueueSend(
                        commandQueue,
                        &request,
                        pdMS_TO_TICKS(50)
                    ) != pdPASS)
                {
                    Serial.println(
                        "[CLI] Command queue full"
                    );
                }
            }

            break;

        case CMD_WATCHDOG:

            printWatchdogStatus();

            break;

        case CMD_TASKS:

            printTaskStatus();

            break;

        case CMD_UPTIME:

            printUptime();

            break;

        case CMD_RESET:

            {
                CliCommand request{
                    CMD_RESET
                };

                if (xQueueSend(
                        commandQueue,
                        &request,
                        pdMS_TO_TICKS(50)
                    ) != pdPASS)
                {
                    Serial.println(
                        "[CLI] Command queue full"
                    );
                }
            }

            break;

        case CMD_ACK:

            {
                CliCommand request{
                    CMD_ACK
                };

                if (xQueueSend(
                        commandQueue,
                        &request,
                        pdMS_TO_TICKS(50)
                    ) != pdPASS)
                {
                    Serial.println(
                        "[CLI] Command queue full"
                    );
                }
            }

            break;

        case CMD_VERSION:

            printVersion();

            break;

        default:

            Serial.println(
                "[CLI] Invalid command"
            );

            break;
    }
}

// =====================================================
//              CONTROL COMMAND PROCESSOR
// =====================================================

void processControlCommand(
    CommandType command
)
{
    switch (command)
    {
        case CMD_STATUS:
        {
            Measurement measurement{};

            if (xSemaphoreTake(
                    measurementMutex,
                    pdMS_TO_TICKS(50)
                ) == pdTRUE)
            {
                measurement =
                    latestMeasurement;

                xSemaphoreGive(
                    measurementMutex
                );
            }

            printSystemStatus(
                measurement
            );

            printNetworkStatus();

            break;
        }

        case CMD_FAULT:

            printFaultStatus();

            break;

        case CMD_RESET:

            resetFault();

            break;

        case CMD_ACK:

            acknowledgeFault();

            break;

        default:

            break;
    }
}

// =====================================================
//                  MEASUREMENT CLI
// =====================================================

void printMeasurement()
{
    Measurement measurement{};

    if (xSemaphoreTake(
            measurementMutex,
            pdMS_TO_TICKS(50)
        ) == pdTRUE)
    {
        measurement =
            latestMeasurement;

        xSemaphoreGive(
            measurementMutex
        );
    }

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "        LATEST MEASUREMENT"
    );

    Serial.println(
        "========================================"
    );

    Serial.print(
        "Validity         : "
    );

    Serial.println(
        measurement.valid
            ? "VALID"
            : "INVALID"
    );

    Serial.print(
        "Voltage          : "
    );

    Serial.print(
        measurement.voltage,
        2
    );

    Serial.println(
        " V"
    );

    Serial.print(
        "Current          : "
    );

    Serial.print(
        measurement.current,
        3
    );

    Serial.println(
        " A"
    );

    Serial.print(
        "Power            : "
    );

    Serial.print(
        measurement.power,
        2
    );

    Serial.println(
        " W"
    );

    Serial.print(
        "Energy           : "
    );

    Serial.print(
        measurement.energy,
        3
    );

    Serial.println(
        " kWh"
    );

    Serial.print(
        "Apparent Power   : "
    );

    Serial.print(
        measurement.apparentPower,
        2
    );

    Serial.println(
        " VA"
    );

    Serial.print(
        "Power Factor     : "
    );

    Serial.println(
        measurement.powerFactor,
        3
    );

    Serial.print(
        "Timestamp        : "
    );

    Serial.print(
        measurement.timestamp
    );

    Serial.println(
        " ms"
    );

    Serial.print(
        "Measurement Age  : "
    );

    Serial.print(
        millis() -
        measurement.timestamp
    );

    Serial.println(
        " ms"
    );

    Serial.println(
        "========================================"
    );
}

// =====================================================
//                  NETWORK STATUS
// =====================================================

void printNetworkStatus()
{
    Serial.println();
    Serial.println(
        "=========== NETWORK STATUS ============="
    );

    Serial.print(
        "WiFi             : "
    );

    Serial.println(
        wifiConnected
            ? "CONNECTED"
            : "DISCONNECTED"
    );

    if (wifiConnected)
    {
        Serial.print(
            "IP Address       : "
        );

        Serial.println(
            WiFi.localIP()
        );

        Serial.print(
            "RSSI             : "
        );

        Serial.print(
            WiFi.RSSI()
        );

        Serial.println(
            " dBm"
        );
    }

    Serial.print(
        "Blynk            : "
    );

    Serial.println(
        blynkConnected
            ? "CONNECTED"
            : "DISCONNECTED"
    );

    Serial.println(
        "========================================"
    );
}

// =====================================================
//                    UPTIME
// =====================================================

void printUptime()
{
    const uint32_t uptimeSeconds =
        millis() / 1000UL;

    const uint32_t days =
        uptimeSeconds / 86400UL;

    const uint32_t hours =
        (uptimeSeconds % 86400UL) / 3600UL;

    const uint32_t minutes =
        (uptimeSeconds % 3600UL) / 60UL;

    const uint32_t seconds =
        uptimeSeconds % 60UL;

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "             SYSTEM UPTIME"
    );

    Serial.println(
        "========================================"
    );

    Serial.print(
        "Uptime           : "
    );

    Serial.print(
        days
    );

    Serial.print(
        "d "
    );

    Serial.print(
        hours
    );

    Serial.print(
        "h "
    );

    Serial.print(
        minutes
    );

    Serial.print(
        "m "
    );

    Serial.print(
        seconds
    );

    Serial.println(
        "s"
    );

    Serial.print(
        "Milliseconds      : "
    );

    Serial.println(
        millis()
    );

    Serial.println(
        "========================================"
    );
}

// =====================================================
//                  FIRMWARE VERSION
// =====================================================

void printVersion()
{
    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "          FIRMWARE INFORMATION"
    );

    Serial.println(
        "========================================"
    );

    Serial.print(
        "Firmware         : "
    );

    Serial.println(
        FIRMWARE_NAME
    );

    Serial.print(
        "Version          : "
    );

    Serial.println(
        FIRMWARE_VERSION
    );

    Serial.print(
        "Build            : "
    );

    Serial.println(
        FIRMWARE_BUILD
    );

    Serial.print(
        "CPU Frequency    : "
    );

    Serial.print(
        getCpuFrequencyMhz()
    );

    Serial.println(
        " MHz"
    );

    Serial.print(
        "Free Heap        : "
    );

    Serial.print(
        ESP.getFreeHeap()
    );

    Serial.println(
        " bytes"
    );

    Serial.print(
        "Minimum Free Heap: "
    );

    Serial.print(
        ESP.getMinFreeHeap()
    );

    Serial.println(
        " bytes"
    );

    Serial.println(
        "========================================"
    );
}

// =====================================================
//                    TASK STATUS
// =====================================================

void printTaskStatus()
{
    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "             RTOS TASK STATUS"
    );

    Serial.println(
        "========================================"
    );

    TaskHandle_t handles[] =
    {
        sensorTaskHandle,
        controlTaskHandle,
        displayTaskHandle,
        networkTaskHandle,
        watchdogTaskHandle,
        cliTaskHandle
    };

    const char* names[] =
    {
        "SensorTask",
        "ControlTask",
        "DisplayTask",
        "NetworkTask",
        "WatchdogTask",
        "CliTask"
    };

    for (uint8_t i = 0;
         i < 6;
         i++)
    {
        if (handles[i] == nullptr)
        {
            Serial.print(
                names[i]
            );

            Serial.println(
                " : NOT CREATED"
            );

            continue;
        }

        Serial.print(
            names[i]
        );

        Serial.print(
            " | State="
        );

        switch (eTaskGetState(handles[i]))
        {
            case eRunning:
                Serial.print("RUNNING");
                break;

            case eReady:
                Serial.print("READY");
                break;

            case eBlocked:
                Serial.print("BLOCKED");
                break;

            case eSuspended:
                Serial.print("SUSPENDED");
                break;

            case eDeleted:
                Serial.print("DELETED");
                break;

            default:
                Serial.print("UNKNOWN");
                break;
        }

        Serial.print(
            " | Priority="
        );

        Serial.print(
            uxTaskPriorityGet(
                handles[i]
            )
        );

        Serial.print(
            " | StackFree="
        );

        Serial.print(
            uxTaskGetStackHighWaterMark(
                handles[i]
            )
        );

        Serial.println();
    }

    Serial.println(
        "========================================"
    );
}

// =====================================================
//              SYSTEM STATE DIAGNOSTIC
// =====================================================

void printSystemStatus(
    const Measurement &measurement
)
{
    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "             SYSTEM STATUS"
    );

    Serial.println(
        "========================================"
    );

    Serial.print(
        "System State     : "
    );

    Serial.println(
        getStateName(
            systemState
        )
    );

    Serial.print(
        "Active Fault     : "
    );

    Serial.println(
        getFaultName(
            faultManager.activeFault
        )
    );

    Serial.print(
        "Last Fault       : "
    );

    Serial.println(
        getFaultName(
            faultManager.lastFault
        )
    );

    Serial.print(
        "Fault Count      : "
    );

    Serial.println(
        faultManager.totalFaults
    );

    Serial.print(
        "Measurement      : "
    );

    Serial.println(
        measurement.valid
            ? "VALID"
            : "INVALID"
    );

    Serial.print(
        "Power            : "
    );

    Serial.print(
        measurement.power,
        2
    );

    Serial.println(
        " W"
    );

    Serial.print(
        "Measurement Age  : "
    );

    Serial.print(
        millis() -
        measurement.timestamp
    );

    Serial.println(
        " ms"
    );

    Serial.println(
        "========================================"
    );
}

// =====================================================
//              WATCHDOG STATUS
// =====================================================

void printWatchdogStatus()
{
    const uint32_t now =
        millis();

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "            WATCHDOG STATUS"
    );

    Serial.println(
        "========================================"
    );

    Serial.print(
        "TWDT Timeout     : "
    );

    Serial.print(
        WATCHDOG_TIMEOUT_MS
    );

    Serial.println(
        " ms"
    );

    Serial.print(
        "Heartbeat Limit  : "
    );

    Serial.print(
        HEARTBEAT_TIMEOUT_MS
    );

    Serial.println(
        " ms"
    );

    for (uint8_t i = 0;
         i < HEARTBEAT_COUNT;
         i++)
    {
        Serial.print(
            getHeartbeatName(
                static_cast<HeartbeatId>(i)
            )
        );

        Serial.print(
            " : "
        );

        if (heartbeatTimestamp[i] == 0)
        {
            Serial.println(
                "NO HEARTBEAT"
            );
        }
        else
        {
            const uint32_t heartbeatAge =
                getHeartbeatAge(
                    static_cast<HeartbeatId>(i)
                );

            if (heartbeatAge == UINT32_MAX)
            {
                Serial.println(
                    "NO HEARTBEAT"
                );
            }
            else
            {
                Serial.print(
                    heartbeatAge
                );

                Serial.println(
                    " ms ago"
                );
            }
        }
    }

    Serial.println(
        "========================================"
    );
}

// =====================================================
//                  SYSTEM STATE LOGIC
// =====================================================

void updateSystemState(
    const Measurement &measurement
)
{
    const SystemState previousState =
        systemState;

    const FaultCode previousFault =
        faultManager.activeFault;

    // -------------------------------------------------
    // Latched fault
    // -------------------------------------------------

    if (systemState == SYSTEM_FAULT)
    {
        return;
    }

    // -------------------------------------------------
    // Invalid sensor
    // -------------------------------------------------

    if (!measurement.valid)
    {
        faultTimerActive =
            false;

        raiseFault(
            FAULT_SENSOR_INVALID
        );
    }

    // -------------------------------------------------
    // Initialization
    // -------------------------------------------------

    else if (measurement.voltage <= 10.0f)
    {
        faultTimerActive =
            false;

        systemState =
            SYSTEM_INIT;
    }

    // -------------------------------------------------
    // Severe overpower
    // -------------------------------------------------

    else if (measurement.power >
             POWER_FAULT_THRESHOLD_W)
    {
        if (!faultTimerActive)
        {
            faultTimerActive =
                true;

            faultStartTime =
                millis();

            Serial.println(
                "[PROTECTION] Fault timer started"
            );
        }

        if (millis() -
            faultStartTime >=
            FAULT_CONFIRM_TIME_MS)
        {
            faultTimerActive =
                false;

            raiseFault(
                FAULT_OVERPOWER
            );
        }
        else
        {
            systemState =
                SYSTEM_WARNING;
        }
    }

    // -------------------------------------------------
    // Warning
    // -------------------------------------------------

    else if (measurement.power >
             POWER_WARNING_THRESHOLD_W)
    {
        faultTimerActive =
            false;

        systemState =
            SYSTEM_WARNING;
    }

    // -------------------------------------------------
    // Normal
    // -------------------------------------------------

    else
    {
        faultTimerActive =
            false;

        systemState =
            SYSTEM_NORMAL;
    }

    // -------------------------------------------------
    // State transition
    // -------------------------------------------------

    if (systemState != previousState)
    {
        Serial.print(
            "[SYSTEM] "
        );

        Serial.print(
            getStateName(
                previousState
            )
        );

        Serial.print(
            " -> "
        );

        Serial.println(
            getStateName(
                systemState
            )
        );

        updateIndicators(
            systemState
        );
    }

    // -------------------------------------------------
    // Fault transition
    // -------------------------------------------------

    if (faultManager.activeFault !=
        previousFault)
    {
        if (faultManager.activeFault !=
            FAULT_NONE)
        {
            printFaultRaised(
                faultManager.activeFault
            );

            printSystemStatus(
                measurement
            );
        }
        else
        {
            printFaultCleared(
                previousFault
            );
        }
    }
}

// =====================================================
//                    RAISE FAULT
// =====================================================

void raiseFault(
    FaultCode fault
)
{
    if (fault == FAULT_NONE)
    {
        return;
    }

    if (faultManager.active &&
        faultManager.activeFault == fault)
    {
        return;
    }

    const uint32_t now =
        millis();

    faultManager.activeFault =
        fault;

    faultManager.lastFault =
        fault;

    faultManager.activeSince =
        now;

    faultManager.lastFaultTime =
        now;

    faultManager.totalFaults++;

    faultManager.active =
        true;

    faultManager.acknowledged =
        false;

    systemState =
        SYSTEM_FAULT;
}

// =====================================================
//                    CLEAR FAULT
// =====================================================

void clearFault()
{
    if (!faultManager.active)
    {
        return;
    }

    const FaultCode clearedFault =
        faultManager.activeFault;

    faultManager.activeFault =
        FAULT_NONE;

    faultManager.active =
        false;

    faultManager.acknowledged =
        false;

    faultManager.activeSince =
        0;

    faultTimerActive =
        false;

    faultStartTime =
        0;

    systemState =
        SYSTEM_INIT;

    updateIndicators(
        SYSTEM_INIT
    );

    printFaultCleared(
        clearedFault
    );
}

// =====================================================
//                  RESET FAULT
// =====================================================

void resetFault()
{
    if (!faultManager.active)
    {
        Serial.println(
            "[PROTECTION] No active fault"
        );

        return;
    }

    Serial.println(
        "[PROTECTION] Fault reset requested"
    );

    clearFault();

    Serial.println(
        "[PROTECTION] Fault reset complete"
    );

    printFaultStatus();
}

// =====================================================
//                ACKNOWLEDGE FAULT
// =====================================================

void acknowledgeFault()
{
    if (!faultManager.active)
    {
        Serial.println(
            "[FAULT] No active fault"
        );

        return;
    }

    faultManager.acknowledged =
        true;

    Serial.println(
        "[FAULT] Fault acknowledged"
    );
}

// =====================================================
//                 FAULT STATUS
// =====================================================

void printFaultStatus()
{
    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "             FAULT STATUS"
    );

    Serial.println(
        "========================================"
    );

    Serial.print(
        "Active Fault     : "
    );

    Serial.println(
        getFaultName(
            faultManager.activeFault
        )
    );

    Serial.print(
        "Last Fault       : "
    );

    Serial.println(
        getFaultName(
            faultManager.lastFault
        )
    );

    Serial.print(
        "Active           : "
    );

    Serial.println(
        faultManager.active
            ? "YES"
            : "NO"
    );

    Serial.print(
        "Acknowledged     : "
    );

    Serial.println(
        faultManager.acknowledged
            ? "YES"
            : "NO"
    );

    Serial.print(
        "Total Faults     : "
    );

    Serial.println(
        faultManager.totalFaults
    );

    Serial.print(
        "Last Fault Time  : "
    );

    Serial.print(
        faultManager.lastFaultTime
    );

    Serial.println(
        " ms"
    );

    if (faultManager.active)
    {
        Serial.print(
            "Active Since     : "
        );

        Serial.print(
            faultManager.activeSince
        );

        Serial.println(
            " ms"
        );

        Serial.print(
            "Active Duration  : "
        );

        Serial.print(
            millis() -
            faultManager.activeSince
        );

        Serial.println(
            " ms"
        );
    }

    Serial.println(
        "========================================"
    );
}

// =====================================================
//             FAULT RAISED DIAGNOSTIC
// =====================================================

void printFaultRaised(
    FaultCode fault
)
{
    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "             FAULT DETECTED"
    );

    Serial.println(
        "========================================"
    );

    Serial.print(
        "[FAULT] Code       : "
    );

    Serial.println(
        getFaultName(
            fault
        )
    );

    Serial.print(
        "[FAULT] Count      : "
    );

    Serial.println(
        faultManager.totalFaults
    );

    Serial.print(
        "[FAULT] Timestamp  : "
    );

    Serial.print(
        faultManager.activeSince
    );

    Serial.println(
        " ms"
    );

    Serial.println(
        "[FAULT] State      : LATCHED"
    );

    Serial.println(
        "========================================"
    );
}

// =====================================================
//             FAULT CLEARED DIAGNOSTIC
// =====================================================

void printFaultCleared(
    FaultCode fault
)
{
    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.print(
        "[FAULT] CLEARED    : "
    );

    Serial.println(
        getFaultName(
            fault
        )
    );

    Serial.print(
        "[FAULT] Total      : "
    );

    Serial.println(
        faultManager.totalFaults
    );

    Serial.println(
        "========================================"
    );
}

// =====================================================
//                 INDICATOR CONTROL
// =====================================================

void updateIndicators(
    SystemState state
)
{
    switch (state)
    {
        case SYSTEM_INIT:

            digitalWrite(
                LED_NORMAL,
                LOW
            );

            digitalWrite(
                LED_ALERT,
                LOW
            );

            break;

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
//                       SETUP
// =====================================================

void setup()
{
    Serial.begin(
        115200
    );

    Serial2.begin(
        9600
    );

    delay(500);

    Serial.println();
    Serial.println(
        "=========================================="
    );

    Serial.println(
        " ESP32 REAL-TIME ENERGY CONTROLLER"
    );

    Serial.println(
        " ESP32 + FreeRTOS"
    );

    Serial.println(
        " STEP 6 - UART DIAGNOSTICS / CLI"
    );

    Serial.println(
        "=========================================="
    );

    // =================================================
    // GPIO
    // =================================================

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

    // =================================================
    // LCD
    // =================================================

    lcd.init();

    lcd.backlight();

    lcd.setCursor(
        0,
        0
    );

    lcd.print(
        "Energy Controller"
    );

    lcd.setCursor(
        0,
        1
    );

    lcd.print(
        "Booting..."
    );

    // =================================================
    // MEASUREMENT MUTEX
    // =================================================

    measurementMutex =
        xSemaphoreCreateMutex();

    if (measurementMutex == nullptr)
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

    // =================================================
    // MEASUREMENT QUEUE
    // =================================================

    measurementQueue =
        xQueueCreate(
            MEASUREMENT_QUEUE_LENGTH,
            sizeof(Measurement)
        );

    if (measurementQueue == nullptr)
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

    // =================================================
    // CLI COMMAND QUEUE
    // =================================================

    commandQueue =
        xQueueCreate(
            COMMAND_QUEUE_LENGTH,
            sizeof(CliCommand)
        );

    if (commandQueue == nullptr)
    {
        Serial.println(
            "[FATAL] Command queue creation failed"
        );

        while (true)
        {
            delay(1000);
        }
    }

    Serial.println(
        "[SYSTEM] CLI command queue created"
    );

    // =================================================
    // WATCHDOG
    // =================================================

    if (!initializeWatchdog())
    {
        Serial.println(
            "[FATAL] Watchdog initialization failed"
        );

        while (true)
        {
            delay(1000);
        }
    }

    // =================================================
    // WIFI
    // =================================================

    Serial.println(
        "[SYSTEM] Starting WiFi..."
    );

    WiFi.mode(
        WIFI_STA
    );

    WiFi.setAutoReconnect(
        true
    );

    WiFi.persistent(
        false
    );

    WiFi.begin(
        ssid,
        pass
    );

    // =================================================
    // BLYNK
    // =================================================

    Blynk.config(
        auth
    );

    Serial.println(
        "[SYSTEM] Blynk configured"
    );

    // =================================================
    // SENSOR TASK
    // Core 1 / Priority 3
    // =================================================

    BaseType_t result =
        xTaskCreatePinnedToCore(
            sensorTask,
            "SensorTask",
            4096,
            nullptr,
            3,
            &sensorTaskHandle,
            1
        );

    if (result == pdPASS)
    {
        Serial.println(
            "[SYSTEM] SensorTask created"
        );
    }
    else
    {
        Serial.println(
            "[FATAL] SensorTask creation failed"
        );
    }

    // =================================================
    // CONTROL TASK
    // Core 1 / Priority 4
    // =================================================

    result =
        xTaskCreatePinnedToCore(
            controlTask,
            "ControlTask",
            4096,
            nullptr,
            4,
            &controlTaskHandle,
            1
        );

    if (result == pdPASS)
    {
        Serial.println(
            "[SYSTEM] ControlTask created"
        );
    }
    else
    {
        Serial.println(
            "[FATAL] ControlTask creation failed"
        );
    }

    // =================================================
    // DISPLAY TASK
    // Core 1 / Priority 1
    // =================================================

    result =
        xTaskCreatePinnedToCore(
            displayTask,
            "DisplayTask",
            4096,
            nullptr,
            1,
            &displayTaskHandle,
            1
        );

    if (result == pdPASS)
    {
        Serial.println(
            "[SYSTEM] DisplayTask created"
        );
    }
    else
    {
        Serial.println(
            "[FATAL] DisplayTask creation failed"
        );
    }

    // =================================================
    // NETWORK TASK
    // Core 0 / Priority 2
    // =================================================

    result =
        xTaskCreatePinnedToCore(
            networkTask,
            "NetworkTask",
            4096,
            nullptr,
            2,
            &networkTaskHandle,
            0
        );

    if (result == pdPASS)
    {
        Serial.println(
            "[SYSTEM] NetworkTask created"
        );
    }
    else
    {
        Serial.println(
            "[FATAL] NetworkTask creation failed"
        );
    }

    // =================================================
    // WATCHDOG TASK
    // Core 0 / Priority 5
    // =================================================

    result =
        xTaskCreatePinnedToCore(
            watchdogTask,
            "WatchdogTask",
            4096,
            nullptr,
            5,
            &watchdogTaskHandle,
            0
        );

    if (result == pdPASS)
    {
        Serial.println(
            "[SYSTEM] WatchdogTask created"
        );
    }
    else
    {
        Serial.println(
            "[FATAL] WatchdogTask creation failed"
        );
    }

    // =================================================
    // CLI TASK
    // Core 0 / Priority 2
    // =================================================

    result =
        xTaskCreatePinnedToCore(
            cliTask,
            "CliTask",
            4096,
            nullptr,
            2,
            &cliTaskHandle,
            0
        );

    if (result == pdPASS)
    {
        Serial.println(
            "[SYSTEM] CliTask created"
        );
    }
    else
    {
        Serial.println(
            "[FATAL] CliTask creation failed"
        );
    }

    // =================================================
    // INITIAL STATE
    // =================================================

    updateIndicators(
        SYSTEM_INIT
    );

    Serial.println();

    Serial.println(
        "[SYSTEM] All RTOS tasks created"
    );

    Serial.println(
        "[SYSTEM] Measurement pipeline active"
    );

    Serial.println(
        "[SYSTEM] Protection controller active"
    );

    Serial.println(
        "[SYSTEM] Fault manager active"
    );

    Serial.println(
        "[SYSTEM] Diagnostics active"
    );

    Serial.println(
        "[SYSTEM] Watchdog active"
    );

    Serial.println(
        "[SYSTEM] Recovery supervisor active"
    );

    Serial.println(
        "[SYSTEM] UART CLI active"
    );

    Serial.println(
        "[SYSTEM] System initialization complete"
    );
}

// =====================================================
//                        LOOP
// =====================================================

void loop()
{
    // All application functionality runs
    // inside FreeRTOS tasks.

    vTaskDelay(
        pdMS_TO_TICKS(
            1000
        )
    );
}
