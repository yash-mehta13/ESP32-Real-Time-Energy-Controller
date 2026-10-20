#include "mini_test.h"
#include <string.h>

#include "heartbeat_monitor.h"
#include "telemetry_json.h"

namespace
{
    TelemetryInput baseInput()
    {
        TelemetryInput in{};
        in.deviceId = "device01";
        in.firmwareVersion = "1.3.0";
        in.measurement.voltage = 230.0f;
        in.measurement.current = 2.17f;
        in.measurement.power = 500.0f;
        in.measurement.apparentPower = 499.1f;
        in.measurement.powerFactor = 1.0f;
        in.measurement.valid = true;
        in.measurement.status = MeasurementStatus::OK;
        in.measurement.source = MeasurementSource::SIMULATION;
        in.protection.state = SystemState::WARNING;
        in.protection.lastResetResult = ResetResult::NONE;
        in.uptimeMs = 123456;
        in.measurementAgeMs = 40;
        in.relayCommandedOn = true;
        in.relayOutputEnabled = false;
        return in;
    }
}

TEST(telemetry_contains_source_validity_and_commanded_relay)
{
    char buf[768];
    const int n = buildTelemetryJson(buf, sizeof(buf), baseInput());
    CHECK(n > 0);
    CHECK_EQ(static_cast<size_t>(n), strlen(buf));
    CHECK(strstr(buf, "\"schema_version\":2") != nullptr);
    CHECK(strstr(buf, "\"source\":\"SIMULATION\"") != nullptr);
    CHECK(strstr(buf, "\"simulated\":true") != nullptr);
    CHECK(strstr(buf, "\"valid\":true") != nullptr);
    CHECK(strstr(buf, "\"relay_commanded_on\":true") != nullptr);
    CHECK(strstr(buf, "\"relay_output_enabled\":false") != nullptr);
    CHECK(strstr(buf, "\"relay_on\"") == nullptr);   // old, misleading key is gone
    CHECK(strstr(buf, "\"state\":\"WARNING\"") != nullptr);
}

TEST(telemetry_fits_the_firmware_buffer_with_margin)
{
    char buf[768];
    TelemetryInput in = baseInput();
    in.protection.fault.activeFault = FaultCode::PROTECTION_INTERNAL;
    in.protection.lastResetResult = ResetResult::REJECTED_INVALID_MEASUREMENT;
    const int n = buildTelemetryJson(buf, sizeof(buf), in);
    CHECK(n > 0 && n < 600);
}

TEST(telemetry_non_finite_numbers_become_null_not_nan)
{
    char buf[768];
    TelemetryInput in = baseInput();
    in.measurement.power = NAN;
    in.measurement.voltage = INFINITY;
    CHECK(buildTelemetryJson(buf, sizeof(buf), in) > 0);
    CHECK(strstr(buf, "\"power\":null") != nullptr);
    CHECK(strstr(buf, "\"voltage\":null") != nullptr);
    CHECK(strstr(buf, "nan") == nullptr && strstr(buf, "inf") == nullptr);
}

TEST(telemetry_escapes_strings)
{
    char buf[768];
    TelemetryInput in = baseInput();
    in.deviceId = "a\"b\\c\n";
    CHECK(buildTelemetryJson(buf, sizeof(buf), in) > 0);
    CHECK(strstr(buf, "\"device_id\":\"a\\\"b\\\\c\\u000a\"") != nullptr);
}

TEST(telemetry_buffer_too_small_returns_error_and_empty_string)
{
    char buf[64];
    memset(buf, 'x', sizeof(buf));
    CHECK_EQ(buildTelemetryJson(buf, sizeof(buf), baseInput()), -1);
    CHECK_EQ(buf[0], '\0');
    CHECK_EQ(buildTelemetryJson(nullptr, 10, baseInput()), -1);
    CHECK_EQ(buildTelemetryJson(buf, 0, baseInput()), -1);
}

TEST(telemetry_is_balanced_json_object)
{
    char buf[768];
    buildTelemetryJson(buf, sizeof(buf), baseInput());
    int depth = 0, quotes = 0;
    for (const char* p = buf; *p; ++p)
    {
        if (*p == '"' && (p == buf || p[-1] != '\\')) quotes++;
        if (*p == '{') depth++;
        if (*p == '}') depth--;
    }
    CHECK_EQ(depth, 0);
    CHECK_EQ(quotes % 2, 0);
    CHECK(buf[0] == '{' && buf[strlen(buf) - 1] == '}');
}

// ---- heartbeat -------------------------------------------------------------

TEST(heartbeat_never_seen_slot_is_unhealthy_after_grace)
{
    HeartbeatMonitor h(3);
    h.beat(0, 11000); h.beat(1, 11000);     // slot 2 never reports
    CHECK(h.healthy(5000, 6000, 0, 10000));            // inside grace period
    CHECK(!h.healthy(12000, 6000, 0, 10000));
    CHECK_EQ(h.firstStale(12000, 6000), 2u);
}

TEST(heartbeat_timeout_boundary)
{
    HeartbeatMonitor h(2);
    h.beat(0, 1000); h.beat(1, 1000);
    CHECK(h.healthy(7000, 6000, 0, 0));      // age == timeout: still healthy
    CHECK(!h.healthy(7001, 6000, 0, 0));
}

TEST(heartbeat_a_zero_timestamp_is_a_real_beat)
{
    HeartbeatMonitor h(1);
    h.beat(0, 0);
    CHECK_EQ(h.ageMs(0, 500), 500u);
}

TEST(heartbeat_rollover_safe)
{
    HeartbeatMonitor h(1);
    h.beat(0, 0xFFFFFF00u);
    CHECK_EQ(h.ageMs(0, 0x100u), 0x200u);
}

TEST(heartbeat_out_of_range_slot_is_ignored_and_never_healthy)
{
    HeartbeatMonitor h(2);
    h.beat(7, 10);
    CHECK_EQ(h.ageMs(7, 100), HEARTBEAT_NEVER);
}

TEST(one_stalled_task_makes_the_system_unhealthy)
{
    HeartbeatMonitor h(3);
    for (uint32_t t = 0; t <= 20000; t += 1000)
    {
        h.beat(0, t); h.beat(1, t);                    // slot 2 stalls at t=0
        if (t == 0) h.beat(2, t);
    }
    CHECK(!h.healthy(20000, 6000, 0, 10000));
    CHECK_EQ(h.firstStale(20000, 6000), 2u);
}
