# Requirements Traceability

Status key: **Done** = implemented and host-tested; **Impl** = implemented, not
host-testable (needs target); **Author** = author-reported on hardware, no committed record; **Gap** = not met.
"Built" status for ESP32 targets is established by CI, not by this table.

| ID | Requirement | Source | Tests | Status | Evidence |
|---|---|---|---|---|---|
| R1 | Real PZEM measurement or deterministic simulation | `measurement.cpp`, `measurement_core.cpp`, `build_mode.h` | `simulation_profile_*`, `test_measurement.cpp` | Done (sim, validation); PZEM link Impl/Author | host tests; README author report |
| R2 | V, I, P, E, S, PF reported | `measurement_core.cpp` | `valid_reading_*` | Done | host tests |
| R3 | Normal and warning states | `protection_core.cpp` | `power_*_warning_*` | Done | host tests |
| R4 | Time-confirmed overpower fault | `protection_core.cpp` | `fault_*_confirmation*`, `confirmation_is_rollover_safe` | Done | host tests |
| R5 | Latched faults | `protection_core.cpp` | `fault_is_latched_*`, `repeated_overpower_*` | Done | host tests |
| R6 | Local ack and reset | `protection.cpp`, `main.cpp` (CLI) | `ack_*`, `reset_*` | Done (logic); CLI Impl | host tests |
| R7 | Remote MQTT ack and reset | `remote_command.cpp`, `mqtt_manager.cpp` | `test_commands.cpp` | Done (parser); transport Impl/Author | host tests |
| R8 | Reset cannot re-energise while unsafe | `protection_core.cpp` | `reset_rejected_*`, `after_reset_*` | Done | host tests |
| R9 | Relay OFF at init, fault, invalid data, unknown state | `actuator_policy.cpp`, `actuator.cpp` | `test_actuator.cpp` | Done (policy); pin Impl | host tests |
| R10 | Simulation cannot actuate the relay | `build_mode.h`, `actuator.cpp`, `actuator_policy.cpp` | `simulation_mode_never_drives_*` | Done (policy); build guards Inspected | host tests; `#error` guards checked with g++ |
| R11 | Protection lock failure -> safe action | `protection_core.cpp`, `protection.cpp`, `actuator_policy.cpp` | `lock_failure_*`, `internal_fault_*` | Done (logic); mutex Impl | host tests |
| R12 | Stale/invalid data cannot authorise actuation | `protection_core.cpp`, `measurement_core.cpp` | `stale_*`, `silence_*`, `nan_*` | Done | host tests |
| R13 | FreeRTOS task separation | `main.cpp` | - | Impl | inspection, docs/architecture.md |
| R14 | MQTT/TLS with CA verification | `mqtt_manager.cpp`, `hivemq_ca.h` | - | Impl/Author | inspection (no `setInsecure`) |
| R15 | Topics consistent with device ID | `energy_topics.h` | `topics_are_derived_*`, `device_id_validation_*` | Done | host tests |
| R16 | Telemetry schema valid, labelled | `telemetry_json.cpp` | `telemetry_*` | Done | host tests |
| R17 | Blynk telemetry | `main.cpp` | - | Impl/Author | inspection |
| R18 | Reconnect behaviour (Wi-Fi/Blynk/MQTT) | `main.cpp`, `mqtt_manager.cpp` | - | Impl/Author | inspection |
| R19 | Task watchdog + heartbeat | `watchdog_manager.cpp`, `heartbeat_monitor.cpp`, `main.cpp` | `heartbeat_*` (logic only) | Logic Done; TWDT Impl/Author | host tests; no target test |
| R20 | Serial diagnostics | `diagnostics.cpp` | - | Impl/Author | inspection |
| R21 | Secrets not committed | `.gitignore`, `scripts/scan_secrets.py` | CI scan | Done | scanner; history inspected |
| R22 | Reproducible build | `platformio.ini`, `.github/workflows/ci.yml` | CI | Gap until CI is green; versions unpinned | see release checklist |
| R23 | Calibrated accuracy | - | - | Gap | not claimed |
| R24 | Persistence of faults/energy across reset | - | - | Gap | roadmap |
| R25 | Relay contact feedback | - | - | Gap | roadmap |
