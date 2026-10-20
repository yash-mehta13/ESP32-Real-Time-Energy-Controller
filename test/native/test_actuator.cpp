#include "mini_test.h"
#include "actuator_policy.h"

TEST(init_state_relay_off_leds_off)
{
    const OutputCommand o = outputsForState(SystemState::INIT);
    CHECK(!o.relayCommandedOn && !o.ledNormal && !o.ledAlert);
}

TEST(normal_state_relay_on_normal_led)
{
    const OutputCommand o = outputsForState(SystemState::NORMAL);
    CHECK(o.relayCommandedOn && o.ledNormal && !o.ledAlert);
}

TEST(warning_state_relay_on_alert_led)
{
    const OutputCommand o = outputsForState(SystemState::WARNING);
    CHECK(o.relayCommandedOn && !o.ledNormal && o.ledAlert);
}

TEST(fault_state_relay_off_alert_led)
{
    const OutputCommand o = outputsForState(SystemState::FAULT);
    CHECK(!o.relayCommandedOn && !o.ledNormal && o.ledAlert);
}

TEST(unknown_state_value_fails_safe_relay_off)
{
    const OutputCommand o = outputsForState(static_cast<SystemState>(200));
    CHECK(!o.relayCommandedOn);
}

TEST(only_normal_and_warning_may_close_the_relay)
{
    for (int s = 0; s < 256; ++s)
    {
        const SystemState st = static_cast<SystemState>(s);
        const bool allowed = st == SystemState::NORMAL || st == SystemState::WARNING;
        CHECK_EQ(outputsForState(st).relayCommandedOn, allowed);
    }
}

TEST(simulation_mode_never_drives_the_relay_pin_on)
{
    for (int s = 0; s < 4; ++s)
    {
        const bool commanded = outputsForState(static_cast<SystemState>(s)).relayCommandedOn;
        CHECK(!relayPinDrivenOn(commanded, RelayOutputMode::DISABLED));
    }
}

TEST(hardware_mode_follows_command)
{
    CHECK(relayPinDrivenOn(true, RelayOutputMode::ENABLED));
    CHECK(!relayPinDrivenOn(false, RelayOutputMode::ENABLED));
}

TEST(active_low_polarity_mapping)
{
    const int LOW_ = 0, HIGH_ = 1;   // active-low module: ON = LOW
    CHECK_EQ(relayLevel(true, LOW_, HIGH_), LOW_);
    CHECK_EQ(relayLevel(false, LOW_, HIGH_), HIGH_);
}

TEST(active_high_polarity_mapping)
{
    const int LOW_ = 0, HIGH_ = 1;
    CHECK_EQ(relayLevel(true, HIGH_, LOW_), HIGH_);
    CHECK_EQ(relayLevel(false, HIGH_, LOW_), LOW_);
}

TEST(lock_failure_result_is_fault_with_safe_outputs_even_if_flags_ignored)
{
    // Regression: protectionUpdate() used to return FAULT with
    // stateChanged == faultChanged == false when the mutex could not be
    // taken, and the caller only drove the actuator on a change flag, so the
    // relay could stay ON. The result must now demand the safe output
    // unconditionally.
    const ProtectionResult r = ProtectionCore::lockFailureResult();
    CHECK(r.synchronizationFailed);
    CHECK(r.state == SystemState::FAULT);
    CHECK(r.fault == FaultCode::PROTECTION_INTERNAL);
    CHECK(r.stateChanged && r.faultChanged);
    CHECK(!outputsForResult(r).relayCommandedOn);
}

TEST(synchronization_failure_overrides_any_reported_state)
{
    ProtectionResult r{};
    r.state = SystemState::NORMAL;      // a (hypothetical) inconsistent result
    r.synchronizationFailed = true;
    CHECK(!outputsForResult(r).relayCommandedOn);
}

TEST(outputs_for_normal_result_follow_state)
{
    ProtectionResult r{};
    r.state = SystemState::NORMAL;
    CHECK(outputsForResult(r).relayCommandedOn);
}
