#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "protection_core.h"

// Thread-safe wrapper around ProtectionCore. Every call that cannot take the
// protection mutex fails closed (see protection.cpp).
void protectionBegin();
ProtectionResult protectionUpdate(const Measurement& measurement);
ProtectionResult protectionTick();
ResetResult protectionReset();
bool protectionAcknowledge();
ProtectionSnapshot protectionGetSnapshot();
