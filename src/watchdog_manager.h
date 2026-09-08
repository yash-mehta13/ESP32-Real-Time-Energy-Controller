#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

enum HeartbeatId : uint8_t
{
    HEARTBEAT_SENSOR = 0,
    HEARTBEAT_CONTROL,
    HEARTBEAT_DISPLAY,
    HEARTBEAT_NETWORK,
    HEARTBEAT_WATCHDOG,
    HEARTBEAT_CLI,
    HEARTBEAT_COUNT
};

bool watchdogManagerBegin();
bool watchdogManagerRegisterCurrentTask(const char* taskName);
void watchdogManagerHeartbeat(HeartbeatId id);
uint32_t watchdogManagerHeartbeatAge(HeartbeatId id);
const char* watchdogManagerHeartbeatName(HeartbeatId id);

bool watchdogManagerHealthy(uint32_t timeoutMs);
void watchdogManagerPrintStatus(uint32_t timeoutMs);
