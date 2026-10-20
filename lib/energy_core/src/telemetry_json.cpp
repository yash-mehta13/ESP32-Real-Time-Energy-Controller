#include "telemetry_json.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
    // Bounded appender: once an append does not fit, `ok` stays false.
    struct Writer
    {
        char* buf;
        size_t size;
        size_t len;
        bool ok;

        void raw(const char* s)
        {
            const size_t n = strlen(s);
            if (!ok || len + n + 1 > size)
            {
                ok = false;
                return;
            }
            memcpy(buf + len, s, n + 1);
            len += n;
        }

        void ch(char c)
        {
            const char t[2] = {c, '\0'};
            raw(t);
        }

        void str(const char* s)
        {
            ch('"');
            for (const char* p = s ? s : ""; *p; ++p)
            {
                const unsigned char c = static_cast<unsigned char>(*p);
                if (c == '"' || c == '\\')
                {
                    ch('\\');
                    ch(static_cast<char>(c));
                }
                else if (c < 0x20)
                {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    raw(esc);
                }
                else
                {
                    ch(static_cast<char>(c));
                }
            }
            ch('"');
        }

        void key(const char* k, bool first = false)
        {
            if (!first)
            {
                ch(',');
            }
            str(k);
            ch(':');
        }

        void num(float v, const char* fmt)
        {
            if (!isfinite(v))
            {
                raw("null");
                return;
            }
            char t[32];
            snprintf(t, sizeof(t), fmt, static_cast<double>(v));
            raw(t);
        }

        void u32(uint32_t v)
        {
            char t[16];
            snprintf(t, sizeof(t), "%lu", static_cast<unsigned long>(v));
            raw(t);
        }

        void boolean(bool v) { raw(v ? "true" : "false"); }
    };
}

int buildTelemetryJson(char* out, size_t outSize, const TelemetryInput& in)
{
    if (out == nullptr || outSize == 0)
    {
        return -1;
    }

    out[0] = '\0';

    Writer w{out, outSize, 0, true};
    const Measurement& m = in.measurement;
    const ProtectionSnapshot& p = in.protection;

    w.ch('{');
    w.key("schema_version", true); w.u32(TELEMETRY_SCHEMA_VERSION);
    w.key("device_id");            w.str(in.deviceId);
    w.key("firmware_version");     w.str(in.firmwareVersion);
    w.key("source");               w.str(measurementSourceName(m.source));
    w.key("simulated");            w.boolean(m.source == MeasurementSource::SIMULATION);
    w.key("uptime_ms");            w.u32(in.uptimeMs);
    w.key("valid");                w.boolean(m.valid);
    w.key("measurement_status");   w.str(measurementStatusName(m.status));
    w.key("measurement_age_ms");   w.u32(in.measurementAgeMs);
    w.key("voltage");              w.num(m.voltage, "%.2f");
    w.key("current");              w.num(m.current, "%.3f");
    w.key("power");                w.num(m.power, "%.2f");
    w.key("energy");               w.num(m.energy, "%.3f");
    w.key("apparent_power");       w.num(m.apparentPower, "%.2f");
    w.key("power_factor");         w.num(m.powerFactor, "%.3f");
    w.key("state");                w.str(systemStateName(p.state));
    w.key("fault");                w.str(faultCodeName(p.fault.activeFault));
    w.key("fault_active");         w.boolean(p.fault.active);
    w.key("fault_acknowledged");   w.boolean(p.fault.acknowledged);
    w.key("last_reset_result");    w.str(resetResultName(p.lastResetResult));
    w.key("relay_commanded_on");   w.boolean(in.relayCommandedOn);
    w.key("relay_output_enabled"); w.boolean(in.relayOutputEnabled);
    w.ch('}');

    if (!w.ok)
    {
        out[0] = '\0';
        return -1;
    }

    return static_cast<int>(w.len);
}
