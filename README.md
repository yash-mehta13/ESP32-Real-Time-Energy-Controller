# ESP32 Real-Time Energy Controller

FreeRTOS-based embedded energy monitoring and control firmware built on the ESP32.

The project is being developed as an industrial-style embedded firmware system with a focus on **real-time task scheduling, sensor acquisition, inter-task communication, system monitoring, fault handling, and reliable control architecture**.

## Current Features

* ESP32-based embedded firmware
* FreeRTOS task architecture
* PZEM-004T energy measurement
* UART communication
* 16x2 I2C LCD
* Blynk cloud telemetry
* Real-time measurement processing
* Mutex-protected shared measurement data
* FreeRTOS queue-based inter-task communication
* Basic system-state management

## System Architecture

```text
                         ESP32
                           |
          +----------------+----------------+
          |                |                |
          v                v                v
     SensorTask       ControlTask      NetworkTask
       Core 1           Core 1            Core 0
          |                |                |
          |                |                +----> Blynk
          |                |
          v                v
    Measurement ----> Control Logic
          |
          v
   Latest Measurement
          |
          v
     DisplayTask
          |
          v
        I2C LCD
```

## Hardware

| Component    | Interface | Purpose                  |
| ------------ | --------- | ------------------------ |
| ESP32        | —         | Main microcontroller     |
| PZEM-004T v3 | UART      | Electrical measurements  |
| 16x2 LCD     | I2C       | Local monitoring         |
| Green LED    | GPIO      | Normal status            |
| Red LED      | GPIO      | Warning/fault indication |

## Firmware Tasks

| Task        | Function                       | Period | Priority |   Core |
| ----------- | ------------------------------ | -----: | -------: | -----: |
| SensorTask  | Sensor acquisition             |    1 s |        3 | Core 1 |
| ControlTask | Measurement processing/control | 100 ms |        4 | Core 1 |
| DisplayTask | LCD update                     | 500 ms |        1 | Core 1 |
| NetworkTask | Cloud communication            |  10 ms |        2 | Core 0 |

## Inter-Task Communication

The firmware uses a FreeRTOS queue to transfer measurement data from the sensor task to the control task.

```text
PZEM-004T
    |
    v
SensorTask
    |
    v
FreeRTOS Queue
    |
    v
ControlTask
```

A mutex protects the latest measurement snapshot accessed by other tasks.

```text
SensorTask
    |
    v
Latest Measurement
    |
    +------> DisplayTask
    |
    +------> NetworkTask
```

## Measurement Data

The firmware maintains the following measurement parameters:

* Voltage
* Current
* Real Power
* Energy
* Apparent Power
* Power Factor
* Measurement validity
* Timestamp

## Development Status

### Completed

* [x] ESP32 hardware initialization
* [x] PZEM-004T UART communication
* [x] FreeRTOS task architecture
* [x] Measurement structure
* [x] FreeRTOS measurement queue
* [x] Mutex-protected measurement snapshot
* [x] LCD monitoring
* [x] Blynk telemetry
* [x] Basic system-state evaluation

### Upcoming

* [ ] Protection state machine
* [ ] Fault persistence and debounce
* [ ] Fault manager
* [ ] Watchdog supervision
* [ ] UART diagnostic interface
* [ ] MQTT telemetry
* [ ] Relay/load disconnect
* [ ] Fault-injection testing
* [ ] Long-duration validation

## Documentation

* [Firmware Architecture](docs/architecture.md)

## Development Approach

The project is being developed incrementally. Each major firmware feature is implemented, tested on physical hardware, documented, and committed to the repository before moving to the next stage.

## Author

**Yash Mehta**
Electronics & Communication Engineering
VIT Vellore
