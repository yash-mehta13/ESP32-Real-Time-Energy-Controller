#include "protection_core.h"

ProtectionCore::ProtectionCore(const ProtectionConfig& config)
    : config_(config),
      state_(SystemState::INIT),
      fault_{FaultCode::NONE, FaultCode::NONE, 0, 0, 0, false, false},
      timerActive_(false),
      timerStart_(0),
      haveMeasurement_(false),
      last_{},
      lastSampleMs_(0),
      awaitingFirstSample_(true),
      rearmMs_(0),
      lastReset_(ResetResult::NONE)
{
}

void ProtectionCore::begin(uint32_t nowMs)
{
    state_ = SystemState::INIT;
    fault_ = FaultManager{FaultCode::NONE, FaultCode::NONE, 0, 0, 0, false, false};
    timerActive_ = false;
    timerStart_ = 0;
    haveMeasurement_ = false;
    last_ = Measurement{};
    lastSampleMs_ = nowMs;  // silence is measured from start-up
    awaitingFirstSample_ = true;
    rearmMs_ = nowMs;
    lastReset_ = ResetResult::NONE;
}

ProtectionResult ProtectionCore::lockFailureResult()
{
    ProtectionResult r{};
    r.stateChanged = true;   // pessimistic: never let a missing flag skip the safe action
    r.faultChanged = true;
    r.previousState = SystemState::FAULT;
    r.state = SystemState::FAULT;
    r.previousFault = FaultCode::NONE;
    r.fault = FaultCode::PROTECTION_INTERNAL;
    r.synchronizationFailed = true;
    return r;
}

ProtectionResult ProtectionCore::finish(SystemState prevState, FaultCode prevFault) const
{
    ProtectionResult r{};
    r.previousState = prevState;
    r.previousFault = prevFault;
    r.state = state_;
    r.fault = fault_.activeFault;
    r.stateChanged = r.state != prevState;
    r.faultChanged = r.fault != prevFault;
    return r;
}

void ProtectionCore::raiseFault(FaultCode fault, uint32_t nowMs)
{
    if (fault == FaultCode::NONE)
    {
        return;
    }

    // Never overwrite a latched fault: the first cause is preserved.
    if (!fault_.active)
    {
        fault_.activeFault = fault;
        fault_.lastFault = fault;
        fault_.activeSince = nowMs;
        fault_.lastFaultTime = nowMs;
        fault_.totalFaults++;
        fault_.active = true;
        fault_.acknowledged = false;
    }

    timerActive_ = false;
    state_ = SystemState::FAULT;
}

void ProtectionCore::evaluate(const Measurement& m, uint32_t nowMs)
{
    const bool fresh = static_cast<uint32_t>(nowMs - m.timestamp) <=
                       config_.maxMeasurementAgeMs;

    if (!fresh)
    {
        raiseFault(FaultCode::SENSOR_STALE, nowMs);
    }
    else if (!m.valid)
    {
        raiseFault(FaultCode::SENSOR_INVALID, nowMs);
    }
    else if (m.voltage <= config_.minOperatingVoltageV)
    {
        // No mains detected: stay in INIT (relay OFF), not a latched fault.
        // Re-arm so that mains returning onto an overload latches at once.
        timerActive_ = false;
        state_ = SystemState::INIT;
        awaitingFirstSample_ = true;
    }
    else if (m.power > config_.faultPowerW)
    {
        if (awaitingFirstSample_)
        {
            // Boot or post-reset with the hazard still present: do not
            // energise for a confirmation window, latch right away.
            raiseFault(FaultCode::OVERPOWER, nowMs);
            awaitingFirstSample_ = false;
            return;
        }

        if (!timerActive_)
        {
            timerActive_ = true;
            timerStart_ = nowMs;
        }

        if (static_cast<uint32_t>(nowMs - timerStart_) >= config_.faultConfirmMs)
        {
            raiseFault(FaultCode::OVERPOWER, nowMs);
        }
        else
        {
            state_ = SystemState::WARNING;
        }
    }
    else if (m.power > config_.warningPowerW)
    {
        timerActive_ = false;
        state_ = SystemState::WARNING;
        awaitingFirstSample_ = false;
    }
    else
    {
        timerActive_ = false;
        state_ = SystemState::NORMAL;
        awaitingFirstSample_ = false;
    }
}

