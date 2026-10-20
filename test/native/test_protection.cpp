// Protection state machine tests. Expected values come from the documented
// policy (warning 485 W, fault 550 W, 2 s confirmation), not from the
// implementation. All inputs are synthetic.
#include "mini_test.h"
#include "protection_core.h"
#include "measurement_core.h"

namespace
{
    ProtectionConfig cfg()
    {
        ProtectionConfig c{};
        c.warningPowerW = 485.0f;
        c.faultPowerW = 550.0f;
        c.minOperatingVoltageV = 10.0f;
        c.faultConfirmMs = 2000;
        c.maxMeasurementAgeMs = 3000;
        return c;
    }

    Measurement sample(float power, uint32_t t, float voltage = 230.0f)
    {
        Measurement m{};
        m.voltage = voltage;
        m.current = voltage > 0 ? power / voltage : 0.0f;
        m.power = power;
        m.apparentPower = voltage * m.current;
        m.powerFactor = 1.0f;
        m.valid = true;
        m.status = MeasurementStatus::OK;
        m.source = MeasurementSource::SIMULATION;
        m.timestamp = t;
        return m;
    }

    Measurement invalidSample(uint32_t t)
    {
        return makeInvalidMeasurement(MeasurementSource::SIMULATION,
                                      MeasurementStatus::SENSOR_ERROR, t);
    }

    // Core that has already seen a normal sample (past the boot gate).
    ProtectionCore armed(uint32_t& now)
    {
        ProtectionCore c(cfg());
        c.begin(0);
        now = 1000;
        c.update(sample(100.0f, now), now);
        return c;
    }

    // Drive a core into a latched OVERPOWER fault at samples 1 s apart.
    void latchOverpower(ProtectionCore& c, uint32_t& now)
    {
        for (int i = 0; i < 3; ++i)
        {
            now += 1000;
            c.update(sample(600.0f, now), now);
        }
    }
}

TEST(init_state_until_first_sample)
{
    ProtectionCore c(cfg());
    c.begin(0);
    CHECK(c.state() == SystemState::INIT);
}

TEST(power_below_warning_is_normal)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    CHECK(c.state() == SystemState::NORMAL);
}

TEST(power_exactly_at_warning_threshold_is_normal)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 1000;
    c.update(sample(485.0f, now), now);
    CHECK(c.state() == SystemState::NORMAL);  // comparison is strictly '>'
}

TEST(power_just_above_warning_is_warning)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 1000;
    c.update(sample(485.1f, now), now);
    CHECK(c.state() == SystemState::WARNING);
}

TEST(power_exactly_at_fault_threshold_is_only_warning)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    for (int i = 0; i < 10; ++i)
    {
        now += 1000;
        c.update(sample(550.0f, now), now);
    }
    CHECK(c.state() == SystemState::WARNING);
    CHECK(!c.snapshot(now).faultTimerActive);
}

TEST(power_just_above_fault_starts_timer_and_stays_warning)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 1000;
    c.update(sample(550.1f, now), now);
    CHECK(c.state() == SystemState::WARNING);
    CHECK(c.snapshot(now).faultTimerActive);
}

TEST(fault_not_latched_one_ms_before_confirmation)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 1000;
    c.update(sample(600.0f, now), now);       // timer starts at `now`
    const uint32_t start = now;
    c.update(sample(600.0f, start + 1999), start + 1999);
    CHECK(c.state() == SystemState::WARNING);
}

TEST(fault_latched_exactly_at_confirmation_time)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 1000;
    c.update(sample(600.0f, now), now);
    const uint32_t start = now;
    const ProtectionResult r = c.update(sample(600.0f, start + 2000), start + 2000);
    CHECK(c.state() == SystemState::FAULT);
    CHECK(r.fault == FaultCode::OVERPOWER);
    CHECK(r.stateChanged && r.faultChanged);
}

TEST(timer_cancelled_when_condition_clears)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 1000; c.update(sample(600.0f, now), now);
    now += 1000; c.update(sample(300.0f, now), now);   // clears
    CHECK(!c.snapshot(now).faultTimerActive);
    // A new excursion must get a full 2 s again.
    now += 1000; c.update(sample(600.0f, now), now);
    const uint32_t start = now;
    c.update(sample(600.0f, start + 1500), start + 1500);
    CHECK(c.state() == SystemState::WARNING);
}

