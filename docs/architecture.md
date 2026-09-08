# System Architecture

## 1. Overview

The firmware is organized around independent FreeRTOS tasks and narrow module responsibilities.

```text
                  +----------------------+
                  |   PZEM / Simulation  |
                  +----------+-----------+
                             |
                             v
                    +----------------+
                    |  SensorTask    |
                    +-------+--------+
                            |
                    FreeRTOS Queue
                            |
                            v
                    +----------------+
                    |  ControlTask   |
                    +-------+--------+
                            |
             +--------------+--------------+
             |                             |
             v                             v
      Protection State                Command Queue
             |
             +----------+
             |          |
             v          v
         Actuator    Fault Manager

+----------------+       +----------------+
| DisplayTask    |       | NetworkTask    |
| LCD            |       | Wi-Fi/Blynk    |
+----------------+       | MQTT/TLS       |
                         +----------------+

                    +----------------+
                    | WatchdogTask   |
                    | Heartbeats     |
                    +----------------+
```

## 2. Task responsibilities

| Task | Core | Priority | Main responsibility |
|---|---:|---:|---|
| SensorTask | 1 | 3 | Acquire and queue measurements |
| ControlTask | 1 | 4 | Protection decisions and commands |
| DisplayTask | 1 | 1 | LCD presentation |
| NetworkTask | 0 | 2 | Wi-Fi, NTP, Blynk, MQTT |
| WatchdogTask | 0 | 5 | Heartbeat supervision |
| CliTask | 0 | 2 | Serial diagnostics |

The priority/core allocation is a design starting point and should be validated on the actual hardware under load.

## 3. Data ownership

- `measurement.cpp` owns measurement-source implementation.
- `protection.cpp` owns system state and fault state.
- `actuator.cpp` owns relay/LED outputs.
- `mqtt_manager.cpp` owns the MQTT client and callback.
- `watchdog_manager.cpp` owns heartbeat state.
- `main.cpp` owns task orchestration and inter-module scheduling.

## 4. Critical design rule

Network availability does not determine the safety state.

If Wi-Fi or MQTT fails:

```text
SensorTask -> Queue -> ControlTask -> Protection -> Actuator
```

continues to operate locally.

## 5. Command path

```text
MQTT broker
     |
     v
MQTT callback
     |
validate topic/payload
     |
     v
commandQueue
     |
     v
ControlTask
     |
     v
Protection reset/ACK
```

The callback never performs blocking or safety-critical control operations.

## 6. Fault path

```text
Invalid measurement
        |
        +----> SENSOR_INVALID
        |
        v
      FAULT
        |
        v
     Relay OFF
```

```text
Power > fault threshold
        |
        v
Confirmation timer
        |
     >= 2 seconds
        |
        v
    OVERPOWER
        |
        v
      FAULT
        |
        v
     Relay OFF
```
