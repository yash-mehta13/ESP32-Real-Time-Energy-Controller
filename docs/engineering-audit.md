# Engineering Audit

Audit of the repository as received (commit `f9151b2`), the defects confirmed by
reading the code, and what was changed. "Verified" below means confirmed by
reading source or by a host-side test in `test/native`. Nothing in this
document is a physical measurement.

## 1. Implementation inventory (as received)

| Area | Status as received | Evidence |
|---|---|---|
| FreeRTOS tasks (Sensor, Control, Display, Network, Watchdog, Cli) | Implemented | `src/main.cpp` |
| Measurement from PZEM or simulation | Implemented; source chosen by a `constexpr` in `config.h` | `src/measurement.cpp` |
| Protection state machine (INIT/NORMAL/WARNING/FAULT) | Implemented, no tests | `src/protection.cpp` |
| Relay/LED actuator | Implemented, no simulation guard | `src/actuator.cpp` |
| MQTT over TLS with CA verification, LWT, bounded callback | Implemented | `src/mqtt_manager.cpp` |
| Blynk telemetry (V0-V5) | Implemented | `src/main.cpp` |
| NTP before TLS | Implemented | `src/main.cpp` |
| Task watchdog + heartbeat supervisor | Implemented | `src/watchdog_manager.cpp` |
| Serial CLI | Implemented | `src/diagnostics.cpp` |
| Automated tests | None | - |
| CI | None | - |
| Hardware validation | Author-reported in README, no committed records | `README.md` |
| License | None in repository | - |

Build status of the received tree: not reproducible from the repository alone
(no environments, no lockfile, source selected by editing a header).

## 2. Defects found and fixed

| # | Finding | Severity | Fix | Test |
|---|---|---|---|---|
| 1 | **Protection lock failure left the relay ON.** `protectionUpdate()` returned `FAULT` with `stateChanged == faultChanged == false` when the mutex could not be taken, and `applyProtectionResult()` only drove the actuator when one of those flags was set. | High | Lock failure returns a dedicated fail-closed result (`synchronizationFailed`, state FAULT, `PROTECTION_INTERNAL`); the actuator is driven on every result; the next successful call latches the internal fault. | `lock_failure_result_is_fault_with_safe_outputs_even_if_flags_ignored`, `internal_fault_*` |
| 2 | **Reset could re-energise the load while the hazard persisted.** `protectionReset()` cleared the fault unconditionally. The next sample above 550 W started the confirmation timer and the state became WARNING, which switches the relay ON for up to 2 s. | High | Reset is refused unless a fresh valid sample shows power at or below the fault threshold; after an accepted reset the first sample may not open a confirmation window (immediate re-latch). | `reset_rejected_*`, `after_reset_overpower_relatches_immediately_without_warning_window` |
| 3 | **Same re-energise window after boot** (watchdog restart, brownout) with an overload already present. | Medium | Same first-sample rule applied at `begin()` and after mains returns. | `boot_with_overpower_present_latches_immediately`, `mains_returning_onto_overload_latches_immediately` |
| 4 | **Simulation could drive the real relay.** The default configuration selects simulation, and `actuatorApplyState()` wrote the relay GPIO regardless of source. | High | Source is selected by a PlatformIO environment; simulation builds use `RelayOutputMode::HOLD_OFF`, so the relay pin only ever receives its OFF level. `#error` guards reject ambiguous flags. | `simulation_mode_never_drives_the_relay_pin_on` |
| 5 | **Stale data accepted indefinitely.** ControlTask only reacted when a sample arrived; if SensorTask stalled the last state (possibly relay ON) persisted until the watchdog fired. | Medium | `ProtectionCore::tick()` runs every control loop; no usable sample for 3 s latches `SENSOR_STALE`. Samples older than 3 s are rejected. | `silence_is_detected_by_tick`, `stale_sample_latches_stale_fault` |
| 6 | **Invalid values could be fabricated.** Derived values collapsed to 0 on bad input and the zero-initialised `Measurement{}` had `status` implicitly OK. Power factor was recomputed as P/S and clamped, hiding inconsistency. | Medium | Explicit status enum (default NOT_INITIALIZED), numeric fields zeroed on invalid, meter power factor used, plausibility and P-versus-S consistency checks. | `test_measurement.cpp` |
| 7 | **`relay_on` in telemetry and CLI looked like a physical fact.** No contact feedback exists. | Low | Renamed `relay_commanded_on`, added `relay_output_enabled`; documented. | `telemetry_contains_source_validity_and_commanded_relay` |
| 8 | **Topic and device ID could drift.** `MQTT_DEVICE_ID` and six literal topics were separate constants. | Low | Topics derived from `MQTT_DEVICE_ID` at compile time; ID validated by `static_assert`. | `topics_are_derived_from_device_id` |
| 9 | **Command validation used `strcmp` on a truncated copy.** A payload with an embedded NUL (`reset\0...`) matched. | Low | Exact byte-length comparison, <= 16 byte limit. | `embedded_nul_does_not_pass_as_reset` |
| 10 | **A retained command would run on reconnect.** PubSubClient does not expose the retain flag. | Medium | Commands inside a 2 s hold-off after subscribing are dropped (heuristic, documented). | `holdoff_window_drops_early_commands_and_is_rollover_safe` |
| 11 | **Watchdog registration errors reported as success.** `ESP_ERR_INVALID_ARG` was treated as OK. | Low | Only OK and INVALID_STATE (already registered) succeed; failures are counted and shown by `watchdog`. | - (hardware API) |
| 12 | **Relay left in last state during a heartbeat-triggered restart.** | Medium | WatchdogTask drives the safe output before `esp_restart()`. External pull-up/down still required for resets it cannot cover. | - (needs hardware) |
| 13 | **Telemetry JSON via `snprintf` without escaping; NaN would print as `nan`.** | Low | Bounded JSON writer with escaping and `null` for non-finite numbers. | `test_telemetry_heartbeat.cpp` |
| 14 | **LCD boot text 17 characters on a 16 column display.** | Cosmetic | Shortened to `RT Energy Ctrl`. | - |
| 15 | **Cross-task plain `bool` flags** (`wifiConnected` etc.). | Low | `std::atomic<bool>`. | - |
| 16 | **`config.h` committed once (`7df609f`).** | Info | History inspected: that file contained placeholders only (7 `YOUR_` placeholders, no real values). No credentials found. Not rewritten. | `scripts/scan_secrets.py` |

