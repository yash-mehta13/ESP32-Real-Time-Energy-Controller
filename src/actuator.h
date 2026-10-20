#pragma once

#include "protection.h"

// The ONLY module that writes the relay and indicator GPIOs.
void actuatorBegin();
void actuatorApplyState(SystemState state);
// Applies the outputs for a protection result. Call on every result, not
// only when a change flag is set.
void actuatorApplyResult(const ProtectionResult& result);

// What the firmware commanded. There is no contact feedback, so this is NOT
// proof that the physical contacts are open or closed.
bool actuatorRelayCommandedOn();
// False in simulation builds: the relay pin is held at its OFF level.
bool actuatorRelayOutputEnabled();
// Number of times the relay pin read back a level different from the one
// written (GPIO-level check only).
uint32_t actuatorReadbackMismatches();
