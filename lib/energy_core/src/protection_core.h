#pragma once

#include <stdint.h>

#include "measurement_types.h"

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
    SENSOR_INVALID,       // sample reported invalid (see MeasurementStatus)
    OVERPOWER,            // power > fault threshold for >= confirmation time
    SENSOR_STALE,         // no usable sample within the freshness limit
    PROTECTION_INTERNAL   // protection could not run (e.g. lock failure)
};

enum class ResetResult : uint8_t
{
    NONE = 0,                  // no reset requested yet
    ACCEPTED,
    REJECTED_NO_ACTIVE_FAULT,
    REJECTED_NO_MEASUREMENT,
    REJECTED_STALE_MEASUREMENT,
    REJECTED_INVALID_MEASUREMENT,
    REJECTED_UNSAFE_CONDITION, // power still above the fault threshold
    REJECTED_BUSY              // protection state could not be locked
};

struct ProtectionConfig
{
    float warningPowerW;          // power > this -> WARNING
    float faultPowerW;            // power > this -> confirmation timer
    float minOperatingVoltageV;   // voltage <= this -> INIT (no mains)
    uint32_t faultConfirmMs;      // overpower must persist this long
    uint32_t maxMeasurementAgeMs; // older samples are not trusted
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
    ResetResult lastResetResult;
};

struct ProtectionResult
{
    bool stateChanged;
    bool faultChanged;
    SystemState previousState;
    SystemState state;
    FaultCode previousFault;
    FaultCode fault;
    // True when the result was synthesised because the protection state could
    // not be accessed. state is then FAULT and callers must drive the safe
    // output regardless of the change flags.
    bool synchronizationFailed;
};

// Pure, single-threaded protection state machine. Time is injected by the
// caller (millis()-style, 32-bit, rollover-safe). Not thread-safe: the
// firmware wrapper (src/protection.cpp) serialises access.
//
// Invariants (each covered by test/test_native):
//  - FAULT is latched; only requestReset() can leave it.
//  - requestReset() never energises anything: it moves to INIT, which maps
//    to relay OFF, and is refused while the cause is still present.
//  - After begin() or an accepted reset the first sample may not start the
//    confirmation window: an overpower sample re-latches immediately.
//  - Invalid or stale input latches a fault; it never counts as "power = 0".
class ProtectionCore
{
public:
    explicit ProtectionCore(const ProtectionConfig& config);

    void begin(uint32_t nowMs);

    ProtectionResult update(const Measurement& m, uint32_t nowMs);
    // Call when no new sample is available so that silence is detected.
    ProtectionResult tick(uint32_t nowMs);

    ResetResult requestReset(uint32_t nowMs);
    bool acknowledge();
    ProtectionResult raiseInternalFault(uint32_t nowMs);

    // Fail-closed result used when the protection state cannot be locked.
    static ProtectionResult lockFailureResult();

    SystemState state() const { return state_; }
    ProtectionSnapshot snapshot(uint32_t nowMs) const;

private:
    ProtectionResult finish(SystemState prevState, FaultCode prevFault) const;
    void raiseFault(FaultCode fault, uint32_t nowMs);
    void evaluate(const Measurement& m, uint32_t nowMs);

    ProtectionConfig config_;
    SystemState state_;
    FaultManager fault_;
    bool timerActive_;
    uint32_t timerStart_;

    bool haveMeasurement_;
    Measurement last_;
    uint32_t lastSampleMs_;     // when the core last saw a sample
    bool awaitingFirstSample_;  // boot/reset re-arm gate
    uint32_t rearmMs_;
    ResetResult lastReset_;
};

const char* systemStateName(SystemState state);
const char* faultCodeName(FaultCode fault);
const char* resetResultName(ResetResult result);
