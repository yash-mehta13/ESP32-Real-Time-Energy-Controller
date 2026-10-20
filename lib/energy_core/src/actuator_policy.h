#pragma once

#include <stdint.h>

#include "protection_core.h"

// Whether the relay GPIO may ever be driven to its "ON" level in this build.
// SIMULATION builds use HOLD_OFF so simulated power values can never close a
// real relay; the relay pin is held at its OFF level instead.
// Member names deliberately avoid DISABLED/ENABLED: the arduino-esp32 core
// defines DISABLED as a macro (esp32-hal-gpio.h), which breaks the enum.
enum class RelayOutputMode : uint8_t
{
    HOLD_OFF = 0,   // relay pin always driven to its OFF level
    FOLLOW_STATE    // relay pin follows the protection state
};

struct OutputCommand
{
    bool relayCommandedOn;
    bool ledNormal;
    bool ledAlert;
};

// State -> intended outputs. Any state outside the explicitly permitted
// operating states (NORMAL, WARNING) maps to relay OFF.
OutputCommand outputsForState(SystemState state);

// What the relay pin should actually be driven to, given the build mode.
bool relayPinDrivenOn(bool commandedOn, RelayOutputMode mode);

// Maps "driven on" to a GPIO level using the configured polarity.
int relayLevel(bool drivenOn, int onLevel, int offLevel);

// Outputs for a protection result. A result flagged synchronizationFailed
// (or carrying any non-permitted state) always yields the safe outputs, and
// callers apply this on EVERY result, not only when change flags are set.
OutputCommand outputsForResult(const ProtectionResult& result);
