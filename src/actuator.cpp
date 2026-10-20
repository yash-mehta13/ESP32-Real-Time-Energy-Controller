#include "actuator.h"

#include "actuator_policy.h"
#include "build_mode.h"
#include "config.h"

namespace
{
    bool commandedOn = false;
    uint32_t readbackMismatches = 0;

    void drive(const OutputCommand& out)
    {
        commandedOn = out.relayCommandedOn;

        // In simulation builds relayPinDrivenOn() is always false, so the
        // relay pin only ever receives its OFF level.
        const int level = relayLevel(
            relayPinDrivenOn(commandedOn, kRelayOutputMode),
            RELAY_ON_LEVEL, RELAY_OFF_LEVEL);

        digitalWrite(RELAY_PIN, level);
        digitalWrite(LED_NORMAL_PIN, out.ledNormal ? HIGH : LOW);
        digitalWrite(LED_ALERT_PIN, out.ledAlert ? HIGH : LOW);

        // GPIO-level check only: it cannot detect a welded contact or a
        // failed driver stage.
        if (digitalRead(RELAY_PIN) != level)
        {
            ++readbackMismatches;
            digitalWrite(RELAY_PIN, level);  // single retry
        }
    }
}

void actuatorBegin()
{
    // Latch the OFF level into the output register before enabling the
    // driver so the pin never glitches to the ON level.
    digitalWrite(RELAY_PIN, RELAY_OFF_LEVEL);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, RELAY_OFF_LEVEL);

    pinMode(LED_NORMAL_PIN, OUTPUT);
    pinMode(LED_ALERT_PIN, OUTPUT);
    digitalWrite(LED_NORMAL_PIN, LOW);
    digitalWrite(LED_ALERT_PIN, LOW);

    commandedOn = false;
}

void actuatorApplyState(SystemState state)
{
    drive(outputsForState(state));
}

void actuatorApplyResult(const ProtectionResult& result)
{
    drive(outputsForResult(result));
}

bool actuatorRelayCommandedOn()
{
    return commandedOn;
}

bool actuatorRelayOutputEnabled()
{
    return kRelayOutputMode == RelayOutputMode::ENABLED;
}

uint32_t actuatorReadbackMismatches()
{
    return readbackMismatches;
}
