#include "protection.h"

#include "config.h"

namespace
{
    SemaphoreHandle_t protectionMutex = nullptr;

    SystemState systemState = SystemState::INIT;

    FaultManager faultManager{
        FaultCode::NONE,
        FaultCode::NONE,
        0,
        0,
        0,
        false,
        false
    };

    bool faultTimerActive = false;
    uint32_t faultStartTime = 0;

    bool lock(TickType_t timeout = pdMS_TO_TICKS(50))
    {
        return protectionMutex != nullptr &&
               xSemaphoreTake(protectionMutex, timeout) == pdTRUE;
    }

    void unlock()
    {
        xSemaphoreGive(protectionMutex);
    }

    void raiseFaultLocked(FaultCode fault)
    {
        if (fault == FaultCode::NONE)
        {
            return;
        }

        if (faultManager.active &&
            faultManager.activeFault == fault)
        {
            return;
        }

        const uint32_t now = millis();

        faultManager.activeFault = fault;
        faultManager.lastFault = fault;
        faultManager.activeSince = now;
        faultManager.lastFaultTime = now;
        faultManager.totalFaults++;
        faultManager.active = true;
        faultManager.acknowledged = false;

        systemState = SystemState::FAULT;
    }
}

void protectionBegin()
{
    protectionMutex = xSemaphoreCreateMutex();

    if (protectionMutex == nullptr)
    {
        Serial.println("[FATAL] Protection mutex creation failed");
        while (true)
        {
            delay(1000);
        }
    }
}

ProtectionResult protectionUpdate(const Measurement& measurement)
{
    ProtectionResult result{};

    if (!lock())
    {
        result.stateChanged = false;
        result.faultChanged = false;
        result.state = SystemState::FAULT;
        result.fault = FaultCode::SENSOR_INVALID;
        return result;
    }

    result.previousState = systemState;
    result.previousFault = faultManager.activeFault;

    // Faults are latched. An explicit reset is required.
    if (systemState == SystemState::FAULT)
    {
        result.state = systemState;
        result.fault = faultManager.activeFault;
        unlock();
        return result;
    }

    if (!measurement.valid)
    {
        faultTimerActive = false;
        raiseFaultLocked(FaultCode::SENSOR_INVALID);
    }
    else if (measurement.voltage <= 10.0f)
    {
        faultTimerActive = false;
        systemState = SystemState::INIT;
    }
    else if (measurement.power > POWER_FAULT_THRESHOLD_W)
    {
        if (!faultTimerActive)
        {
            faultTimerActive = true;
            faultStartTime = millis();
            Serial.println("[PROTECTION] Overpower confirmation timer started");
        }

        if (millis() - faultStartTime >= FAULT_CONFIRM_TIME_MS)
        {
            faultTimerActive = false;
            raiseFaultLocked(FaultCode::OVERPOWER);
        }
        else
        {
            systemState = SystemState::WARNING;
        }
    }
    else if (measurement.power > POWER_WARNING_THRESHOLD_W)
    {
        faultTimerActive = false;
        systemState = SystemState::WARNING;
    }
    else
    {
        faultTimerActive = false;
        systemState = SystemState::NORMAL;
    }

    result.state = systemState;
    result.fault = faultManager.activeFault;
    result.stateChanged = result.state != result.previousState;
    result.faultChanged = result.fault != result.previousFault;

    unlock();
    return result;
}

bool protectionReset()
{
    if (!lock())
    {
        return false;
    }

    if (!faultManager.active)
    {
        unlock();
        return false;
    }

    Serial.println("[PROTECTION] Fault reset requested");

    faultManager.activeFault = FaultCode::NONE;
    faultManager.active = false;
    faultManager.acknowledged = false;
    faultManager.activeSince = 0;

    faultTimerActive = false;
    faultStartTime = 0;
    systemState = SystemState::INIT;

    unlock();

    Serial.println("[PROTECTION] Fault reset complete");
    return true;
}

bool protectionAcknowledge()
{
    if (!lock())
    {
        return false;
    }

    if (!faultManager.active)
    {
        unlock();
        return false;
    }

    faultManager.acknowledged = true;
    unlock();

    Serial.println("[FAULT] Fault acknowledged");
    return true;
}

ProtectionSnapshot protectionGetSnapshot()
{
    ProtectionSnapshot snapshot{};

    if (!lock())
    {
        snapshot.state = SystemState::FAULT;
        snapshot.fault.activeFault = FaultCode::SENSOR_INVALID;
        snapshot.fault.active = true;
        return snapshot;
    }

    snapshot.state = systemState;
    snapshot.fault = faultManager;
    snapshot.faultTimerActive = faultTimerActive;
    snapshot.faultTimerAgeMs =
        faultTimerActive ? millis() - faultStartTime : 0;

    unlock();
    return snapshot;
}

const char* systemStateName(SystemState state)
{
    switch (state)
    {
        case SystemState::INIT:    return "INIT";
        case SystemState::NORMAL:  return "NORMAL";
        case SystemState::WARNING: return "WARNING";
        case SystemState::FAULT:   return "FAULT";
        default:                   return "UNKNOWN";
    }
}

const char* faultCodeName(FaultCode fault)
{
    switch (fault)
    {
        case FaultCode::NONE:          return "NONE";
        case FaultCode::SENSOR_INVALID:return "SENSOR_INVALID";
        case FaultCode::OVERPOWER:     return "OVERPOWER";
        default:                       return "UNKNOWN";
    }
}
