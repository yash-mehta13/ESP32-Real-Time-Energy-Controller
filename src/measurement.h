#pragma once

#include <Arduino.h>

enum class MeasurementSource : uint8_t
{
    PZEM = 0,
    SIMULATION = 1
};

struct Measurement
{
    float voltage;
    float current;
    float power;
    float energy;
    float apparentPower;
    float powerFactor;
    bool valid;
    uint32_t timestamp;
};

void measurementBegin();
Measurement readMeasurement();
const char* measurementSourceName();
