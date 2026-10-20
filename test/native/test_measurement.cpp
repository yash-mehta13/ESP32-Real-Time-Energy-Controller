#include "mini_test.h"
#include "measurement_core.h"

namespace
{
    RawPzemReading good()
    {
        RawPzemReading r{};
        r.voltage = 230.0f;
        r.current = 2.0f;
        r.power = 440.0f;
        r.energy = 1.5f;
        r.powerFactor = 0.95f;
        return r;
    }

    Measurement build(const RawPzemReading& r)
    {
        return buildMeasurement(r, MeasurementSource::PZEM, 1234,
                                defaultMeasurementLimits());
    }
}

TEST(valid_reading_is_accepted_and_apparent_power_is_v_times_i)
{
    const Measurement m = build(good());
    CHECK(m.valid);
    CHECK(m.status == MeasurementStatus::OK);
    CHECK(fabsf(m.apparentPower - 460.0f) < 0.01f);
    CHECK(fabsf(m.power - 440.0f) < 0.01f);   // active power is the meter's, not V*I
    CHECK(fabsf(m.powerFactor - 0.95f) < 0.0001f);
    CHECK(m.source == MeasurementSource::PZEM);
    CHECK_EQ(m.timestamp, 1234u);
}

TEST(nan_in_any_field_is_sensor_error_not_zero)
{
    float RawPzemReading::*fields[] = {
        &RawPzemReading::voltage, &RawPzemReading::current,
        &RawPzemReading::power, &RawPzemReading::energy,
        &RawPzemReading::powerFactor};
    for (auto f : fields)
    {
        RawPzemReading r = good();
        r.*f = NAN;
        const Measurement m = build(r);
        CHECK(!m.valid);
        CHECK(m.status == MeasurementStatus::SENSOR_ERROR);
    }
}

TEST(infinity_is_sensor_error)
{
    RawPzemReading r = good();
    r.power = INFINITY;
    CHECK(build(r).status == MeasurementStatus::SENSOR_ERROR);
}

TEST(invalid_measurement_has_zeroed_numeric_fields)
{
    RawPzemReading r = good();
    r.voltage = NAN;
    const Measurement m = build(r);
    CHECK(m.voltage == 0.0f && m.power == 0.0f && m.apparentPower == 0.0f);
    CHECK(!m.valid);  // consumers must look at .valid, a zero is not a reading
}

TEST(negative_values_are_out_of_range)
{
    RawPzemReading r = good(); r.voltage = -1.0f;
    CHECK(build(r).status == MeasurementStatus::OUT_OF_RANGE);
    r = good(); r.current = -0.1f;
    CHECK(build(r).status == MeasurementStatus::OUT_OF_RANGE);
    r = good(); r.power = -5.0f;
    CHECK(build(r).status == MeasurementStatus::OUT_OF_RANGE);
    r = good(); r.energy = -0.001f;
    CHECK(build(r).status == MeasurementStatus::OUT_OF_RANGE);
}

TEST(values_above_plausibility_bounds_are_out_of_range)
{
    RawPzemReading r = good(); r.voltage = 301.0f; r.current = 2.0f;
    CHECK(build(r).status == MeasurementStatus::OUT_OF_RANGE);
    r = good(); r.current = 100.1f;
    CHECK(build(r).status == MeasurementStatus::OUT_OF_RANGE);
}

TEST(power_factor_outside_zero_to_one_is_out_of_range)
{
    RawPzemReading r = good(); r.powerFactor = 1.01f;
    CHECK(build(r).status == MeasurementStatus::OUT_OF_RANGE);
    r = good(); r.powerFactor = -0.01f;
    CHECK(build(r).status == MeasurementStatus::OUT_OF_RANGE);
}

TEST(zero_voltage_and_zero_current_is_a_valid_idle_reading)
{
    RawPzemReading r{};
    const Measurement m = build(r);
    CHECK(m.valid);
    CHECK(m.apparentPower == 0.0f);
    CHECK(m.powerFactor == 0.0f);
}

TEST(active_power_far_above_apparent_power_is_inconsistent)
{
    RawPzemReading r = good();
    r.power = 900.0f;           // S = 460 VA; P cannot exceed S
    CHECK(build(r).status == MeasurementStatus::INCONSISTENT);
}

TEST(power_with_zero_voltage_is_inconsistent)
{
    RawPzemReading r{};
    r.power = 300.0f;
    CHECK(build(r).status == MeasurementStatus::INCONSISTENT);
}

TEST(small_power_over_apparent_within_tolerance_is_accepted)
{
    RawPzemReading r = good();
    r.power = 460.0f * 1.05f;   // 5 % over S, inside the 10 % + 2 W allowance
    CHECK(build(r).valid);
}

TEST(freshness_boundary_and_rollover)
{
    Measurement m{};
    m.timestamp = 1000;
    CHECK(measurementIsFresh(m, 4000, 3000));
    CHECK(!measurementIsFresh(m, 4001, 3000));
    m.timestamp = 0xFFFFFF00u;
    CHECK(measurementIsFresh(m, 0x000000FFu, 3000));   // across the wrap
    // A timestamp from the "future" is never fresh.
    m.timestamp = 5000;
    CHECK(!measurementIsFresh(m, 4000, 3000));
}

TEST(simulation_profile_is_deterministic_and_labelled)
{
    const Measurement a = simulationSample(2000);
    const Measurement b = simulationSample(7000);
    const Measurement c = simulationSample(12000);
    const Measurement wrap = simulationSample(15000 + 2000);
    CHECK(a.valid && b.valid && c.valid);
    CHECK(a.source == MeasurementSource::SIMULATION);
    CHECK(fabsf(a.power - 40.0f) < 0.01f);
    CHECK(fabsf(b.power - 500.0f) < 0.01f);
    CHECK(fabsf(c.power - 600.0f) < 0.01f);
    CHECK(fabsf(wrap.power - a.power) < 0.01f);
    CHECK_STR(measurementSourceName(a.source), "SIMULATION");
}

TEST(source_and_status_names)
{
    CHECK_STR(measurementSourceName(MeasurementSource::PZEM), "PZEM");
    CHECK_STR(measurementSourceName(static_cast<MeasurementSource>(9)), "UNKNOWN");
    CHECK_STR(measurementStatusName(MeasurementStatus::INCONSISTENT), "INCONSISTENT");
}
