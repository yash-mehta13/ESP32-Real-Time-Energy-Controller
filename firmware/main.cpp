#define BLYNK_PRINT Serial

// =====================================================
//        ESP32 REAL-TIME ENERGY CONTROLLER
//        STEP 7D - PRODUCTION MQTT / TLS
// =====================================================
//
// Architecture:
//
//   Measurement Source
//          |
//          v
//     SensorTask
//          |
//          v
//   measurementQueue
//          |
//          v
//     ControlTask
//          |
//          +------> Protection / Fault Manager
//          |
//          v
//     System Snapshot
//          |
//          +------> DisplayTask
//          |
//          +------> NetworkTask
//                         |
//              +----------+----------+
//              |                     |
//            Blynk                 MQTT
//
// MQTT commands:
//
// MQTT Callback
//      |
//      v
// commandQueue
//      |
//      v
// ControlTask
//
// =====================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <BlynkSimpleEsp32.h>
#include <PubSubClient.h>
#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_task_wdt.h"
#include "esp_system.h"

#include "config.h"
#include "hivemq_ca.h"

// =====================================================
//                  FIRMWARE INFORMATION
// =====================================================

constexpr const char* FIRMWARE_NAME =
    "ESP32 Real-Time Energy Controller";

constexpr const char* FIRMWARE_VERSION =
    "1.2.0";

constexpr const char* FIRMWARE_BUILD =
    "STEP-7D-PRODUCTION-MQTT";

// =====================================================
//                  HARDWARE CONFIG
// =====================================================

constexpr uint8_t PZEM_RX_PIN = 16;
constexpr uint8_t PZEM_TX_PIN = 17;

constexpr uint8_t LED_NORMAL = 18;
constexpr uint8_t LED_ALERT = 19;

constexpr uint8_t LCD_ADDRESS = 0x27;

// =====================================================
//                  MEASUREMENT SOURCE
// =====================================================

enum MeasurementSource
{
    MEASUREMENT_SOURCE_PZEM,
    MEASUREMENT_SOURCE_SIMULATION
};

// -----------------------------------------------------
// Keep simulation while PZEM hardware is unavailable.
// -----------------------------------------------------

constexpr MeasurementSource MEASUREMENT_SOURCE =
    MEASUREMENT_SOURCE_SIMULATION;

// =====================================================
//                  PROTECTION CONFIG
// =====================================================

constexpr float POWER_WARNING_THRESHOLD_W =
    485.0f;

constexpr float POWER_FAULT_THRESHOLD_W =
    550.0f;

constexpr uint32_t FAULT_CONFIRM_TIME_MS =
    2000;

// =====================================================
//                    TASK PERIODS
// =====================================================

constexpr uint32_t SENSOR_PERIOD_MS = 1000;
constexpr uint32_t CONTROL_PERIOD_MS = 100;
constexpr uint32_t DISPLAY_PERIOD_MS = 500;
constexpr uint32_t NETWORK_PERIOD_MS = 10;
constexpr uint32_t TELEMETRY_PERIOD_MS = 2000;

// =====================================================
//                    QUEUE CONFIG
// =====================================================

constexpr uint8_t MEASUREMENT_QUEUE_LENGTH = 5;
constexpr uint8_t COMMAND_QUEUE_LENGTH = 8;

// =====================================================
//                  WATCHDOG CONFIG
// =====================================================

constexpr uint32_t WATCHDOG_TIMEOUT_MS = 8000;
constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 6000;
constexpr uint32_t WATCHDOG_STARTUP_GRACE_MS = 10000;
constexpr uint32_t WATCHDOG_SUPERVISOR_PERIOD_MS = 1000;

// =====================================================
//                  NETWORK CONFIG
// =====================================================

constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
constexpr uint32_t BLYNK_RECONNECT_INTERVAL_MS = 5000;

constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;

constexpr uint32_t NTP_CHECK_INTERVAL_MS = 1000;

constexpr uint32_t NTP_VALID_EPOCH = 1700000000UL;

// =====================================================
//                  MQTT CONFIG
// =====================================================

constexpr uint16_t MQTT_PORT = 8883;

constexpr uint16_t MQTT_BUFFER_SIZE = 1024;

constexpr uint16_t MQTT_KEEP_ALIVE_SECONDS = 30;

constexpr uint16_t MQTT_SOCKET_TIMEOUT_SECONDS = 5;

constexpr uint32_t MQTT_CONNECTION_TIMEOUT_MS = 10000;

// =====================================================
//                  MQTT TOPICS
// =====================================================

constexpr const char* MQTT_TOPIC_TELEMETRY =
    "energy/device01/telemetry";

constexpr const char* MQTT_TOPIC_STATE =
    "energy/device01/status/state";

constexpr const char* MQTT_TOPIC_FAULT =
    "energy/device01/fault/code";

constexpr const char* MQTT_TOPIC_AVAILABILITY =
    "energy/device01/status/availability";

constexpr const char* MQTT_TOPIC_CMD_RESET =
    "energy/device01/cmd/reset";

constexpr const char* MQTT_TOPIC_CMD_ACK =
    "energy/device01/cmd/ack";

constexpr const char* MQTT_TOPIC_COMMAND_STATUS =
    "energy/device01/status/command";

// =====================================================
//                       HARDWARE
// =====================================================

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
//                MEASUREMENT STRUCTURE
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

SystemState systemState = SYSTEM_INIT;

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

bool faultTimerActive = false;
uint32_t faultStartTime = 0;

// =====================================================
//                    RTOS OBJECTS
// =====================================================

