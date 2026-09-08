#pragma once

#include <Arduino.h>

#include "measurement.h"

enum class SystemState : uint8_t
{
    INIT = 0,
    NORMAL,
    WARNING,
    FAULT
};

enum class FaultCode : uint8_t
{
    NONE = 0,
    SENSOR_INVALID,
    OVERPOWER
};

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

struct ProtectionSnapshot
{
    SystemState state;
    FaultManager fault;
    bool faultTimerActive;
    uint32_t faultTimerAgeMs;
};

struct ProtectionResult
{
    bool stateChanged;
    bool faultChanged;
    SystemState previousState;
    SystemState state;
    FaultCode previousFault;
    FaultCode fault;
};

void protectionBegin();
ProtectionResult protectionUpdate(const Measurement& measurement);
bool protectionReset();
bool protectionAcknowledge();
ProtectionSnapshot protectionGetSnapshot();

const char* systemStateName(SystemState state);
const char* faultCodeName(FaultCode fault);
