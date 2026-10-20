#include "protection.h"

#include <atomic>

#include "config.h"

namespace
{
    SemaphoreHandle_t protectionMutex = nullptr;

    ProtectionConfig makeConfig()
    {
        ProtectionConfig c{};
        c.warningPowerW = POWER_WARNING_THRESHOLD_W;
        c.faultPowerW = POWER_FAULT_THRESHOLD_W;
        c.minOperatingVoltageV = MIN_OPERATING_VOLTAGE_V;
        c.faultConfirmMs = FAULT_CONFIRM_TIME_MS;
        c.maxMeasurementAgeMs = MAX_MEASUREMENT_AGE_MS;
        return c;
    }

    ProtectionCore core(makeConfig());

    // Set when any call failed to take the mutex. The next call that does get
    // the lock latches PROTECTION_INTERNAL, so a lock failure is never merely
    // a transient return value.
    std::atomic<bool> internalFaultPending{false};

    bool lock()
    {
        return protectionMutex != nullptr &&
               xSemaphoreTake(protectionMutex, pdMS_TO_TICKS(PROTECTION_LOCK_TIMEOUT_MS)) == pdTRUE;
    }

    void unlock()
    {
        xSemaphoreGive(protectionMutex);
    }

    void applyPendingInternalFault(uint32_t now)
    {
        if (internalFaultPending.exchange(false))
        {
            core.raiseInternalFault(now);
        }
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

    core.begin(millis());
}

ProtectionResult protectionUpdate(const Measurement& measurement)
{
    if (!lock())
    {
        internalFaultPending = true;
        return ProtectionCore::lockFailureResult();
    }

    const uint32_t now = millis();
    applyPendingInternalFault(now);
    const ProtectionResult result = core.update(measurement, now);
    unlock();
    return result;
}

ProtectionResult protectionTick()
{
    if (!lock())
    {
        internalFaultPending = true;
        return ProtectionCore::lockFailureResult();
    }

    const uint32_t now = millis();
    applyPendingInternalFault(now);
    const ProtectionResult result = core.tick(now);
    unlock();
    return result;
}

ResetResult protectionReset()
{
    if (!lock())
    {
        return ResetResult::REJECTED_BUSY;
    }

    const uint32_t now = millis();
    // A pending internal fault is applied first, so a reset that raced a
    // lock failure is judged against (and cannot erase) that fault.
    applyPendingInternalFault(now);
    const ResetResult result = core.requestReset(now);
    unlock();

    Serial.printf("[PROTECTION] Reset request: %s\n", resetResultName(result));
    return result;
}

bool protectionAcknowledge()
{
    if (!lock())
    {
        return false;
    }

    const bool ok = core.acknowledge();
    unlock();

    Serial.println(ok ? "[FAULT] Fault acknowledged"
                      : "[FAULT] Acknowledge ignored: no active fault");
    return ok;
}

ProtectionSnapshot protectionGetSnapshot()
{
    ProtectionSnapshot snapshot{};

    if (!lock())
    {
        snapshot.state = SystemState::FAULT;
        snapshot.fault.activeFault = FaultCode::PROTECTION_INTERNAL;
        snapshot.fault.active = true;
        return snapshot;
    }

    snapshot = core.snapshot(millis());
    unlock();
    return snapshot;
}