SemaphoreHandle_t measurementMutex = nullptr;

SemaphoreHandle_t systemMutex = nullptr;

QueueHandle_t measurementQueue = nullptr;

QueueHandle_t commandQueue = nullptr;

// =====================================================
//                    TASK HANDLES
// =====================================================

TaskHandle_t sensorTaskHandle = nullptr;
TaskHandle_t controlTaskHandle = nullptr;
TaskHandle_t displayTaskHandle = nullptr;
TaskHandle_t networkTaskHandle = nullptr;
TaskHandle_t watchdogTaskHandle = nullptr;
TaskHandle_t cliTaskHandle = nullptr;

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

bool wifiConnected = false;
bool blynkConnected = false;
bool mqttConnected = false;

bool timeSynchronized = false;

bool ntpStarted = false;

uint32_t lastWiFiAttempt = 0;
uint32_t lastBlynkAttempt = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastTelemetryTime = 0;
uint32_t lastNtpCheck = 0;

// =====================================================
//                MQTT DIRTY FLAGS
// =====================================================
//
// ControlTask sets these flags.
//
// NetworkTask consumes them.
//
// This keeps all MQTT operations inside
// NetworkTask.
//

volatile bool mqttStateDirty = true;
volatile bool mqttFaultDirty = true;

// =====================================================
//                  MQTT CLIENT
// =====================================================

WiFiClientSecure mqttSecureClient;

PubSubClient mqttClient(
    mqttSecureClient
);

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
    CMD_VERSION,

    CMD_REMOTE_RESET,
    CMD_REMOTE_ACK
};

struct CliCommand
{
    CommandType type;
};

// =====================================================
//                 FUNCTION DECLARATIONS
// =====================================================

void sensorTask(void* parameter);
void controlTask(void* parameter);
void displayTask(void* parameter);
void networkTask(void* parameter);
void watchdogTask(void* parameter);
void cliTask(void* parameter);

Measurement readPzemMeasurement();
Measurement generateSimulatedMeasurement();
Measurement readMeasurement();

const char* getMeasurementSourceName();

bool initializeWatchdog();

bool registerCurrentTaskWithWatchdog(
    const char* taskName
);

void watchdogHeartbeat(
    HeartbeatId id
);

uint32_t getHeartbeatAge(
    HeartbeatId id
);

CommandType parseCommand(
    const char* command
);

void printCliPrompt();
void printCliHelp();

void processCliCommand(
    CommandType command
);

void processControlCommand(
    CommandType command
);

void updateSystemState(
    const Measurement& measurement
);

void updateIndicators(
    SystemState state
);

void raiseFault(
    FaultCode fault
);

void clearFault();
void resetFault();
void acknowledgeFault();

void printFaultStatus();

void printFaultRaised(
    FaultCode fault
);

void printFaultCleared(
    FaultCode fault
);

void printSystemStatus(
    const Measurement& measurement
);

void printWatchdogStatus();
void printTaskStatus();
void printMeasurement();
void printNetworkStatus();
void printUptime();
void printVersion();

const char* getStateName(
    SystemState state
);

const char* getFaultName(
    FaultCode fault
);

const char* getHeartbeatName(
    HeartbeatId id
);

void mqttCallback(
    char* topic,
    byte* payload,
    unsigned int length
);

bool connectMqtt();

void publishMqttTelemetry();

void publishMqttState();

void publishMqttFault();

void publishMqttCommandStatus(
    const char* command,
    const char* result
);

void markMqttStateDirty();
void markMqttFaultDirty();

bool isTimeSynchronized();

void startNtpSynchronization();

void checkNtpSynchronization();

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
//            MEASUREMENT SOURCE NAME
// =====================================================

const char* getMeasurementSourceName()
{
    switch (MEASUREMENT_SOURCE)
    {
        case MEASUREMENT_SOURCE_PZEM:
            return "PZEM";

        case MEASUREMENT_SOURCE_SIMULATION:
            return "SIMULATION";

        default:
            return "UNKNOWN";
    }
}

// =====================================================
//           SIMULATED MEASUREMENT SOURCE
// =====================================================

Measurement generateSimulatedMeasurement()
{
    Measurement measurement{};

    const uint32_t elapsed =
        millis() % 15000UL;

    if (elapsed < 5000UL)
    {
        measurement.voltage = 230.0f;
        measurement.current = 0.20f;
        measurement.power = 40.0f;
        measurement.energy = 0.0f;
    }
    else if (elapsed < 10000UL)
    {
        measurement.voltage = 230.0f;
        measurement.current = 2.17f;
        measurement.power = 500.0f;
        measurement.energy = 0.0f;
    }
    else
    {
        measurement.voltage = 230.0f;
        measurement.current = 2.60f;
        measurement.power = 600.0f;
        measurement.energy = 0.0f;
    }

    if (measurement.voltage > 0.0f &&
        measurement.current >= 0.0f)
    {
        measurement.apparentPower =
            measurement.voltage *
            measurement.current;
    }

    if (measurement.apparentPower > 0.0f)
    {
        measurement.powerFactor =
            measurement.power /
            measurement.apparentPower;

        measurement.powerFactor =
            constrain(
                measurement.powerFactor,
                0.0f,
                1.0f
            );
    }

    measurement.valid = true;
    measurement.timestamp = millis();

    return measurement;
}

// =====================================================
//              MEASUREMENT SOURCE
// =====================================================

