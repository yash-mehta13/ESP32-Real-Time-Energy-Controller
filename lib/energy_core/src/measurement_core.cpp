#include "measurement_core.h"

#include <math.h>

const char* measurementSourceName(MeasurementSource source)
{
    switch (source)
    {
        case MeasurementSource::PZEM:       return "PZEM";
        case MeasurementSource::SIMULATION: return "SIMULATION";
        default:                            return "UNKNOWN";
    }
}

const char* measurementStatusName(MeasurementStatus status)
{
    switch (status)
    {
        case MeasurementStatus::OK:              return "OK";
        case MeasurementStatus::NOT_INITIALIZED: return "NOT_INITIALIZED";
        case MeasurementStatus::SENSOR_ERROR:    return "SENSOR_ERROR";
        case MeasurementStatus::OUT_OF_RANGE:    return "OUT_OF_RANGE";
        case MeasurementStatus::INCONSISTENT:    return "INCONSISTENT";
        default:                                 return "UNKNOWN";
    }
}

MeasurementLimits defaultMeasurementLimits()
{
    MeasurementLimits l{};
    l.maxVoltage = 300.0f;
    l.maxCurrent = 100.0f;
    l.maxPower = 25000.0f;
    l.maxEnergyKwh = 10000.0f;
    l.powerOverApparentRatio = 0.10f;
    l.powerOverApparentMarginW = 2.0f;
    return l;
}

Measurement makeInvalidMeasurement(
    MeasurementSource source,
    MeasurementStatus status,
    uint32_t timestampMs)
{
    Measurement m{};
    m.valid = false;
    m.status = status;
    m.source = source;
    m.timestamp = timestampMs;
    return m;
}

Measurement buildMeasurement(
    const RawPzemReading& raw,
    MeasurementSource source,
    uint32_t timestampMs,
    const MeasurementLimits& limits)
{
    // Power factor is allowed to be NaN only if the meter did not answer at
    // all, which is already covered by the other fields; treat it the same.
    if (!isfinite(raw.voltage) || !isfinite(raw.current) ||
        !isfinite(raw.power) || !isfinite(raw.energy) ||
        !isfinite(raw.powerFactor))
    {
        return makeInvalidMeasurement(
            source, MeasurementStatus::SENSOR_ERROR, timestampMs);
    }

    if (raw.voltage < 0.0f || raw.voltage > limits.maxVoltage ||
        raw.current < 0.0f || raw.current > limits.maxCurrent ||
        raw.power < 0.0f || raw.power > limits.maxPower ||
        raw.energy < 0.0f || raw.energy > limits.maxEnergyKwh ||
        raw.powerFactor < 0.0f || raw.powerFactor > 1.0f)
    {
        return makeInvalidMeasurement(
            source, MeasurementStatus::OUT_OF_RANGE, timestampMs);
    }

    // V * I is apparent power (VA) for AC. It is deliberately NOT used as
    // active power: active power is the meter's own measurement.
    const float apparent = raw.voltage * raw.current;

    if (raw.power > apparent * (1.0f + limits.powerOverApparentRatio) +
                        limits.powerOverApparentMarginW)
    {
        return makeInvalidMeasurement(
            source, MeasurementStatus::INCONSISTENT, timestampMs);
    }

    Measurement m{};
    m.voltage = raw.voltage;
    m.current = raw.current;
    m.power = raw.power;
    m.energy = raw.energy;
    m.apparentPower = apparent;
    m.powerFactor = raw.powerFactor;
    m.valid = true;
    m.status = MeasurementStatus::OK;
    m.source = source;
    m.timestamp = timestampMs;
    return m;
}

Measurement simulationSample(uint32_t nowMs)
{
    RawPzemReading raw{};
    const uint32_t phase = nowMs % 15000UL;

    raw.voltage = 230.0f;
    raw.energy = 0.0f;

    if (phase < 5000UL)
    {
        raw.current = 0.20f;
        raw.power = 40.0f;
    }
    else if (phase < 10000UL)
    {
        raw.current = 2.17f;
        raw.power = 500.0f;
    }
    else
    {
        raw.current = 2.60f;
        raw.power = 600.0f;
    }

    const float apparent = raw.voltage * raw.current;
    raw.powerFactor = apparent > 0.0f ? raw.power / apparent : 0.0f;
    if (raw.powerFactor > 1.0f)
    {
        raw.powerFactor = 1.0f;
    }

    return buildMeasurement(
        raw, MeasurementSource::SIMULATION, nowMs, defaultMeasurementLimits());
}

bool measurementIsFresh(const Measurement& m, uint32_t nowMs, uint32_t maxAgeMs)
{
    return static_cast<uint32_t>(nowMs - m.timestamp) <= maxAgeMs;
}
