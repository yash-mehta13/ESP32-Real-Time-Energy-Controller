#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "measurement.h"
#include "protection.h"

enum class CommandType : uint8_t
{
    NONE = 0,
    HELP,
    STATUS,
    MEASURE,
    FAULT,
    WATCHDOG,
    TASKS,
    UPTIME,
    RESET,
    ACK,
    VERSION,
    REMOTE_RESET,
    REMOTE_ACK
};

struct CliCommand
{
    CommandType type;
};

struct NetworkSnapshot
{
    bool wifiConnected;
    bool timeSynchronized;
    bool blynkConnected;
    bool mqttConnected;
    int32_t rssi;
    IPAddress ip;
};

void diagnosticsBegin();
CommandType diagnosticsParseCommand(const char* command);

void diagnosticsPrintHelp();
void diagnosticsPrintMeasurement(const Measurement& measurement);
void diagnosticsPrintFault(const ProtectionSnapshot& protection);
void diagnosticsPrintStatus(
    const Measurement& measurement,
    const ProtectionSnapshot& protection,
    const NetworkSnapshot& network
);
void diagnosticsPrintNetwork(const NetworkSnapshot& network);
void diagnosticsPrintUptime();
void diagnosticsPrintVersion();
void diagnosticsPrintTasks();
void diagnosticsPrintWatchdog();

void diagnosticsSetTaskHandles(
    TaskHandle_t sensor,
    TaskHandle_t control,
    TaskHandle_t display,
    TaskHandle_t network,
    TaskHandle_t watchdog,
    TaskHandle_t cli
);
