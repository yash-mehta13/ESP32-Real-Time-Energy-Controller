#pragma once

#include <stddef.h>
#include <stdint.h>

#include "measurement_types.h"
#include "protection_core.h"

constexpr int TELEMETRY_SCHEMA_VERSION = 2;

struct TelemetryInput
{
    const char* deviceId;
    const char* firmwareVersion;
    Measurement measurement;
    ProtectionSnapshot protection;
    uint32_t uptimeMs;           // millis() since boot; wraps after ~49.7 days
    uint32_t measurementAgeMs;
    bool relayCommandedOn;       // what the firmware commanded, not a contact reading
    bool relayOutputEnabled;     // false in simulation builds
};

// Writes one JSON object into `out`. Returns the length (excluding NUL) or
// -1 if the buffer is too small / arguments are invalid; on failure `out`
// holds an empty string. Strings are escaped; non-finite floats become null.
int buildTelemetryJson(char* out, size_t outSize, const TelemetryInput& in);
