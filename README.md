#  ESP32 Real-Time Energy Controller

**FreeRTOS-based embedded energy monitoring and control firmware built on ESP32.**

Designed as an **industrial-style embedded firmware system** with real-time task scheduling, sensor acquisition, inter-task communication, system monitoring, and fault-aware control architecture.

---

##  Features

*  ESP32 + **FreeRTOS** firmware architecture
*  **PZEM-004T v3** energy measurement via UART
*  FreeRTOS **Queue + Mutex** based task communication
*  Real-time voltage, current, power, energy & power factor monitoring
*  16×2 I²C LCD local display
*  Blynk cloud telemetry
*  Real-time system-state evaluation
*  Designed for fault handling, diagnostics & protection expansion

---

##  System Architecture

```text
                         ESP32
                           │
          ┌────────────────┼────────────────┐
          │                │                │
          ▼                ▼                ▼
     SensorTask       ControlTask      NetworkTask
       Core 1           Core 1            Core 0
          │                │                │
          │                ▼                └──► Blynk
          │          Control Logic
          │
          ▼
    Measurement
          │
          └──────────► Latest Snapshot
                           │
                           ▼
                      DisplayTask
                           │
                           ▼
                         LCD
```

### Task Configuration

| Task          | Purpose              | Period | Priority | Core |
| ------------- | -------------------- | -----: | -------: | ---: |
| `SensorTask`  | PZEM acquisition     |    1 s |        3 |    1 |
| `ControlTask` | Processing & control | 100 ms |        4 |    1 |
| `DisplayTask` | LCD update           | 500 ms |        1 |    1 |
| `NetworkTask` | Blynk communication  |  10 ms |        2 |    0 |

---

##  Measurement Pipeline

```text
PZEM-004T
    │ UART
    ▼
SensorTask
    │
    ▼
FreeRTOS Queue
    │
    ▼
ControlTask
    │
    ▼
System State Logic
```

A mutex-protected measurement snapshot is shared with the display and network tasks.

```text
              latestMeasurement
                     │
               Measurement Mutex
                 ┌───┴───┐
                 ▼       ▼
           DisplayTask  NetworkTask
                 │       │
                 ▼       ▼
                LCD     Blynk
```

**Queue** → transfers measurements between tasks
**Mutex** → protects shared measurement data

---

## 🔌 Hardware

| Component    | Interface | Purpose                 |
| ------------ | --------- | ----------------------- |
| ESP32        | —         | Main MCU                |
| PZEM-004T v3 | UART      | Electrical measurements |
| 16×2 LCD     | I²C       | Local monitoring        |
| Green LED    | GPIO      | Normal status           |
| Red LED      | GPIO      | Warning / fault         |

---

##  Measurements

The firmware processes:

`Voltage` · `Current` · `Real Power` · `Energy` · `Apparent Power` · `Power Factor`

with measurement validity and timestamps.

---

##  Development Status

### Completed

* [x] ESP32 hardware initialization
* [x] PZEM UART communication
* [x] FreeRTOS task architecture
* [x] Measurement data structure
* [x] FreeRTOS measurement queue
* [x] Mutex-protected measurement snapshot
* [x] LCD monitoring
* [x] Blynk telemetry
* [x] Basic system-state evaluation

### Roadmap

* [ ] Protection state machine
* [ ] Fault persistence & debounce
* [ ] Fault manager & diagnostics
* [ ] Watchdog supervision
* [ ] UART diagnostic interface
* [ ] MQTT telemetry
* [ ] Relay/load disconnect
* [ ] Fault-injection testing
* [ ] Long-duration validation

---

##  Documentation

* [Firmware Architecture](docs/architecture.md)
* [Measurement Pipeline](docs/measurement-pipeline.md)

---

##  Development Philosophy

The firmware is developed incrementally:

**Design → Implement → Compile → Flash → Hardware Test → Document → Commit**

Each major feature is validated on physical hardware before moving to the next stage.

---

##  Author

**Yash Mehta**
Electronics & Communication Engineering — VIT Vellore
