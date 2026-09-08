# Test Procedure

## 1. Static repository checks

Confirm:

```text
src/config.h
```

is ignored by Git.

```bash
git check-ignore -v src/config.h
git status --ignored
```

Never publish real credentials.

## 2. Build test

```bash
pio run
```

Expected:

```text
SUCCESS
```

The build must complete without compiler warnings that indicate broken APIs or unsafe conversions.

## 3. Simulation protection test

Keep:

```cpp
MeasurementSource::SIMULATION
```

Expected sequence:

```text
NORMAL
  -> WARNING
  -> WARNING
  -> FAULT
```

The 600 W phase should latch `OVERPOWER` after approximately 2 seconds above the threshold.

Expected actuator behavior:

```text
NORMAL  -> Relay ON
WARNING -> Relay ON
FAULT   -> Relay OFF
```

## 4. Fault ACK

Send:

```text
ack
```

Expected:

```text
Fault remains active
Acknowledged = YES
Relay remains OFF
```

## 5. Fault reset

Send:

```text
reset
```

Expected:

```text
Fault cleared
State = INIT
Relay = OFF
```

After the next valid low-power measurement:

```text
State = NORMAL
Relay = ON
```

## 6. MQTT command test

Publish:

```text
reset
```

to:

```text
energy/device01/cmd/reset
```

Verify the serial log reports that the remote command was received and executed by ControlTask.

Test ACK similarly:

```text
energy/device01/cmd/ack
```

## 7. TLS test

Verify that the firmware connects with:

```text
setCACert(HIVEMQ_ROOT_CA)
```

and contains no `setInsecure()` call.

The system clock must be synchronized before MQTT connects.

## 8. Wi-Fi recovery

Disconnect the access point or temporarily make the network unavailable.

Verify:

- SensorTask continues.
- ControlTask continues.
- Protection continues.
- Relay behavior remains locally controlled.
- NetworkTask retries connection.
- MQTT reconnects after Wi-Fi returns.

## 9. MQTT recovery

Stop MQTT connectivity while keeping Wi-Fi active.

Verify:

- Protection continues.
- Telemetry resumes after reconnection.
- State/fault are republished after a fresh MQTT connection.

## 10. Watchdog test

For a controlled development test only, intentionally prevent a task from updating its heartbeat.

Expected:

```text
WATCHDOG FAILURE
        |
        v
system restart
```

Do not perform this test while controlling a dangerous or unattended electrical load.

## 11. Real PZEM test

After verifying the complete simulation path:

1. Power down.
2. Verify PZEM wiring.
3. Change the source to `PZEM`.
4. Build and upload.
5. Start with a known safe load.
6. Compare readings with a trusted meter.
7. Verify invalid/no-PZEM behavior.
8. Verify protection thresholds.

## 12. Final GitHub security check

Before making the repository public:

```bash
git grep -n "YOUR_WIFI"
git grep -n "YOUR_MQTT"
git grep -n "BLYNK_AUTH"
```

Also inspect:

```bash
git status
git log --all --oneline
```

If credentials were previously committed to Git history, rotating them is necessary; removing them only from the latest working tree is not sufficient.
