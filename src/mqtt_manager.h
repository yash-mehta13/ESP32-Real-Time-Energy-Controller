#pragma once

#include <Arduino.h>

#include "diagnostics.h"
#include "measurement.h"
#include "protection.h"

void mqttManagerBegin(QueueHandle_t commandQueue);
void mqttManagerSetTimeSynchronized(bool synchronized);
bool mqttManagerConnect();
void mqttManagerLoop();
bool mqttManagerIsConnected();

bool mqttManagerPublishTelemetry(
    const Measurement& measurement,
    const ProtectionSnapshot& protection,
    bool relayOn
);
bool mqttManagerPublishState(SystemState state);
bool mqttManagerPublishFault(const ProtectionSnapshot& protection);