Measurement readMeasurement()
{
    switch (MEASUREMENT_SOURCE)
    {
        case MEASUREMENT_SOURCE_PZEM:
            return readPzemMeasurement();

        case MEASUREMENT_SOURCE_SIMULATION:
            return generateSimulatedMeasurement();

        default:
        {
            Measurement invalid{};

            invalid.valid = false;
            invalid.timestamp = millis();

            return invalid;
        }
    }
}

// =====================================================
//              PZEM MEASUREMENT SOURCE
// =====================================================

Measurement readPzemMeasurement()
{
    Measurement reading{};

    reading.voltage = pzem.voltage();
    reading.current = pzem.current();
    reading.power = pzem.power();
    reading.energy = pzem.energy();

    if (!isnan(reading.voltage) &&
        !isnan(reading.current) &&
        reading.voltage > 0.0f &&
        reading.current >= 0.0f)
    {
        reading.apparentPower =
            reading.voltage *
            reading.current;
    }

    if (!isnan(reading.power) &&
        reading.apparentPower > 0.0f)
    {
        reading.powerFactor =
            reading.power /
            reading.apparentPower;

        reading.powerFactor =
            constrain(
                reading.powerFactor,
                0.0f,
                1.0f
            );
    }

    reading.timestamp = millis();

    reading.valid =
        !isnan(reading.voltage) &&
        !isnan(reading.current) &&
        !isnan(reading.power) &&
        !isnan(reading.energy) &&
        !isnan(reading.apparentPower) &&
        !isnan(reading.powerFactor);

    return reading;
}

// =====================================================
//                 WATCHDOG INIT
// =====================================================

bool initializeWatchdog()
{
    esp_task_wdt_config_t config =
    {
        .timeout_ms = WATCHDOG_TIMEOUT_MS,
        .idle_core_mask =
            (1U << portNUM_PROCESSORS) - 1U,
        .trigger_panic = true
    };

    esp_err_t result =
        esp_task_wdt_init(&config);

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
        "[WATCHDOG] Initialization failed: "
    );

    Serial.println(
        static_cast<int>(result)
    );

    return false;
}

// =====================================================
//          REGISTER TASK WITH WATCHDOG
// =====================================================

bool registerCurrentTaskWithWatchdog(
    const char* taskName
)
{
    esp_err_t result =
        esp_task_wdt_add(nullptr);

    if (result == ESP_OK ||
        result == ESP_ERR_INVALID_ARG)
    {
        Serial.print(
            "[WATCHDOG] Registered: "
        );

        Serial.println(taskName);

        return true;
    }

    Serial.print(
        "[WATCHDOG] Registration failed: "
    );

    Serial.println(taskName);

    return false;
}

// =====================================================
//                  HEARTBEAT
// =====================================================

void watchdogHeartbeat(
    HeartbeatId id
)
{
    if (id >= HEARTBEAT_COUNT)
        return;

    heartbeatTimestamp[id] =
        millis();
}

// =====================================================
//              HEARTBEAT AGE
// =====================================================

uint32_t getHeartbeatAge(
    HeartbeatId id
)
{
    if (id >= HEARTBEAT_COUNT)
        return UINT32_MAX;

    const uint32_t now = millis();

    const uint32_t lastHeartbeat =
        heartbeatTimestamp[id];

    if (lastHeartbeat == 0)
        return UINT32_MAX;

    return now - lastHeartbeat;
}

// =====================================================
//                    SENSOR TASK
// =====================================================

void sensorTask(void* parameter)
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
        Measurement reading =
            readMeasurement();

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

        watchdogHeartbeat(
            HEARTBEAT_SENSOR
        );

        esp_task_wdt_reset();

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

void controlTask(void* parameter)
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
}

// =====================================================
//                   DISPLAY TASK
// =====================================================

void displayTask(void* parameter)
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
//                  MQTT CALLBACK
// =====================================================

void mqttCallback(
    char* topic,
    byte* payload,
    unsigned int length
)
{
    char message[32];

    const unsigned int copyLength =
        min(
            length,
            static_cast<unsigned int>(
                sizeof(message) - 1
            )
        );

    memcpy(
        message,
        payload,
        copyLength
    );

    message[copyLength] =
        '\0';

    Serial.print(
        "[MQTT] Command received: "
    );

    Serial.print(topic);

    Serial.print(" -> ");

    Serial.println(message);

    CliCommand command{};

    if (strcmp(
            topic,
            MQTT_TOPIC_CMD_RESET
        ) == 0)
    {
        if (strcmp(
                message,
                "reset"
            ) != 0)
        {
            Serial.println(
                "[MQTT] Invalid reset command"
            );

            return;
        }

        command.type =
            CMD_REMOTE_RESET;
    }
    else if (
        strcmp(
            topic,
            MQTT_TOPIC_CMD_ACK
        ) == 0
    )
    {
        if (strcmp(
                message,
                "ack"
            ) != 0)
        {
            Serial.println(
                "[MQTT] Invalid ACK command"
            );

            return;
        }

        command.type =
            CMD_REMOTE_ACK;
    }
    else
    {
        Serial.println(
            "[MQTT] Unknown command topic"
        );

        return;
    }

    if (xQueueSend(
            commandQueue,
            &command,
            0
        ) != pdPASS)
    {
        Serial.println(
            "[MQTT] Command queue full"
        );
    }
}

// =====================================================
//                 MQTT CONNECTION
// =====================================================

