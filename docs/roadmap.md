# Roadmap

## Verified today (see requirements-traceability.md)

Host-tested decision logic for thresholds, latching, reset interlock,
fail-closed lock failure, measurement validity, output gating, command
parsing, topics, telemetry and heartbeat timing; simulation builds that cannot
drive the relay pin.

## Needed to become a validated prototype

1. Run and record the first green CI build of both environments; pin the
   `platform` and library versions.
2. Bench-validation records under `docs/validation/` (reference meter,
   conditions, errors; relay opens observed with an independent measurement;
   boot/reset behaviour; PZEM-unplugged behaviour).
3. Hardware-in-the-loop watchdog tests (stalled Control/Sensor task).
4. Verify the plausibility and consistency limits against real meter data.
5. Hardware: pull resistor for the relay driver, relay feedback input (aux
   contact or sensing of the load side), independent fuse/breaker.

## Candidate improvements (not started)

- Persist the latched fault and energy offset across reset (NVS), with a
  rule that a persisted fault still requires the reset interlock.
- Optional MQTT command-result topic (non-retained) so remote operators see
  rejected resets without serial access.
- Wall-clock timestamp in telemetry once NTP state is part of the snapshot.
- Overcurrent / voltage window protection with their own thresholds.
- Message nonce or signed commands if the broker cannot be trusted.
- Replace the hold-off heuristic once the MQTT client exposes the retain flag.
- A PlatformIO `native` test environment (Unity) if the registry is available
  to CI; the dependency-free harness in `test/native` would port directly.
- Choose and add a license.
