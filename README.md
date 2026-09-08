# ESP32 Real-Time Energy Controller

A production-oriented ESP32 firmware project that combines real-time measurement, threshold-based protection, fail-safe actuation, FreeRTOS task architecture, watchdog supervision, Blynk telemetry, and secure MQTT connectivity through HiveMQ Cloud.

> **Current stage:** Step 8 production firmware / repository hardening  
> **Measurement:** PZEM-004T v3.0 or deterministic simulation  
> **RTOS:** FreeRTOS through the ESP32 Arduino framework

## Why this project is different

This is designed as an embedded firmware system rather than a simple IoT dashboard.

The protection path is independent of cloud connectivity:

```text
PZEM / Simulation
       |
       v
   SensorTask
       |
 Measurement Queue
       |
       v
   ControlTask
       |
       +----> Protection State Machine
       |              |
       |              +----> Fail-safe Relay
       |              +----> Fault Manager
       |
       +----> LCD / LEDs

NetworkTask
       |
       +----> Blynk
       |
       +----> MQTT/TLS
                    |
                    v
                HiveMQ Cloud

WatchdogTask
       |
       +----> Task heartbeats
       +----> Recovery / restart
```

## Features

- ESP32 + FreeRTOS task-based firmware
- PZEM-004T v3.0 measurement support
- Measurement-source abstraction for hardware/simulation testing
- Voltage, current, power, energy, apparent power and power factor
- Overpower warning threshold
- Time-confirmed overpower fault
- Latched fault handling
- Local ACK and RESET commands
- MQTT remote ACK and RESET commands
- Secure MQTT over TLS port 8883
- CA certificate verification; no `setInsecure()`
- NTP synchronization before MQTT TLS connection
- MQTT Last Will and Testament availability status
- Narrow command topics
- Blynk telemetry
- 16x2 I2C LCD
- Normal/alert indicators
- Fail-safe relay behavior
- FreeRTOS queues and mutexes
- Task watchdog + application heartbeat supervisor
- Serial diagnostics CLI
- Reconnect handling for Wi-Fi, Blynk and MQTT

## Repository

```text
ESP32-Real-Time-Energy-Controller/
|
├── src/
│   ├── main.cpp
│   ├── config.h
│   ├── config.example.h
│   ├── hivemq_ca.h
│   ├── measurement.h
│   ├── measurement.cpp
│   ├── protection.h
│   ├── protection.cpp
│   ├── actuator.h
│   ├── actuator.cpp
│   ├── mqtt_manager.h
│   ├── mqtt_manager.cpp
│   ├── watchdog_manager.h
│   ├── watchdog_manager.cpp
│   ├── diagnostics.h
│   └── diagnostics.cpp
|
├── docs/
│   ├── architecture.md
│   ├── measurement-pipeline.md
│   ├── mqtt-protocol.md
│   ├── fault-management.md
│   └── test-procedure.md
|
├── README.md
├── .gitignore
└── platformio.ini
```

## Hardware

| Function | GPIO |
|---|---:|
| PZEM RX | 16 |
| PZEM TX | 17 |
| Normal LED | 18 |
| Alert LED | 19 |
| Relay | 23 |
| I2C LCD | SDA/SCL default ESP32 pins |
| Serial monitor | 115200 baud |
| PZEM UART | 9600 baud |

**Relay note:** GPIO23 is a software-configurable default. Verify the relay driver circuit and active level before connecting a real load.

## Protection policy

| Condition | State | Relay |
|---|---|---|
| Startup / low-voltage initialization | INIT | OFF |
| Power <= 485 W | NORMAL | ON |
| 485 W < power <= 550 W | WARNING | ON |
| Power > 550 W for >= 2 s | FAULT | OFF |
| Invalid measurement | FAULT | OFF |

Faults are latched. An explicit `reset` command is required to clear them.

## Measurement source

The project retains both hardware and simulation paths:

```cpp
constexpr uint8_t MEASUREMENT_SOURCE = 1;
```

For real hardware:

```cpp
constexpr uint8_t MEASUREMENT_SOURCE = 0;
```

The ControlTask does not know which source produced a measurement. Both paths feed the same queue and protection logic.

## Security

Copy:

```text
src/config.example.h
```

to:

```text
src/config.h
```

and fill in your own credentials.

`src/config.h` is ignored by Git.

Never commit:

- Wi-Fi passwords
- Blynk tokens
- MQTT passwords
- private keys

The MQTT client validates the HiveMQ TLS certificate using the public ISRG Root X1 CA.

## MQTT

### Telemetry

```text
energy/device01/telemetry
```

Example:

```json
{
  "device_id": "device01",
  "firmware_version": "1.2.0",
  "voltage": 230.00,
  "current": 2.170,
  "power": 500.00,
  "energy": 0.000,
  "apparent_power": 499.10,
  "power_factor": 1.000,
  "valid": true,
  "state": "WARNING",
  "fault": "NONE",
  "fault_active": false,
  "fault_acknowledged": false,
  "relay_on": true,
  "source": "SIMULATION",
  "uptime_ms": 123456
}
```

### Commands

```text
energy/device01/cmd/reset
energy/device01/cmd/ack
```

Payloads:

```text
reset
ack
```

The MQTT callback performs only validation and queueing. It does not directly manipulate protection state.

## Serial CLI

At 115200 baud:

```text
help
status
measure
fault
watchdog
tasks
uptime
reset
ack
version
```

## Build

Install PlatformIO, then:

```bash
pio run
```

Upload:

```bash
pio run --target upload
```

Monitor:

```bash
pio device monitor
```

The exact serial port may need to be selected in PlatformIO/VS Code.

## Engineering principles

1. **Protection is local.** Cloud services are not required for safe fault handling.
2. **ControlTask consumes measurements.** Source-specific logic stays in the measurement layer.
3. **MQTT callbacks are lightweight.** Commands are passed to ControlTask through a FreeRTOS queue.
4. **Faults are explicit and latched.**
5. **The actuator defaults to a safe OFF state.**
6. **Network failures are recoverable and must not stop the protection path.**
7. **Secrets are externalized from source control.**
8. **Watchdog supervision covers both the ESP task watchdog and application heartbeats.**

## Development progression

- Step 1–6: Embedded/RTOS foundations and control pipeline
- Step 7A: Measurement-source abstraction
- Step 7B: MQTT telemetry
- Step 7C: MQTT remote commands
- Step 7D: Production MQTT security
- Step 8: Hardware/firmware hardening, fail-safe actuation, testing and GitHub finalization