bool connectMqtt()
{
    if (WiFi.status() != WL_CONNECTED)
        return false;

    if (!isTimeSynchronized())
    {
        Serial.println(
            "[MQTT] Waiting for NTP time synchronization"
        );

        return false;
    }

    uint64_t chipId =
        ESP.getEfuseMac();

    char clientId[64];

    snprintf(
        clientId,
        sizeof(clientId),
        "%s-%04X%08X",
        MQTT_DEVICE_ID,
        static_cast<uint16_t>(
            chipId >> 32
        ),
        static_cast<uint32_t>(
            chipId
        )
    );

    Serial.print(
        "[MQTT] Connecting as: "
    );

    Serial.println(clientId);

    bool connected =
        mqttClient.connect(
            clientId,
            MQTT_USERNAME,
            MQTT_PASSWORD,
            MQTT_TOPIC_AVAILABILITY,
            1,
            true,
            "offline"
        );

    if (!connected)
    {
        mqttConnected = false;

        Serial.print(
            "[MQTT] Connection failed. State = "
        );

        Serial.println(
            mqttClient.state()
        );

        return false;
    }

    mqttConnected = true;

    Serial.println(
        "[MQTT] Connected securely"
    );

    if (!mqttClient.publish(
            MQTT_TOPIC_AVAILABILITY,
            "online",
            true
        ))
    {
        Serial.println(
            "[MQTT] Online status publish failed"
        );
    }

    bool resetSubscribed =
        mqttClient.subscribe(
            MQTT_TOPIC_CMD_RESET
        );

    bool ackSubscribed =
        mqttClient.subscribe(
            MQTT_TOPIC_CMD_ACK
        );

    Serial.print(
        "[MQTT] Reset subscription: "
    );

    Serial.println(
        resetSubscribed
            ? "OK"
            : "FAILED"
    );

    Serial.print(
        "[MQTT] ACK subscription: "
    );

    Serial.println(
        ackSubscribed
            ? "OK"
            : "FAILED"
    );

    mqttStateDirty = true;
    mqttFaultDirty = true;

    publishMqttState();
    publishMqttFault();

    publishMqttCommandStatus(
        "connection",
        "connected"
    );

    return true;
}

// =====================================================
//                 MQTT TELEMETRY
// =====================================================

void publishMqttTelemetry()
{
    if (!mqttClient.connected())
    {
        mqttConnected = false;
        return;
    }

    Measurement measurement{};

    if (xSemaphoreTake(
            measurementMutex,
            pdMS_TO_TICKS(50)
        ) != pdTRUE)
    {
        return;
    }

    measurement =
        latestMeasurement;

    xSemaphoreGive(
        measurementMutex
    );

    SystemState stateSnapshot;
    FaultManager faultSnapshot;

    if (xSemaphoreTake(
            systemMutex,
            pdMS_TO_TICKS(50)
        ) != pdTRUE)
    {
        return;
    }

    stateSnapshot =
        systemState;

    faultSnapshot =
        faultManager;

    xSemaphoreGive(
        systemMutex
    );

    char payload[1024];

    snprintf(
        payload,
        sizeof(payload),

        "{"
        "\"device_id\":\"%s\","
        "\"firmware_version\":\"%s\","
        "\"voltage\":%.2f,"
        "\"current\":%.3f,"
        "\"power\":%.2f,"
        "\"energy\":%.3f,"
        "\"apparent_power\":%.2f,"
        "\"power_factor\":%.3f,"
        "\"valid\":%s,"
        "\"state\":\"%s\","
        "\"fault\":\"%s\","
        "\"fault_active\":%s,"
        "\"acknowledged\":%s,"
        "\"fault_count\":%lu,"
        "\"source\":\"%s\","
        "\"uptime_ms\":%lu"
        "}",

        MQTT_DEVICE_ID,
        FIRMWARE_VERSION,

        measurement.voltage,
        measurement.current,
        measurement.power,
        measurement.energy,
        measurement.apparentPower,
        measurement.powerFactor,

        measurement.valid
            ? "true"
            : "false",

        getStateName(
            stateSnapshot
        ),

        getFaultName(
            faultSnapshot.activeFault
        ),

        faultSnapshot.active
            ? "true"
            : "false",

        faultSnapshot.acknowledged
            ? "true"
            : "false",

        static_cast<unsigned long>(
            faultSnapshot.totalFaults
        ),

        getMeasurementSourceName(),

        static_cast<unsigned long>(
            millis()
        )
    );

    if (!mqttClient.publish(
            MQTT_TOPIC_TELEMETRY,
            payload
        ))
    {
        Serial.println(
            "[MQTT] Telemetry publish failed"
        );
    }
}

// =====================================================
//                    MQTT STATE
// =====================================================

void publishMqttState()
{
    if (!mqttClient.connected())
    {
        mqttConnected = false;
        return;
    }

    SystemState stateSnapshot;

    if (xSemaphoreTake(
            systemMutex,
            pdMS_TO_TICKS(50)
        ) != pdTRUE)
    {
        return;
    }

    stateSnapshot =
        systemState;

    xSemaphoreGive(
        systemMutex
    );

    if (mqttClient.publish(
            MQTT_TOPIC_STATE,
            getStateName(
                stateSnapshot
            ),
            true
        ))
    {
        mqttStateDirty = false;
    }
    else
    {
        Serial.println(
            "[MQTT] State publish failed"
        );
    }
}

// =====================================================
//                    MQTT FAULT
// =====================================================

void publishMqttFault()
{
    if (!mqttClient.connected())
    {
        mqttConnected = false;
        return;
    }

    FaultCode faultSnapshot;

    if (xSemaphoreTake(
            systemMutex,
            pdMS_TO_TICKS(50)
        ) != pdTRUE)
    {
        return;
    }

    faultSnapshot =
        faultManager.activeFault;

    xSemaphoreGive(
        systemMutex
    );

    if (mqttClient.publish(
            MQTT_TOPIC_FAULT,
            getFaultName(
                faultSnapshot
            ),
            true
        ))
    {
        mqttFaultDirty = false;
    }
    else
    {
        Serial.println(
            "[MQTT] Fault publish failed"
        );
    }
}

