# Changelog

Only changes made in this revision are listed; earlier history is in `git log`.

## Unreleased (firmware 1.3.0, build `SAFETY-HARDENING-1`)

### Fixed (behaviour-changing, safety)
- Protection lock failure no longer leaves the relay ON: a fail-closed result
  is returned and the actuator is driven on every result (previously only when
  a change flag was set).
- Reset is now an interlocked request: refused unless a fresh valid sample
  shows power at or below the fault threshold; accepted reset returns to INIT
  and re-arms so an overload re-latches immediately instead of running a 2 s
  WARNING window with the relay ON. Same rule after boot and after mains returns.
- Simulated measurements can no longer drive the relay pin (build-time
  separation; relay pin held at OFF level in simulation builds).
- Sensor silence / stale samples latch `SENSOR_STALE` (new fault code);
  internal protection failure latches `PROTECTION_INTERNAL` (new fault code).
- Watchdog restart path drives the safe output first; task-watchdog
  registration errors are no longer reported as success.
- Remote commands: exact byte-wise validation (no truncated-string match);
  2 s hold-off after subscribing to drop retained commands.

### Changed (breaking)
- Build selection moved to PlatformIO environments `esp32dev_sim` (default) and
  `esp32dev_pzem`; `MEASUREMENT_SOURCE` removed from `config.h`. `pio run`
  without `-e` builds simulation.
- `config.h`: `MQTT_DEVICE_ID` is now a macro and `MQTT_TOPIC_*` constants were
  removed (derived at compile time). Existing local `config.h` files must be
  re-created from `config.example.h`.
- Telemetry schema v2: `relay_on` replaced by `relay_commanded_on`; added
  `schema_version`, `simulated`, `measurement_status`, `measurement_age_ms`,
  `last_reset_result`, `relay_output_enabled`; invalid samples zero numeric
  fields; non-finite values serialise as `null`.
- PZEM power factor is now the meter's `pf()` value (previously recomputed as
  P/S and clamped). Added range and P-versus-S consistency checks: a sample
  that was previously accepted can now be flagged invalid (these limits have
  not been exercised on hardware).
- LCD boot text shortened to fit 16 columns; `S` marks simulation.

### Added
- `lib/energy_core` pure logic library, `test/native` host test suite (87
  tests), `Makefile` (`make test`).
- GitHub Actions workflow (host tests, both firmware builds, link and secret scans).
- Diagrams (`docs/assets/*.svg`), engineering documents, `CLAUDE.md`,
  `CONTRIBUTING.md`, `SECURITY.md`, issue and PR templates, helper scripts.

### Fixed after the first CI run
- The first CI run (both `pio run` environments, espressif32 8.0.0 /
  arduino-esp32 3.3.12) failed: the core defines `DISABLED` as a macro in
  `esp32-hal-gpio.h`, which broke the `RelayOutputMode::DISABLED` enum
  member. Members renamed to `HOLD_OFF` / `FOLLOW_STATE`. The host build now
  force-includes `test/native/arduino_macros.h` (the core's global macros) so
  such collisions fail `make test` too.
- `diagnostics.cpp`: `%u` used for a `uint32_t` (`-Wformat` warning in CI).

### Verification performed for this revision
- `make test`: 87 tests, 0 failed (host, g++ `-Wall -Wextra -Werror -pedantic`).
- Mutation spot-checks: re-introducing the original defects (lock-failure
  flag ignored, reset interlock removed, simulation driving the relay,
  first-sample rule removed, ack clearing the fault) each made at least one
  test fail.
- `-fsyntax-only` of every `src/*.cpp` in both source modes with `-Wall
  -Wextra` (no warnings) against hand-written Arduino/FreeRTOS/WiFi/Blynk
  stubs and the real PZEM library header. This approximates the SDK; it is
  not a firmware build. The `#error` / `static_assert` guards were exercised
  with g++.
- `scripts/check_docs.py` and `scripts/scan_secrets.py` pass.
- **Not run locally:** `pio run` (the PlatformIO registry host was not
  reachable from the preparation environment); firmware builds are verified
  only by CI. No ESP32 hardware, MQTT or Blynk connection was used.
