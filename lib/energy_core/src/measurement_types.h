#pragma once

#include <stdint.h>

// Where a measurement came from. The value travels with every sample so that
// downstream consumers (diagnostics, telemetry) report the source actually
// used instead of re-deriving it from a separate configuration constant.
enum class MeasurementSource : uint8_t
{
    PZEM = 0,
    SIMULATION = 1
};

// Why a sample is (not) usable. Anything other than OK means valid == false.
enum class MeasurementStatus : uint8_t
{
    NOT_INITIALIZED = 0,  // default-constructed / no sample yet (valid == false)
    OK,
    SENSOR_ERROR,     // meter did not answer or returned NaN/Inf
    OUT_OF_RANGE,     // finite but outside plausible bounds
    INCONSISTENT      // finite and in range but physically contradictory
};

// Units: volts (RMS), amperes (RMS), watts (active power as reported by the
// meter), kWh (meter accumulator), volt-amperes (derived V*I), power factor
// (dimensionless, 0..1). timestamp is millis() at the time of the read.
struct Measurement
{
    float voltage;
    float current;
    float power;
    float energy;
    float apparentPower;
    float powerFactor;
    bool valid;
    MeasurementStatus status;
    MeasurementSource source;
    uint32_t timestamp;
};

const char* measurementSourceName(MeasurementSource source);
const char* measurementStatusName(MeasurementStatus status);