// =====================================================
//              MQTT COMMAND STATUS
// =====================================================

void publishMqttCommandStatus(
    const char* command,
    const char* result
)
{
    if (!mqttClient.connected())
        return;

    char payload[160];

    snprintf(
        payload,
        sizeof(payload),
        "{\"command\":\"%s\",\"result\":\"%s\",\"uptime_ms\":%lu}",
        command,
        result,
        static_cast<unsigned long>(
            millis()
        )
    );

    mqttClient.publish(
        MQTT_TOPIC_COMMAND_STATUS,
        payload
    );
}

// =====================================================
//                 MQTT DIRTY FLAGS
// =====================================================

void markMqttStateDirty()
{
    mqttStateDirty = true;
}

void markMqttFaultDirty()
{
    mqttFaultDirty = true;
}

// =====================================================
//                  NTP FUNCTIONS
// =====================================================

bool isTimeSynchronized()
{
    time_t now = time(nullptr);

    return now >=
           static_cast<time_t>(
               NTP_VALID_EPOCH
           );
}

// =====================================================
//             START NTP SYNCHRONIZATION
// =====================================================

void startNtpSynchronization()
{
    if (ntpStarted)
        return;

    Serial.println(
        "[NTP] Starting time synchronization"
    );

    configTime(
        0,
        0,
        NTP_SERVER_1,
        NTP_SERVER_2
    );

    ntpStarted = true;
}

// =====================================================
//             CHECK NTP SYNCHRONIZATION
// =====================================================

void checkNtpSynchronization()
{
    if (!wifiConnected)
        return;

    if (!ntpStarted)
        startNtpSynchronization();

    if (isTimeSynchronized())
    {
        if (!timeSynchronized)
        {
            timeSynchronized = true;

            time_t now = time(nullptr);

            Serial.print(
                "[NTP] Time synchronized: "
            );

            Serial.println(
                static_cast<unsigned long>(
                    now
                )
            );
        }

        return;
    }

    timeSynchronized = false;
}

// =====================================================
//                  NETWORK TASK
// =====================================================