TEST(confirmation_is_rollover_safe)
{
    ProtectionCore c(cfg());
    const uint32_t t0 = 0xFFFFFE00u;
    c.begin(t0);
    c.update(sample(100.0f, t0 + 100), t0 + 100);              // arm
    c.update(sample(600.0f, t0 + 1000), t0 + 1000);            // timer start
    const uint32_t before = t0 + 1000 + 1999;                  // wraps past 2^32
    c.update(sample(600.0f, before), before);
    CHECK(c.state() == SystemState::WARNING);
    const uint32_t at = t0 + 1000 + 2000;
    c.update(sample(600.0f, at), at);
    CHECK(c.state() == SystemState::FAULT);
    CHECK(at < t0);  // the test really crossed the 32-bit boundary
}

TEST(invalid_measurement_latches_sensor_fault)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 1000;
    const ProtectionResult r = c.update(invalidSample(now), now);
    CHECK(c.state() == SystemState::FAULT);
    CHECK(r.fault == FaultCode::SENSOR_INVALID);
}

TEST(invalid_first_sample_after_boot_latches_fault)
{
    ProtectionCore c(cfg());
    c.begin(0);
    c.update(invalidSample(500), 500);
    CHECK(c.state() == SystemState::FAULT);
}

TEST(stale_sample_latches_stale_fault)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 4000;
    c.update(sample(100.0f, now - 3500), now);  // age 3500 > 3000
    CHECK(c.state() == SystemState::FAULT);
    CHECK(c.snapshot(now).fault.activeFault == FaultCode::SENSOR_STALE);
}

TEST(silence_is_detected_by_tick)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    c.tick(now + 3000);
    CHECK(c.state() == SystemState::NORMAL);   // exactly at the limit: still ok
    c.tick(now + 3001);
    CHECK(c.state() == SystemState::FAULT);
    CHECK(c.snapshot(now + 3001).fault.activeFault == FaultCode::SENSOR_STALE);
}

TEST(silence_before_first_sample_is_detected)
{
    ProtectionCore c(cfg());
    c.begin(10);
    c.tick(10 + 3001);
    CHECK(c.state() == SystemState::FAULT);
}

TEST(no_mains_voltage_is_init_not_fault)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 1000;
    c.update(sample(0.0f, now, 0.0f), now);
    CHECK(c.state() == SystemState::INIT);
    CHECK(!c.snapshot(now).fault.active);
}

TEST(voltage_exactly_at_minimum_is_still_init)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 1000;
    c.update(sample(0.0f, now, 10.0f), now);
    CHECK(c.state() == SystemState::INIT);
}

// ---- latching, acknowledgement, reset -------------------------------------

TEST(fault_is_latched_even_when_condition_clears)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    latchOverpower(c, now);
    CHECK(c.state() == SystemState::FAULT);
    for (int i = 0; i < 5; ++i)
    {
        now += 1000;
        c.update(sample(10.0f, now), now);
    }
    CHECK(c.state() == SystemState::FAULT);
}

TEST(repeated_overpower_does_not_overwrite_or_recount_fault)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    latchOverpower(c, now);
    const uint32_t total = c.snapshot(now).fault.totalFaults;
    now += 1000;
    c.update(invalidSample(now), now);   // a different cause while latched
    const ProtectionSnapshot s = c.snapshot(now);
    CHECK(s.fault.activeFault == FaultCode::OVERPOWER);  // first cause kept
    CHECK_EQ(s.fault.totalFaults, total);
}

TEST(ack_sets_flag_but_does_not_clear_fault)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    latchOverpower(c, now);
    CHECK(c.acknowledge());
    const ProtectionSnapshot s = c.snapshot(now);
    CHECK(s.fault.acknowledged);
    CHECK(s.fault.active);
    CHECK(s.state == SystemState::FAULT);
}

TEST(ack_without_fault_is_rejected)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    CHECK(!c.acknowledge());
}

TEST(reset_without_fault_is_rejected)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    CHECK(c.requestReset(now) == ResetResult::REJECTED_NO_ACTIVE_FAULT);
    CHECK(c.state() == SystemState::NORMAL);
}

TEST(reset_rejected_while_overpower_persists)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    latchOverpower(c, now);
    now += 500;
    CHECK(c.requestReset(now) == ResetResult::REJECTED_UNSAFE_CONDITION);
    CHECK(c.state() == SystemState::FAULT);
    CHECK(c.snapshot(now).fault.active);
}