## 3. Documentation inconsistencies found

- README claimed the source is chosen by `MEASUREMENT_SOURCE` in `config.h`
  and `test-procedure.md` referred to `MeasurementSource::SIMULATION` as a
  setting; both are now replaced by the build environment.
- README table "PZEM RX | 16" is ambiguous: GPIO16 is the **ESP32's** UART2 RX
  (wire it to the PZEM TX pin). The tables now say so.
- README stated "Step 8 complete - production firmware". The project is an
  educational prototype; production qualification is not claimed.
- Fault documentation said reset "clears the active fault" without any
  interlock; updated to the implemented behaviour.

## 4. Open items and risks

- **No ESP32 firmware build could be run in the audit environment** (the
  PlatformIO registry host was not reachable). The firmware adapter files
  (all of `src/*.cpp`) were only syntax-checked against hand-written SDK
  stubs and the real PZEM header. Run `pio run -e
  esp32dev_sim -e esp32dev_pzem` (CI does this) before relying on them.
- The first CI firmware build exposed a macro collision (`DISABLED` from the
  arduino-esp32 core) that the stub-based syntax check could not see; fixed,
  and the host build now includes the core's global macros.
- Toolchain versions are not pinned (CI resolved espressif32 8.0.0 /
  arduino-esp32 3.3.12). `watchdog_manager.cpp` needs the
  arduino-esp32 3.x API (a compile-time guard states this). Pin the platform
  after the first green CI run.
- Fault state is RAM only: a reset clears the latch (mitigated by the
  immediate re-latch rule, but the relay's reset-time state is hardware).
- ESP32 GPIO floats during reset: an external resistor must hold the relay
  driver OFF (docs/hardware-and-wiring.md).
- If the PZEM sits **downstream** of the relay, opening the relay makes
  measured power drop to about zero, so the reset interlock cannot prove the
  overload is gone.
- Plausibility bounds and the P-versus-S tolerance are engineering choices,
  not meter specifications, and have not been exercised on hardware.
- No relay contact feedback; "commanded" is not "open".
- ISRG Root X1 (embedded CA) expires in 2035 and the broker chain can change.
- No license file: terms of reuse are undefined.
