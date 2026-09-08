#include "diagnostics.h"

#include <esp_system.h>

#include "actuator.h"
#include "config.h"
#include "watchdog_manager.h"

namespace
{
    TaskHandle_t taskHandles[6] = {nullptr};

    const char* taskNames[6] =
    {
        "SensorTask",
        "ControlTask",
        "DisplayTask",
        "NetworkTask",
        "WatchdogTask",
        "CliTask"
    };
}

void diagnosticsBegin()
{
    Serial.println("[DIAGNOSTICS] CLI initialized");
}

CommandType diagnosticsParseCommand(const char* command)
{
    if (strcmp(command, "help") == 0) return CommandType::HELP;
    if (strcmp(command, "status") == 0) return CommandType::STATUS;
    if (strcmp(command, "measure") == 0) return CommandType::MEASURE;
    if (strcmp(command, "fault") == 0) return CommandType::FAULT;
    if (strcmp(command, "watchdog") == 0) return CommandType::WATCHDOG;
    if (strcmp(command, "tasks") == 0) return CommandType::TASKS;
    if (strcmp(command, "uptime") == 0) return CommandType::UPTIME;
    if (strcmp(command, "reset") == 0) return CommandType::RESET;
    if (strcmp(command, "ack") == 0) return CommandType::ACK;
    if (strcmp(command, "version") == 0) return CommandType::VERSION;
    return CommandType::NONE;
}

void diagnosticsPrintHelp()
{
    Serial.println();
    Serial.println("========================================");
    Serial.println("       ESP32 ENERGY CONTROLLER CLI");
    Serial.println("========================================");
    Serial.println("help       - Show available commands");
    Serial.println("status     - Complete system status");
    Serial.println("measure    - Latest measurement");
    Serial.println("fault      - Fault manager status");
    Serial.println("watchdog   - Watchdog/heartbeat status");
    Serial.println("tasks      - RTOS task status");
    Serial.println("uptime     - System uptime");
    Serial.println("reset      - Reset active fault");
    Serial.println("ack        - Acknowledge active fault");
    Serial.println("version    - Firmware information");
    Serial.println("========================================");
}

void diagnosticsPrintMeasurement(const Measurement& m)
{
    Serial.println();
    Serial.println("=========== MEASUREMENT ================");
    Serial.printf("Source           : %s\n", measurementSourceName());
    Serial.printf("Validity         : %s\n", m.valid ? "VALID" : "INVALID");
    Serial.printf("Voltage          : %.2f V\n", m.voltage);
    Serial.printf("Current          : %.3f A\n", m.current);
    Serial.printf("Power            : %.2f W\n", m.power);
    Serial.printf("Energy           : %.3f kWh\n", m.energy);
    Serial.printf("Apparent Power   : %.2f VA\n", m.apparentPower);
    Serial.printf("Power Factor     : %.3f\n", m.powerFactor);
    Serial.printf("Timestamp        : %lu ms\n", (unsigned long)m.timestamp);
    Serial.printf("Measurement Age  : %lu ms\n",
                  (unsigned long)(millis() - m.timestamp));
    Serial.println("========================================");
}

void diagnosticsPrintFault(const ProtectionSnapshot& p)
{
    Serial.println();
    Serial.println("============== FAULT ==================");
    Serial.printf("Active Fault     : %s\n", faultCodeName(p.fault.activeFault));
    Serial.printf("Last Fault       : %s\n", faultCodeName(p.fault.lastFault));
    Serial.printf("Active           : %s\n", p.fault.active ? "YES" : "NO");
    Serial.printf("Acknowledged     : %s\n",
                  p.fault.acknowledged ? "YES" : "NO");
    Serial.printf("Total Faults     : %lu\n",
                  (unsigned long)p.fault.totalFaults);
    Serial.printf("State            : %s\n", systemStateName(p.state));

    if (p.fault.active)
    {
        Serial.printf("Active Duration  : %lu ms\n",
                      (unsigned long)(millis() - p.fault.activeSince));
    }

    Serial.println("========================================");
}

void diagnosticsPrintNetwork(const NetworkSnapshot& n)
{
    Serial.println();
    Serial.println("============ NETWORK ===================");
    Serial.printf("WiFi             : %s\n",
                  n.wifiConnected ? "CONNECTED" : "DISCONNECTED");
    Serial.printf("NTP              : %s\n",
                  n.timeSynchronized ? "SYNCED" : "NOT SYNCED");

    if (n.wifiConnected)
    {
        Serial.printf("IP Address       : %s\n", n.ip.toString().c_str());
        Serial.printf("RSSI             : %ld dBm\n", (long)n.rssi);
    }

    Serial.printf("Blynk            : %s\n",
                  n.blynkConnected ? "CONNECTED" : "DISCONNECTED");
    Serial.printf("MQTT             : %s\n",
                  n.mqttConnected ? "CONNECTED" : "DISCONNECTED");
    Serial.println("========================================");
}

