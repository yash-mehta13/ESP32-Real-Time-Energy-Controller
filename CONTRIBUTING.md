# Contributing

Thanks for helping. This is a safety-adjacent educational project: correctness
and honest evidence matter more than feature count.

## Local setup

```bash
git clone <repo> && cd ESP32-Real-Time-Energy-Controller
pip install platformio
cp src/config.example.h src/config.h     # placeholders only; never commit it
make test
pio run -e esp32dev_sim
```

Work in simulation first. Do not connect mains for development.

## Source changes

- Keep hardware-independent logic in `lib/energy_core` and thin adapters in `src/`.
- Match the surrounding style; comment design decisions, not syntax.
- Do not add dependencies or layers without a clear need.

## Protection logic changes

- Write or update tests in `test/native` first. Expected values must come
  from the requirement, not from what the code currently does.
- Preserve the invariants in CLAUDE.md (simulation isolation, latch, reset
  interlock, fail-closed).
- Update `docs/fault-management.md` and `docs/requirements-traceability.md`.

## Measurement changes

State units, validity rules and how invalid data is handled. Never turn
invalid input into a valid zero. Do not invent calibration or accuracy numbers.

## MQTT / network changes

Callbacks only validate and enqueue. Keep topics derived from `MQTT_DEVICE_ID`,
keep payload validation exact, never weaken TLS. Document schema changes in
`docs/mqtt-protocol.md` and bump `schema_version` for breaking ones.

## Hardware validation records

Physical-test claims need a file under `docs/validation/` with firmware
commit, environment, wiring/parts (including verified relay polarity),
reference instrument, raw readings and observed behaviour. Do not submit
numbers you did not measure. Only qualified people should work on mains.

## Docs and wiring

New features and pin/wiring changes must update the README tables and
`docs/hardware-and-wiring.md`. Run `python3 scripts/check_docs.py`.

## Before opening a pull request

`make test`, both `pio run` environments, `scripts/scan_secrets.py`, and fill
in the pull-request template.