TEST(reset_rejected_with_stale_measurement)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    latchOverpower(c, now);
    now += 1000;
    c.update(sample(100.0f, now), now);            // condition has cleared
    CHECK(c.requestReset(now + 3001) == ResetResult::REJECTED_STALE_MEASUREMENT);
    CHECK(c.state() == SystemState::FAULT);
}

TEST(reset_rejected_with_invalid_measurement)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 1000;
    c.update(invalidSample(now), now);             // SENSOR_INVALID latched
    CHECK(c.requestReset(now + 10) == ResetResult::REJECTED_INVALID_MEASUREMENT);
    CHECK(c.state() == SystemState::FAULT);
}

TEST(reset_accepted_only_after_condition_clears_and_goes_to_init)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    latchOverpower(c, now);
    now += 1000;
    c.update(sample(100.0f, now), now);
    CHECK(c.requestReset(now) == ResetResult::ACCEPTED);
    // Reset itself never closes the relay: state is INIT (relay OFF).
    CHECK(c.state() == SystemState::INIT);
    CHECK(!c.snapshot(now).fault.active);
    CHECK(c.snapshot(now).lastResetResult == ResetResult::ACCEPTED);
}

TEST(after_reset_old_samples_are_ignored_and_state_stays_init)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    latchOverpower(c, now);
    now += 1000;
    c.update(sample(100.0f, now), now);
    c.requestReset(now + 50);
    c.update(sample(100.0f, now), now + 60);       // timestamp before reset
    CHECK(c.state() == SystemState::INIT);
}

TEST(after_reset_fresh_normal_sample_returns_to_normal)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    latchOverpower(c, now);
    now += 1000;
    c.update(sample(100.0f, now), now);
    c.requestReset(now);
    now += 1000;
    c.update(sample(100.0f, now), now);
    CHECK(c.state() == SystemState::NORMAL);
}

TEST(after_reset_overpower_relatches_immediately_without_warning_window)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    latchOverpower(c, now);
    now += 1000;
    c.update(sample(100.0f, now), now);
    CHECK(c.requestReset(now) == ResetResult::ACCEPTED);
    now += 1000;
    const ProtectionResult r = c.update(sample(700.0f, now), now);
    // Must go straight back to FAULT, never through WARNING (relay ON).
    CHECK(r.state == SystemState::FAULT);
    CHECK(r.fault == FaultCode::OVERPOWER);
}

TEST(boot_with_overpower_present_latches_immediately)
{
    ProtectionCore c(cfg());
    c.begin(0);
    const ProtectionResult r = c.update(sample(700.0f, 100), 100);
    CHECK(r.state == SystemState::FAULT);
}

TEST(mains_returning_onto_overload_latches_immediately)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    now += 1000; c.update(sample(0.0f, now, 0.0f), now);   // mains lost
    now += 1000;
    const ProtectionResult r = c.update(sample(700.0f, now), now);
    CHECK(r.state == SystemState::FAULT);
}

// ---- internal failure ------------------------------------------------------

TEST(internal_fault_latches_and_is_reported_as_fault_state)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    const ProtectionResult r = c.raiseInternalFault(now);
    CHECK(r.state == SystemState::FAULT);
    CHECK(r.fault == FaultCode::PROTECTION_INTERNAL);
    now += 1000;
    c.update(sample(100.0f, now), now);
    CHECK(c.state() == SystemState::FAULT);   // does not silently recover
}

TEST(internal_fault_requires_explicit_reset_with_good_data)
{
    uint32_t now;
    ProtectionCore c = armed(now);
    c.raiseInternalFault(now);
    now += 1000;
    c.update(sample(100.0f, now), now);
    CHECK(c.requestReset(now) == ResetResult::ACCEPTED);
}

TEST(reset_busy_name)
{
    CHECK_STR(resetResultName(ResetResult::REJECTED_BUSY), "REJECTED_BUSY");
}

TEST(names_are_stable_strings)
{
    CHECK_STR(systemStateName(SystemState::WARNING), "WARNING");
    CHECK_STR(faultCodeName(FaultCode::PROTECTION_INTERNAL), "PROTECTION_INTERNAL");
    CHECK_STR(resetResultName(ResetResult::REJECTED_UNSAFE_CONDITION),
              "REJECTED_UNSAFE_CONDITION");
}