ProtectionResult ProtectionCore::update(const Measurement& m, uint32_t nowMs)
{
    const SystemState prevState = state_;
    const FaultCode prevFault = fault_.activeFault;

    last_ = m;
    haveMeasurement_ = true;

    if (state_ == SystemState::FAULT)
    {
        lastSampleMs_ = nowMs;
        return finish(prevState, prevFault);  // latched
    }

    // After boot/reset ignore samples taken before the re-arm point; they
    // describe the pre-reset world. Stay in INIT (relay OFF) meanwhile.
    if (awaitingFirstSample_ &&
        static_cast<int32_t>(m.timestamp - rearmMs_) < 0)
    {
        return finish(prevState, prevFault);
    }

    lastSampleMs_ = nowMs;
    evaluate(m, nowMs);
    return finish(prevState, prevFault);
}

ProtectionResult ProtectionCore::tick(uint32_t nowMs)
{
    const SystemState prevState = state_;
    const FaultCode prevFault = fault_.activeFault;

    if (state_ != SystemState::FAULT &&
        static_cast<uint32_t>(nowMs - lastSampleMs_) > config_.maxMeasurementAgeMs)
    {
        raiseFault(FaultCode::SENSOR_STALE, nowMs);
    }

    return finish(prevState, prevFault);
}

ResetResult ProtectionCore::requestReset(uint32_t nowMs)
{
    ResetResult result = ResetResult::ACCEPTED;

    if (!fault_.active)
    {
        result = ResetResult::REJECTED_NO_ACTIVE_FAULT;
    }
    else if (!haveMeasurement_)
    {
        result = ResetResult::REJECTED_NO_MEASUREMENT;
    }
    else if (static_cast<uint32_t>(nowMs - last_.timestamp) >
             config_.maxMeasurementAgeMs)
    {
        result = ResetResult::REJECTED_STALE_MEASUREMENT;
    }
    else if (!last_.valid)
    {
        result = ResetResult::REJECTED_INVALID_MEASUREMENT;
    }
    else if (last_.power > config_.faultPowerW)
    {
        result = ResetResult::REJECTED_UNSAFE_CONDITION;
    }

    lastReset_ = result;

    if (result == ResetResult::ACCEPTED)
    {
        fault_.activeFault = FaultCode::NONE;
        fault_.active = false;
        fault_.acknowledged = false;
        fault_.activeSince = 0;
        timerActive_ = false;
        timerStart_ = 0;
        state_ = SystemState::INIT;  // relay OFF until a fresh sample
        awaitingFirstSample_ = true;
        rearmMs_ = nowMs;
        lastSampleMs_ = nowMs;       // restart the silence clock
    }

    return result;
}

bool ProtectionCore::acknowledge()
{
    // Acknowledgement only records that an operator saw the fault. It must
    // not change state or release the latch.
    if (!fault_.active)
    {
        return false;
    }

    fault_.acknowledged = true;
    return true;
}

ProtectionResult ProtectionCore::raiseInternalFault(uint32_t nowMs)
{
    const SystemState prevState = state_;
    const FaultCode prevFault = fault_.activeFault;
    raiseFault(FaultCode::PROTECTION_INTERNAL, nowMs);
    return finish(prevState, prevFault);
}

ProtectionSnapshot ProtectionCore::snapshot(uint32_t nowMs) const
{
    ProtectionSnapshot s{};
    s.state = state_;
    s.fault = fault_;
    s.faultTimerActive = timerActive_;
    s.faultTimerAgeMs = timerActive_ ? static_cast<uint32_t>(nowMs - timerStart_) : 0;
    s.lastResetResult = lastReset_;
    return s;
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
        case FaultCode::NONE:                return "NONE";
        case FaultCode::SENSOR_INVALID:      return "SENSOR_INVALID";
        case FaultCode::OVERPOWER:           return "OVERPOWER";
        case FaultCode::SENSOR_STALE:        return "SENSOR_STALE";
        case FaultCode::PROTECTION_INTERNAL: return "PROTECTION_INTERNAL";
        default:                             return "UNKNOWN";
    }
}

const char* resetResultName(ResetResult result)
{
    switch (result)
    {
        case ResetResult::NONE:                         return "NONE";
        case ResetResult::ACCEPTED:                     return "ACCEPTED";
        case ResetResult::REJECTED_NO_ACTIVE_FAULT:     return "REJECTED_NO_ACTIVE_FAULT";
        case ResetResult::REJECTED_NO_MEASUREMENT:      return "REJECTED_NO_MEASUREMENT";
        case ResetResult::REJECTED_STALE_MEASUREMENT:   return "REJECTED_STALE_MEASUREMENT";
        case ResetResult::REJECTED_INVALID_MEASUREMENT: return "REJECTED_INVALID_MEASUREMENT";
        case ResetResult::REJECTED_UNSAFE_CONDITION:    return "REJECTED_UNSAFE_CONDITION";
        case ResetResult::REJECTED_BUSY:                return "REJECTED_BUSY";
        default:                                        return "UNKNOWN";
    }
}
