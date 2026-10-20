#pragma once

#include <Arduino.h>

#include "measurement_core.h"

void measurementBegin();
Measurement readMeasurement();
MeasurementSource activeMeasurementSource();