void diagnosticsPrintStatus(
    const Measurement& measurement,
    const ProtectionSnapshot& protection,
    const NetworkSnapshot& network
)
{
    Serial.println();
    Serial.println("============ SYSTEM STATUS =============");
    Serial.printf("State            : %s\n",
                  systemStateName(protection.state));
    Serial.printf("Fault            : %s\n",
                  faultCodeName(protection.fault.activeFault));
    Serial.printf("Fault Active     : %s\n",
                  protection.fault.active ? "YES" : "NO");
    Serial.printf("Fault ACK        : %s\n",
                  protection.fault.acknowledged ? "YES" : "NO");
    Serial.printf("Relay            : %s\n",
                  actuatorRelayIsOn() ? "ON" : "OFF");
    Serial.printf("Power            : %.2f W\n", measurement.power);
    Serial.printf("Measurement      : %s\n",
                  measurement.valid ? "VALID" : "INVALID");
    Serial.printf("Source           : %s\n",
                  measurementSourceName());
    Serial.println("========================================");

    diagnosticsPrintNetwork(network);
}

void diagnosticsPrintUptime()
{
    const uint32_t seconds = millis() / 1000UL;
    Serial.printf(
        "Uptime: %lu d %lu h %lu m %lu s\n",
        (unsigned long)(seconds / 86400UL),
        (unsigned long)((seconds % 86400UL) / 3600UL),
        (unsigned long)((seconds % 3600UL) / 60UL),
        (unsigned long)(seconds % 60UL)
    );
}

void diagnosticsPrintVersion()
{
    Serial.println();
    Serial.println("=========== FIRMWARE ==================");
    Serial.printf("Firmware         : %s\n", FIRMWARE_NAME);
    Serial.printf("Version          : %s\n", FIRMWARE_VERSION);
    Serial.printf("Build            : %s\n", FIRMWARE_BUILD);
    Serial.printf("Source           : %s\n", measurementSourceName());
    Serial.printf("CPU              : %u MHz\n", getCpuFrequencyMhz());
    Serial.printf("Free Heap        : %lu bytes\n",
                  (unsigned long)ESP.getFreeHeap());
    Serial.printf("Minimum Free Heap: %lu bytes\n",
                  (unsigned long)ESP.getMinFreeHeap());
    Serial.println("========================================");
}

void diagnosticsPrintTasks()
{
    Serial.println();
    Serial.println("============= RTOS TASKS ===============");

    for (size_t i = 0; i < 6; ++i)
    {
        TaskHandle_t handle = taskHandles[i];

        if (handle == nullptr)
        {
            Serial.printf("%-14s : NOT CREATED\n", taskNames[i]);
            continue;
        }

        Serial.printf(
            "%-14s | state=%d | priority=%lu | stack_free=%u\n",
            taskNames[i],
            static_cast<int>(eTaskGetState(handle)),
            (unsigned long)uxTaskPriorityGet(handle),
            (unsigned)uxTaskGetStackHighWaterMark(handle)
        );
    }

    Serial.println("========================================");
}

void diagnosticsPrintWatchdog()
{
    Serial.println();
    Serial.println("=========== WATCHDOG ===================");
    Serial.printf("TWDT Timeout     : %lu ms\n",
                  (unsigned long)WATCHDOG_TIMEOUT_MS);
    Serial.printf("Heartbeat Limit  : %lu ms\n",
                  (unsigned long)HEARTBEAT_TIMEOUT_MS);

    for (uint8_t i = 0; i < HEARTBEAT_COUNT; ++i)
    {
        const uint32_t age = watchdogManagerHeartbeatAge(
            static_cast<HeartbeatId>(i)
        );

        if (age == UINT32_MAX)
            Serial.printf("%-14s : NO HEARTBEAT\n",
                          watchdogManagerHeartbeatName(
                              static_cast<HeartbeatId>(i)));
        else
            Serial.printf("%-14s : %lu ms ago\n",
                          watchdogManagerHeartbeatName(
                              static_cast<HeartbeatId>(i)),
                          (unsigned long)age);
    }

    Serial.println("========================================");
}

void diagnosticsSetTaskHandles(
    TaskHandle_t sensor,
    TaskHandle_t control,
    TaskHandle_t display,
    TaskHandle_t network,
    TaskHandle_t watchdog,
    TaskHandle_t cli
)
{
    taskHandles[0] = sensor;
    taskHandles[1] = control;
    taskHandles[2] = display;
    taskHandles[3] = network;
    taskHandles[4] = watchdog;
    taskHandles[5] = cli;
}
