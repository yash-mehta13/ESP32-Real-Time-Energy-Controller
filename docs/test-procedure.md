# Test Procedure

Evidence levels used throughout this repository:

| Level | Meaning |
|---|---|
| **Inspected** | Confirmed by reading the code |
| **Host-tested** | Automated test on a development machine (no ESP32, no mains) |
| **Built** | Firmware compiled for the ESP32 target (CI) |
| **Simulated on target** | Run on an ESP32 with `esp32dev_sim`, no load |
| **Bench-tested** | Run on hardware with a real load. Requires a committed record |

## 1. Prerequisites

- `g++` (C++11) and `make` for host tests.
- PlatformIO Core for firmware builds (`pip install platformio`).
- `src/config.h` copied from `src/config.example.h`.

## 2. Host unit tests (host-tested)

```bash
make test
```

Compiles `lib/energy_core` and `test/native` with `-Wall -Wextra -Werror
-pedantic` and runs them. Output ends with
`N tests, 0 failed (host build, simulated/mocked inputs only)`. Covered:
threshold boundaries and timing (including `millis()` rollover), measurement
validity, latch/ack/reset/interlock, fail-closed lock-failure result, output
mapping per state and polarity, simulation never driving the relay pin, MQTT
command parsing, topic/ID derivation, telemetry JSON and heartbeat logic.

Not covered by host tests: real PZEM communication, GPIO, FreeRTOS scheduling,
the ESP-IDF watchdog, Wi-Fi/TLS/Blynk, the LCD.

## 3. Firmware builds (built)

```bash
cp src/config.example.h src/config.h      # placeholders only
pio run -e esp32dev_sim
pio run -e esp32dev_pzem
```

Both must succeed. Record `pio pkg list -e <env>` output with any reported
result so toolchain versions are known.

## 4. Simulation on target (no load connected)

Flash `esp32dev_sim`. Open the monitor (`pio device monitor`).

1. Boot banner shows `Measurement source: SIMULATION | Relay GPIO output: DISABLED`.
2. LCD shows `S` in the last column.
3. Over 15 s: NORMAL (40 W) -> WARNING (500 W) -> FAULT `OVERPOWER` ~2 s into
   the 600 W phase.
4. `status` shows `Relay (commanded): OFF` after FAULT and
   `Relay GPIO output: DISABLED`. With a multimeter on the relay input pin the
   level must never reach the relay's ON level in any phase (optional check).
5. `ack`: fault stays, `Fault ACK: YES`.
6. `reset` while the profile is in its 600 W phase: `REJECTED_UNSAFE_CONDITION`.
   `reset` during the 40 W phase: `ACCEPTED`, state INIT; next sample NORMAL.
7. Publish `reset` / `ack` to the command topics (use a client with your
   credentials) and observe the same outcomes in the serial log.

## 5. Network resilience (simulated on target)

Disconnect the access point: SensorTask/ControlTask/protection keep running,
state still changes, NetworkTask retries. Restore: MQTT reconnects, state and
fault are republished. Block only MQTT: protection continues; telemetry resumes.

## 6. Watchdog (development only, never with a live load)

Temporarily stall a task in a scratch build. Expect the `WATCHDOG FAILURE`
banner, the safe output driven, then a restart. This path is **not covered by
automated tests** (needs the target).

## 7. Bench validation with a real PZEM and relay

Only by a person qualified for the electrical work involved, after reading
docs/safety-and-limitations.md and docs/hardware-and-wiring.md. Use a
current-limited or fused supply, an enclosure and a non-hazardous test load
(for example a resistive lamp) first.

Create a record under `docs/validation/` (one file per session) with:

- date, firmware commit (`git rev-parse HEAD`), environment, toolchain list;
- wiring/photo reference, relay module model, **verified relay polarity**;
- reference meter model and calibration status, load description, ambient
  conditions;
- table of reference reading versus PZEM reading and the error;
- for each protection step: the commanded input, the observed serial log,
  and a measurement showing the load actually de-energised (for example
  reference meter reading 0 W);
- PZEM unplugged behaviour (expect `SENSOR_INVALID` fault, relay OFF);
- boot behaviour: relay stays OFF from power-on until the first valid sample
  (observe with the load connected only through a safe test arrangement).

A claim of hardware validation is only treated as reproducible evidence when
such a record is committed.

## 8. Repository checks

```bash
python3 scripts/check_docs.py     # relative links / image paths
python3 scripts/scan_secrets.py   # non-placeholder credentials, key blocks
git check-ignore -v src/config.h  # config.h must be ignored
```

If credentials were ever committed, rotate them; removing them from the latest
tree is not sufficient.

## 9. Results recorded for this revision

See `CHANGELOG.md` and docs/release-checklist.md for the commands run in the
last audit and their outcomes.
