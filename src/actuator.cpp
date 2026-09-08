#include "actuator.h"

#include "config.h"

namespace
{
    bool relayOn = false;

    void writeRelay(bool on)
    {
        relayOn = on;
        digitalWrite(RELAY_PIN, on ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
    }
}

void actuatorBegin()
{
    // Set the GPIO output level before enabling the output driver.
    digitalWrite(RELAY_PIN, RELAY_OFF_LEVEL);
    pinMode(RELAY_PIN, OUTPUT);

    pinMode(LED_NORMAL_PIN, OUTPUT);
    pinMode(LED_ALERT_PIN, OUTPUT);

    digitalWrite(LED_NORMAL_PIN, LOW);
    digitalWrite(LED_ALERT_PIN, LOW);

    relayOn = false;
}

void actuatorApplyState(SystemState state)
{
    switch (state)
    {
        case SystemState::INIT:
            writeRelay(false);
            digitalWrite(LED_NORMAL_PIN, LOW);
            digitalWrite(LED_ALERT_PIN, LOW);
            break;

        case SystemState::NORMAL:
            writeRelay(true);
            digitalWrite(LED_NORMAL_PIN, HIGH);
            digitalWrite(LED_ALERT_PIN, LOW);
            break;

        case SystemState::WARNING:
            writeRelay(true);
            digitalWrite(LED_NORMAL_PIN, LOW);
            digitalWrite(LED_ALERT_PIN, HIGH);
            break;

        case SystemState::FAULT:
            writeRelay(false);
            digitalWrite(LED_NORMAL_PIN, LOW);
            digitalWrite(LED_ALERT_PIN, HIGH);
            break;

        default:
            writeRelay(false);
            digitalWrite(LED_NORMAL_PIN, LOW);
            digitalWrite(LED_ALERT_PIN, LOW);
            break;
    }
}

bool actuatorRelayIsOn()
{
    return relayOn;
}
