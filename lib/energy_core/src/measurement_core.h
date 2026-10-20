#pragma once

#include "measurement_types.h"

// Raw values as returned by the PZEM driver. Any field may be NaN when the
// meter did not answer.
struct RawPzemReading
{
    float voltage;
    float current;
    float power;
    float energy;
    float powerFactor;
};

// Plausibility bounds. These are sanity limits chosen to be wider than the
// commonly published PZEM-004T v3.0 ranges (80-260 V, 0-100 A with the
// external CT, up to about 23 kW, 0-9999.99 kWh). They are NOT calibration
// data or accuracy claims; verify against your module's datasheet.
struct MeasurementLimits
{
    float maxVoltage;
    float maxCurrent;
    float maxPower;
    float maxEnergyKwh;
    // Active power may not exceed apparent power by more than
    // (ratio * S + margin). Absorbs rounding/resolution and meter tolerance.
    float powerOverApparentRatio;
    float powerOverApparentMarginW;
};

MeasurementLimits defaultMeasurementLimits();

// Validates a raw reading and derives apparent power (V*I). Invalid readings
// are returned with all numeric fields zeroed and valid == false so that
// NaN never propagates into JSON/LCD and never looks like a legitimate zero:
// callers must check .valid / .status.
Measurement buildMeasurement(
    const RawPzemReading& raw,
    MeasurementSource source,
    uint32_t timestampMs,
    const MeasurementLimits& limits);

Measurement makeInvalidMeasurement(
    MeasurementSource source,
    MeasurementStatus status,
    uint32_t timestampMs);

// Deterministic 15 s profile: 5 s at 40 W, 5 s at 500 W, 5 s at 600 W.
// Always labelled MeasurementSource::SIMULATION.
Measurement simulationSample(uint32_t nowMs);

// Rollover-safe age check (unsigned subtraction).
bool measurementIsFresh(const Measurement& m, uint32_t nowMs, uint32_t maxAgeMs);
