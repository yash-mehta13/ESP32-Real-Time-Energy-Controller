# CLAUDE.md

Guidance for Claude sessions working in this repository.

## Project

ESP32 (Arduino framework, PlatformIO) energy controller: PZEM-004T v3.0 or a
simulation -> protection state machine -> relay, with Blynk and MQTT/TLS
reporting. Educational prototype; **no safety certification is claimed**.

## Commands

```bash
make test                                   # host unit tests (g++, no deps)
cp src/config.example.h src/config.h        # placeholders; never commit config.h
pio run -e esp32dev_sim                     # default, relay pin held OFF
pio run -e esp32dev_pzem                    # real PZEM + real relay output
python3 scripts/check_docs.py               # relative links
python3 scripts/scan_secrets.py             # credential heuristics
```

## Layout

- `lib/energy_core/src` - pure logic (no Arduino/FreeRTOS): `protection_core`,
  `measurement_core`, `actuator_policy`, `remote_command`, `telemetry_json`,
  `heartbeat_monitor`, `energy_topics.h`. Unit-tested in `test/native`.
- `src/` - ESP32 adapters and `main.cpp`. `build_mode.h` decides source/relay mode.
- `docs/` - engineering docs; `docs/assets/*.svg` diagrams (hand-editable).

## Rules that must not be broken

1. Simulation builds must never drive the relay pin to its ON level
   (`RelayOutputMode::HOLD_OFF`). Do not add a path around `build_mode.h`.
2. Only `src/actuator.cpp` writes relay/LED GPIOs. MQTT callbacks, CLI and
   display never touch GPIO or protection state directly.
3. Every protection result drives the actuator (`actuatorApplyResult`), not
   only on change flags. Failure of any safety dependency (lock, stale data,
   invalid data) must lead to relay OFF.
4. Faults are latched. ACK is not reset. Reset is a request judged by the
   interlock (fresh valid sample, power <= fault threshold) and returns to
   INIT (relay OFF); first sample after boot/reset must not start a
   confirmation window.
5. Local protection must not depend on Wi-Fi, Blynk, MQTT or NTP.
6. Never call `setInsecure()` or disable certificate validation.
7. No secrets in the repo, logs, docs, tests or CI. `src/config.h` stays ignored.
8. Time arithmetic uses unsigned subtraction (rollover safe).
9. Do not claim relay "state" as physical fact: it is *commanded*.

## Evidence discipline

Never invent test results, hardware measurements, photos or accuracy figures.
Distinguish: inspected, host-tested, built, simulated on target, bench-tested.
Hardware claims need a record in `docs/validation/`. Conceptual diagrams must
be labelled as such. If a tool/hardware/credential is unavailable, say so.

## Changing behaviour

Protection/measurement/command changes need host tests first (expected values
from requirements, not copied from the implementation) and updates to
`docs/fault-management.md`, `docs/requirements-traceability.md` and
`CHANGELOG.md`. Keep topic names, telemetry keys and thresholds stable unless
the change is documented as breaking.
