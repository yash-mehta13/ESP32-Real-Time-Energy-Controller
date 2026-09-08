#pragma once

// Copy this file to src/config.h and fill in your own credentials.
// src/config.h is intentionally ignored by Git.
//
// NEVER commit:
// - Wi-Fi passwords
// - Blynk authentication tokens
// - MQTT passwords
// - private keys

#define BLYNK_TEMPLATE_ID "YOUR_BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Energy Monitor"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

constexpr char MQTT_BROKER_HOST[] = "YOUR_CLUSTER.s1.eu.hivemq.cloud";
constexpr char MQTT_USERNAME[] = "YOUR_MQTT_USERNAME";
constexpr char MQTT_PASSWORD[] = "YOUR_MQTT_PASSWORD";


// -----------------------------------------------------------------------------
// MQTT / device identity
// -----------------------------------------------------------------------------

constexpr uint16_t MQTT_PORT = 8883;
constexpr char MQTT_DEVICE_ID[] = "device01";

constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;

// -----------------------------------------------------------------------------
// MQTT topic namespace
// -----------------------------------------------------------------------------

// Topics intentionally remain device-specific and narrow.
// Do not grant the MQTT account broad '#' permissions.

constexpr char MQTT_TOPIC_TELEMETRY[] =
    "energy/device01/telemetry";
constexpr char MQTT_TOPIC_STATE[] =
    "energy/device01/status/state";
constexpr char MQTT_TOPIC_FAULT[] =
    "energy/device01/fault/code";
constexpr char MQTT_TOPIC_AVAILABILITY[] =
    "energy/device01/status/availability";
constexpr char MQTT_TOPIC_CMD_RESET[] =
    "energy/device01/cmd/reset";
constexpr char MQTT_TOPIC_CMD_ACK[] =
    "energy/device01/cmd/ack";

// -----------------------------------------------------------------------------
// Firmware metadata
// -----------------------------------------------------------------------------

constexpr char FIRMWARE_NAME[] =
    "ESP32 Real-Time Energy Controller";
constexpr char FIRMWARE_VERSION[] = "1.2.0";
constexpr char FIRMWARE_BUILD[] =
    "STEP-8-PRODUCTION-FIRMWARE";

// -----------------------------------------------------------------------------
// Hardware
// -----------------------------------------------------------------------------

constexpr uint8_t PZEM_RX_PIN = 16;
constexpr uint8_t PZEM_TX_PIN = 17;
constexpr uint32_t PZEM_BAUD_RATE = 9600;

constexpr uint8_t LED_NORMAL_PIN = 18;
constexpr uint8_t LED_ALERT_PIN = 19;

// Fail-safe relay control.
// Change this pin to match the actual relay driver circuit.
// The software assumes an active-low relay module by default.
constexpr uint8_t RELAY_PIN = 23;
constexpr uint8_t RELAY_ON_LEVEL = LOW;
constexpr uint8_t RELAY_OFF_LEVEL = HIGH;

constexpr uint8_t LCD_I2C_ADDRESS = 0x27;
constexpr uint8_t LCD_COLUMNS = 16;
constexpr uint8_t LCD_ROWS = 2;

// -----------------------------------------------------------------------------
// Measurement source
// -----------------------------------------------------------------------------

// 0 = real PZEM, 1 = deterministic simulation
constexpr uint8_t MEASUREMENT_SOURCE = 1;

// Change to 0 when the real PZEM is connected.

// -----------------------------------------------------------------------------
// Protection
// -----------------------------------------------------------------------------

constexpr float POWER_WARNING_THRESHOLD_W = 485.0f;
constexpr float POWER_FAULT_THRESHOLD_W = 550.0f;
constexpr uint32_t FAULT_CONFIRM_TIME_MS = 2000;

// -----------------------------------------------------------------------------
// RTOS timing
// -----------------------------------------------------------------------------

constexpr uint32_t SENSOR_PERIOD_MS = 1000;
constexpr uint32_t CONTROL_PERIOD_MS = 100;
constexpr uint32_t DISPLAY_PERIOD_MS = 500;
constexpr uint32_t NETWORK_PERIOD_MS = 10;
constexpr uint32_t TELEMETRY_PERIOD_MS = 2000;

constexpr uint8_t MEASUREMENT_QUEUE_LENGTH = 5;
constexpr uint8_t COMMAND_QUEUE_LENGTH = 8;

// -----------------------------------------------------------------------------
// Task configuration
// -----------------------------------------------------------------------------

constexpr uint16_t SENSOR_TASK_STACK = 4096;
constexpr uint16_t CONTROL_TASK_STACK = 4096;
constexpr uint16_t DISPLAY_TASK_STACK = 4096;
constexpr uint16_t NETWORK_TASK_STACK = 6144;
constexpr uint16_t WATCHDOG_TASK_STACK = 4096;
constexpr uint16_t CLI_TASK_STACK = 4096;

constexpr uint8_t SENSOR_TASK_PRIORITY = 3;
constexpr uint8_t CONTROL_TASK_PRIORITY = 4;
constexpr uint8_t DISPLAY_TASK_PRIORITY = 1;
constexpr uint8_t NETWORK_TASK_PRIORITY = 2;
constexpr uint8_t WATCHDOG_TASK_PRIORITY = 5;
constexpr uint8_t CLI_TASK_PRIORITY = 2;

// -----------------------------------------------------------------------------
// Watchdog
// -----------------------------------------------------------------------------

constexpr uint32_t WATCHDOG_TIMEOUT_MS = 8000;
constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 6000;
constexpr uint32_t WATCHDOG_STARTUP_GRACE_MS = 10000;
constexpr uint32_t WATCHDOG_SUPERVISOR_PERIOD_MS = 1000;

// -----------------------------------------------------------------------------
// Network / NTP
// -----------------------------------------------------------------------------

constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
constexpr uint32_t BLYNK_RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t BLYNK_CONNECT_TIMEOUT_MS = 1000;

constexpr long NTP_GMT_OFFSET_SECONDS = 0;
constexpr int NTP_DAYLIGHT_OFFSET_SECONDS = 0;
constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.nist.gov";
constexpr uint32_t NTP_CHECK_INTERVAL_MS = 1000;

// -----------------------------------------------------------------------------
// CLI
// -----------------------------------------------------------------------------

constexpr uint32_t CLI_TASK_PERIOD_MS = 20;
constexpr uint8_t CLI_BUFFER_SIZE = 40;
constexpr uint32_t SERIAL_BAUD_RATE = 115200;
