# Troubleshooting

| Symptom | Likely cause | Action |
|---|---|---|
| `fatal error: config.h: No such file` | `src/config.h` missing | `cp src/config.example.h src/config.h` and fill in values |
| `#error Select a build environment` | built without `-e` while overriding defaults, or flags removed | `pio run -e esp32dev_sim` or `-e esp32dev_pzem` |
| `#error Define exactly one of ...` | both source flags defined | remove the extra flag |
| `#error MQTT_DEVICE_ID must be defined` | old `config.h` with `constexpr char MQTT_DEVICE_ID[]` and literal topics | re-copy `config.example.h`; the ID is now a macro and topics are derived |
| `static assertion failed: MQTT_DEVICE_ID` | ID has `/`, `+`, `#`, space or is > 32 chars | use `[A-Za-z0-9_-]` |
| `#error ... requires arduino-esp32 3.x` | old Arduino core | use a platform providing arduino-esp32 3.x (ESP-IDF 5) |
| `source: SIMULATION` but PZEM wired | built the default environment | `pio run -e esp32dev_pzem -t upload` |
| Relay never turns ON | simulation build (by design), state INIT/FAULT, or polarity wrong | `status`: check `Relay GPIO output`, state, fault |
| Relay ON when it should be OFF (or inverted) | `RELAY_ON_LEVEL` does not match your module | disconnect all loads; verify polarity (hardware doc) |
| Immediate `SENSOR_INVALID` fault at boot with PZEM | meter not answering yet, wiring crossed (RX/TX), wrong baud/level | check `measure` (`SENSOR_ERROR`), wiring, supply; `reset` once valid |
| `INCONSISTENT` status | P larger than V x I by more than 10 % + 2 W | check CT/wiring/meter; widen `MeasurementLimits` only with justification |
| `OUT_OF_RANGE` | reading outside sanity bounds | check meter variant/range |
| `SENSOR_STALE` fault | SensorTask blocked or PZEM read too slow | check `tasks`, `watchdog`, PZEM read time; raise `MAX_MEASUREMENT_AGE_MS` only if justified |
| `reset` answers `REJECTED_*` | interlock | read the reason: `UNSAFE_CONDITION` (power still high), `INVALID`/`STALE` (fix the sensor first), `NO_ACTIVE_FAULT` |
| `ack` does nothing | by design | ack does not clear faults; use `reset` |
| LCD blank | wrong I2C address or contrast | try `0x3F`, adjust contrast pot, check SDA/SCL |
| LCD garbled / flicker | supply noise; `lcd.clear()` every 500 ms | check wiring/power |
| Wi-Fi never connects | credentials, 5 GHz-only AP | check SSID/password; ESP32 is 2.4 GHz only |
| NTP not syncing | blocked UDP 123, no DNS | MQTT will not connect until time is valid |
| MQTT `state=-2` / TLS failure | clock not synced, wrong cluster host, CA chain changed | check NTP, `MQTT_BROKER_HOST`, update `hivemq_ca.h` |
| MQTT `state=5` | bad credentials / ACL | check username/password and ACLs |
| Remote command ignored | wrong topic/payload, retained, inside 2 s hold-off, queue full | check the log line (`REJECTED_*`, hold-off, queue full) |
| Blynk not connecting | token/template mismatch | check `BLYNK_*` in `config.h`; this repo contains no dashboard definition: create datastreams V0-V5 yourself |
| `Command queue full` | ControlTask not draining | check `tasks`/`watchdog` |
| `WATCHDOG FAILURE` and restart | a task missed its heartbeat (6 s) | read the status table printed before restart |
| `Registration FAILED` | task not covered by the TWDT | investigate before relying on the watchdog |
| Spontaneous reboot when relay switches | supply brown-out | separate/stiffen relay supply |