void networkTask(void* parameter)
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
                wifiConnected = false;
                blynkConnected = false;
                mqttConnected = false;
                timeSynchronized = false;
                ntpStarted = false;

                Serial.println(
                    "[NETWORK] WiFi disconnected"
                );
            }

            if (
                now - lastWiFiAttempt >=
                WIFI_RECONNECT_INTERVAL_MS
            )
            {
                lastWiFiAttempt = now;

                Serial.println(
                    "[NETWORK] Attempting WiFi reconnect"
                );

                WiFi.begin(
                    WIFI_SSID,
                    WIFI_PASSWORD
                );
            }
        }
        else
        {
            if (!wifiConnected)
            {
                wifiConnected = true;

                Serial.print(
                    "[NETWORK] WiFi connected. IP: "
                );

                Serial.println(
                    WiFi.localIP()
                );

                startNtpSynchronization();
            }

            // =================================================
            // NTP
            // =================================================

            if (
                now - lastNtpCheck >=
                NTP_CHECK_INTERVAL_MS
            )
            {
                lastNtpCheck = now;

                checkNtpSynchronization();
            }

            // =================================================
            // BLYNK
            // =================================================

            if (!Blynk.connected())
            {
                blynkConnected = false;

                if (
                    now - lastBlynkAttempt >=
                    BLYNK_RECONNECT_INTERVAL_MS
                )
                {
                    lastBlynkAttempt = now;

                    Serial.println(
                        "[NETWORK] Attempting Blynk connection"
                    );

                    if (Blynk.connect(1000))
                    {
                        blynkConnected = true;

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
                    blynkConnected = true;

                    Serial.println(
                        "[NETWORK] Blynk connection restored"
                    );
                }

                Blynk.run();
            }

            // =================================================
            // MQTT
            // =================================================

            if (!mqttClient.connected())
            {
                mqttConnected = false;

                if (
                    timeSynchronized &&
                    now - lastMqttAttempt >=
                    MQTT_RECONNECT_INTERVAL_MS
                )
                {
                    lastMqttAttempt = now;

                    connectMqtt();
                }
            }
            else
            {
                mqttConnected = true;

                mqttClient.loop();

                // -------------------------------------------------
                // Publish changed state
                // -------------------------------------------------

                if (mqttStateDirty)
                {
                    publishMqttState();
                }

                // -------------------------------------------------
                // Publish changed fault
                // -------------------------------------------------

                if (mqttFaultDirty)
                {
                    publishMqttFault();
                }
            }

            // =================================================
            // TELEMETRY
            // =================================================

            if (
                now - lastTelemetryTime >=
                TELEMETRY_PERIOD_MS
            )
            {
                lastTelemetryTime = now;

                Measurement measurement{};

                if (
                    xSemaphoreTake(
                        measurementMutex,
                        pdMS_TO_TICKS(50)
                    ) == pdTRUE
                )
                {
                    measurement =
                        latestMeasurement;

                    xSemaphoreGive(
                        measurementMutex
                    );
                }

                // -------------------------------------------------
                // Blynk telemetry
                // -------------------------------------------------

                if (
                    blynkConnected &&
                    measurement.valid
                )
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

                // -------------------------------------------------
                // MQTT telemetry
                // -------------------------------------------------

                if (mqttConnected)
                {
                    publishMqttTelemetry();
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

void watchdogTask(void* parameter)
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

        if (
            now - supervisorStartTime <
            WATCHDOG_STARTUP_GRACE_MS
        )
        {
            vTaskDelay(
                pdMS_TO_TICKS(
                    WATCHDOG_SUPERVISOR_PERIOD_MS
                )
            );

            continue;
        }

        bool systemHealthy = true;

        for (
            uint8_t i = 0;
            i < HEARTBEAT_COUNT;
            i++
        )
        {
            const uint32_t lastHeartbeat =
                heartbeatTimestamp[i];

            if (lastHeartbeat == 0)
            {
                systemHealthy = false;

                Serial.print(
                    "[WATCHDOG] Missing heartbeat: "
                );

                Serial.println(
                    getHeartbeatName(
                        static_cast<HeartbeatId>(i)
                    )
                );

                continue;
            }

            const uint32_t heartbeatAge =
                getHeartbeatAge(
                    static_cast<HeartbeatId>(i)
                );

            if (
                heartbeatAge >
                HEARTBEAT_TIMEOUT_MS
            )
            {
                systemHealthy = false;

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
            }
        }

        if (!systemHealthy)
        {
            Serial.println(
                "[WATCHDOG] FAILURE - restarting"
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
//                     CLI PROMPT
// =====================================================

void printCliPrompt()
{
    Serial.print(
        "\r\ndiag> "
    );
}

// =====================================================
//                     CLI TASK
// =====================================================

void cliTask(void* parameter)
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

    uint8_t bufferIndex = 0;

    bool commandOverflow = false;

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

            if (
                c == '\r' ||
                c == '\n'
            )
            {
                if (
                    bufferIndex == 0 &&
                    !commandOverflow
                )
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
                    }
                    else
                    {
                        processCliCommand(
                            command
                        );
                    }
                }

                bufferIndex = 0;
                commandOverflow = false;

                printCliPrompt();

                continue;
            }

            if (
                c == '\b' ||
                c == 127
            )
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

            if (
                c < 32 ||
                c > 126
            )
            {
                continue;
            }

            if (
                bufferIndex <
                CLI_BUFFER_SIZE - 1
            )
            {
                commandBuffer[
                    bufferIndex++
                ] = c;

                Serial.print(c);
            }
            else
            {
                commandOverflow = true;
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
    const char* command
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
        "watchdog   - Watchdog status"
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
        {
            CliCommand request{CMD_STATUS};

            xQueueSend(
                commandQueue,
                &request,
                pdMS_TO_TICKS(50)
            );

            break;
        }

        case CMD_MEASURE:
            printMeasurement();
            break;

        case CMD_FAULT:
        {
            CliCommand request{CMD_FAULT};

            xQueueSend(
                commandQueue,
                &request,
                pdMS_TO_TICKS(50)
            );

            break;
        }

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
            CliCommand request{CMD_RESET};

            xQueueSend(
                commandQueue,
                &request,
                pdMS_TO_TICKS(50)
            );

            break;
        }

        case CMD_ACK:
        {
            CliCommand request{CMD_ACK};

            xQueueSend(
                commandQueue,
                &request,
                pdMS_TO_TICKS(50)
            );

            break;
        }

        case CMD_VERSION:
            printVersion();
            break;

        default:
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

            if (
                xSemaphoreTake(
                    measurementMutex,
                    pdMS_TO_TICKS(50)
                ) == pdTRUE
            )
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

        case CMD_REMOTE_RESET:

            Serial.println(
                "[MQTT] Remote reset command executing"
            );

            resetFault();

            publishMqttCommandStatus(
                "reset",
                "accepted"
            );

            break;

        case CMD_REMOTE_ACK:

            Serial.println(
                "[MQTT] Remote ACK command executing"
            );

            acknowledgeFault();

            publishMqttCommandStatus(
                "ack",
                "accepted"
            );

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

    if (
        xSemaphoreTake(
            measurementMutex,
            pdMS_TO_TICKS(50)
        ) == pdTRUE
    )
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
        "Source           : "
    );

    Serial.println(
        getMeasurementSourceName()
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

    Serial.println(" V");

    Serial.print(
        "Current          : "
    );

    Serial.print(
        measurement.current,
        3
    );

    Serial.println(" A");

    Serial.print(
        "Power            : "
    );

    Serial.print(
        measurement.power,
        2
    );

    Serial.println(" W");

    Serial.print(
        "Energy           : "
    );

    Serial.print(
        measurement.energy,
        3
    );

    Serial.println(" kWh");

    Serial.print(
        "Apparent Power   : "
    );

    Serial.print(
        measurement.apparentPower,
        2
    );

    Serial.println(" VA");

    Serial.print(
        "Power Factor     : "
    );

    Serial.println(
        measurement.powerFactor,
        3
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

        Serial.println(" dBm");
    }

    Serial.print(
        "NTP              : "
    );

    Serial.println(
        timeSynchronized
            ? "SYNCHRONIZED"
            : "NOT SYNCHRONIZED"
    );

    Serial.print(
        "Blynk            : "
    );

    Serial.println(
        blynkConnected
            ? "CONNECTED"
            : "DISCONNECTED"
    );

    Serial.print(
        "MQTT             : "
    );

    Serial.println(
        mqttConnected
            ? "CONNECTED/TLS"
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

    Serial.printf(
        "Uptime           : %lud %luh %lum %lus\n",
        static_cast<unsigned long>(days),
        static_cast<unsigned long>(hours),
        static_cast<unsigned long>(minutes),
        static_cast<unsigned long>(seconds)
    );

    Serial.print(
        "Milliseconds     : "
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
        "Measurement Source: "
    );

    Serial.println(
        getMeasurementSourceName()
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

    for (
        uint8_t i = 0;
        i < 6;
        i++
    )
    {
        if (handles[i] == nullptr)
        {
            Serial.print(names[i]);
            Serial.println(
                " : NOT CREATED"
            );

            continue;
        }

        Serial.print(names[i]);

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

        Serial.println(
            uxTaskGetStackHighWaterMark(
                handles[i]
            )
        );
    }

    Serial.println(
        "========================================"
    );
}

// =====================================================
//              SYSTEM STATUS
// =====================================================

void printSystemStatus(
    const Measurement& measurement
)
{
    SystemState stateSnapshot;
    FaultManager faultSnapshot;

    if (
        xSemaphoreTake(
            systemMutex,
            pdMS_TO_TICKS(50)
        ) != pdTRUE
    )
    {
        return;
    }

    stateSnapshot = systemState;
    faultSnapshot = faultManager;

    xSemaphoreGive(
        systemMutex
    );

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
            stateSnapshot
        )
    );

    Serial.print(
        "Active Fault     : "
    );

    Serial.println(
        getFaultName(
            faultSnapshot.activeFault
        )
    );

    Serial.print(
        "Last Fault       : "
    );

    Serial.println(
        getFaultName(
            faultSnapshot.lastFault
        )
    );

    Serial.print(
        "Fault Count      : "
    );

    Serial.println(
        faultSnapshot.totalFaults
    );

    Serial.print(
        "Acknowledged     : "
    );

    Serial.println(
        faultSnapshot.acknowledged
            ? "YES"
            : "NO"
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

    Serial.println(
        "========================================"
    );
}

// =====================================================
//              WATCHDOG STATUS
// =====================================================

void printWatchdogStatus()
{
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

    for (
        uint8_t i = 0;
        i < HEARTBEAT_COUNT;
        i++
    )
    {
        Serial.print(
            getHeartbeatName(
                static_cast<HeartbeatId>(i)
            )
        );

        Serial.print(" : ");

        uint32_t age =
            getHeartbeatAge(
                static_cast<HeartbeatId>(i)
            );

        if (age == UINT32_MAX)
        {
            Serial.println(
                "NO HEARTBEAT"
            );
        }
        else
        {
            Serial.print(age);
            Serial.println(
                " ms ago"
            );
        }
    }

    Serial.println(
        "========================================"
    );
}

// =====================================================
//              SYSTEM STATE LOGIC
// =====================================================

void updateSystemState(
    const Measurement& measurement
)
{
    SystemState previousState;
    FaultCode previousFault;

    if (
        xSemaphoreTake(
            systemMutex,
            pdMS_TO_TICKS(50)
        ) != pdTRUE
    )
    {
        return;
    }

    previousState = systemState;
    previousFault =
        faultManager.activeFault;

    if (systemState == SYSTEM_FAULT)
    {
        xSemaphoreGive(
            systemMutex
        );

        return;
    }

    if (!measurement.valid)
    {
        faultTimerActive = false;

        raiseFault(
            FAULT_SENSOR_INVALID
        );
    }
    else if (measurement.voltage <= 10.0f)
    {
        faultTimerActive = false;

        systemState =
            SYSTEM_INIT;
    }
    else if (
        measurement.power >
        POWER_FAULT_THRESHOLD_W
    )
    {
        if (!faultTimerActive)
        {
            faultTimerActive = true;
            faultStartTime = millis();

            Serial.println(
                "[PROTECTION] Fault timer started"
            );
        }

        if (
            millis() - faultStartTime >=
            FAULT_CONFIRM_TIME_MS
        )
        {
            faultTimerActive = false;

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
    else if (
        measurement.power >
        POWER_WARNING_THRESHOLD_W
    )
    {
        faultTimerActive = false;

        systemState =
            SYSTEM_WARNING;
    }
    else
    {
        faultTimerActive = false;

        systemState =
            SYSTEM_NORMAL;
    }

    if (systemState != previousState)
    {
        Serial.print(
            "[SYSTEM] "
        );

        Serial.print(
            getStateName(previousState)
        );

        Serial.print(
            " -> "
        );

        Serial.println(
            getStateName(systemState)
        );

        updateIndicators(
            systemState
        );

        markMqttStateDirty();
    }

    if (
        faultManager.activeFault !=
        previousFault
    )
    {
        markMqttFaultDirty();

        if (
            faultManager.activeFault !=
            FAULT_NONE
        )
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

    xSemaphoreGive(
        systemMutex
    );
}

// =====================================================
//                    RAISE FAULT
// =====================================================

void raiseFault(
    FaultCode fault
)
{
    if (fault == FAULT_NONE)
        return;

    if (
        faultManager.active &&
        faultManager.activeFault == fault
    )
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

    faultManager.active = true;

    faultManager.acknowledged =
        false;

    systemState =
        SYSTEM_FAULT;

    updateIndicators(
        SYSTEM_FAULT
    );
}

// =====================================================
//                    CLEAR FAULT
// =====================================================

void clearFault()
{
    if (!faultManager.active)
        return;

    const FaultCode clearedFault =
        faultManager.activeFault;

    faultManager.activeFault =
        FAULT_NONE;

    faultManager.active = false;

    faultManager.acknowledged =
        false;

    faultManager.activeSince = 0;

    faultTimerActive = false;
    faultStartTime = 0;

    systemState =
        SYSTEM_INIT;

    updateIndicators(
        SYSTEM_INIT
    );

    markMqttFaultDirty();
    markMqttStateDirty();

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
    FaultManager snapshot;

    if (
        xSemaphoreTake(
            systemMutex,
            pdMS_TO_TICKS(50)
        ) != pdTRUE
    )
    {
        return;
    }

    snapshot =
        faultManager;

    xSemaphoreGive(
        systemMutex
    );

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
            snapshot.activeFault
        )
    );

    Serial.print(
        "Last Fault       : "
    );

    Serial.println(
        getFaultName(
            snapshot.lastFault
        )
    );

    Serial.print(
        "Active           : "
    );

    Serial.println(
        snapshot.active
            ? "YES"
            : "NO"
    );

    Serial.print(
        "Acknowledged     : "
    );

    Serial.println(
        snapshot.acknowledged
            ? "YES"
            : "NO"
    );

    Serial.print(
        "Total Faults     : "
    );

    Serial.println(
        snapshot.totalFaults
    );

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
        "[FAULT] Code      : "
    );

    Serial.println(
        getFaultName(fault)
    );

    Serial.print(
        "[FAULT] Count     : "
    );

    Serial.println(
        faultManager.totalFaults
    );

    Serial.println(
        "[FAULT] State     : LATCHED"
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

    Serial.print(
        "[FAULT] CLEARED    : "
    );

    Serial.println(
        getFaultName(fault)
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
    Serial.begin(115200);

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
        " STEP 7D - PRODUCTION MQTT / TLS"
    );

    Serial.println(
        "=========================================="
    );

    Serial.print(
        "[SYSTEM] Measurement Source: "
    );

    Serial.println(
        getMeasurementSourceName()
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

    lcd.setCursor(0, 0);

    lcd.print(
        "Energy Controller"
    );

    lcd.setCursor(0, 1);

    lcd.print(
        "Booting..."
    );

    // =================================================
    // MUTEXES
    // =================================================

    measurementMutex =
        xSemaphoreCreateMutex();

    systemMutex =
        xSemaphoreCreateMutex();

    if (
        measurementMutex == nullptr ||
        systemMutex == nullptr
    )
    {
        Serial.println(
            "[FATAL] Mutex creation failed"
        );

        while (true)
        {
            delay(1000);
        }
    }

    // =================================================
    // QUEUES
    // =================================================

    measurementQueue =
        xQueueCreate(
            MEASUREMENT_QUEUE_LENGTH,
            sizeof(Measurement)
        );

    commandQueue =
        xQueueCreate(
            COMMAND_QUEUE_LENGTH,
            sizeof(CliCommand)
        );

    if (
        measurementQueue == nullptr ||
        commandQueue == nullptr
    )
    {
        Serial.println(
            "[FATAL] Queue creation failed"
        );

        while (true)
        {
            delay(1000);
        }
    }

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
        WIFI_SSID,
        WIFI_PASSWORD
    );

    // =================================================
    // BLYNK
    // =================================================

    Blynk.config(
        BLYNK_AUTH_TOKEN
    );

    Serial.println(
        "[SYSTEM] Blynk configured"
    );

    // =================================================
    // MQTT
    // =================================================

    mqttSecureClient.setCACert(
        HIVEMQ_ROOT_CA
    );

    mqttClient.setServer(
        MQTT_BROKER_HOST,
        MQTT_PORT
    );

    mqttClient.setCallback(
        mqttCallback
    );

    mqttClient.setKeepAlive(
        MQTT_KEEP_ALIVE_SECONDS
    );

    mqttClient.setSocketTimeout(
        MQTT_SOCKET_TIMEOUT_SECONDS
    );

    mqttClient.setBufferSize(
        MQTT_BUFFER_SIZE
    );

    Serial.println(
        "[SYSTEM] MQTT TLS configured"
    );

    // =================================================
    // SENSOR TASK
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

    if (result != pdPASS)
    {
        Serial.println(
            "[FATAL] SensorTask creation failed"
        );
    }

    // =================================================
    // CONTROL TASK
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

    if (result != pdPASS)
    {
        Serial.println(
            "[FATAL] ControlTask creation failed"
        );
    }

    // =================================================
    // DISPLAY TASK
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

    if (result != pdPASS)
    {
        Serial.println(
            "[FATAL] DisplayTask creation failed"
        );
    }

    // =================================================
    // NETWORK TASK
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

    if (result != pdPASS)
    {
        Serial.println(
            "[FATAL] NetworkTask creation failed"
        );
    }

    // =================================================
    // WATCHDOG TASK
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

    if (result != pdPASS)
    {
        Serial.println(
            "[FATAL] WatchdogTask creation failed"
        );
    }

    // =================================================
    // CLI TASK
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

    if (result != pdPASS)
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
        "[SYSTEM] Measurement abstraction active"
    );

    Serial.println(
        "[SYSTEM] Protection controller active"
    );

    Serial.println(
        "[SYSTEM] Fault manager active"
    );

    Serial.println(
        "[SYSTEM] Watchdog active"
    );

    Serial.println(
        "[SYSTEM] Blynk telemetry active"
    );

    Serial.println(
        "[SYSTEM] MQTT TLS active"
    );

    Serial.println(
        "[SYSTEM] MQTT command interface active"
    );

    Serial.println(
        "[SYSTEM] NTP synchronization active"
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
    vTaskDelay(
        pdMS_TO_TICKS(1000)
    );
}
