# Firmware Architecture

## Overview

The ESP32 Real-Time Energy Controller uses a FreeRTOS-based architecture to separate sensing, control, display, and network operations into independent tasks.

The objective is to prevent sensor acquisition, control processing, display updates, and network communication from being tightly coupled inside a single execution loop.

## Current Task Architecture

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

## FreeRTOS Tasks

### SensorTask

Responsible for:

* Reading measurements from the PZEM-004T.
* Calculating apparent power.
* Calculating power factor.
* Validating measurement data.
* Publishing measurements through the FreeRTOS queue.
* Updating the latest measurement snapshot.

### ControlTask

Responsible for:

* Receiving measurements from the FreeRTOS queue.
* Processing measurement data.
* Evaluating the current system state.
* Updating system status indicators.

### DisplayTask

Responsible for:

* Reading the latest measurement snapshot.
* Updating the 16x2 I2C LCD.

### NetworkTask

Responsible for:

* Maintaining Blynk communication.
* Uploading electrical measurements to the cloud.
* Running network-related operations independently from sensing and control.

## Task Configuration

| Task        | Period | Priority |   Core |
| ----------- | -----: | -------: | -----: |
| SensorTask  |    1 s |        3 | Core 1 |
| ControlTask | 100 ms |        4 | Core 1 |
| DisplayTask | 500 ms |        1 | Core 1 |
| NetworkTask |  10 ms |        2 | Core 0 |

## Design Principles

The current firmware follows several embedded-system design principles:

* Separation of system functions using RTOS tasks.
* Priority-based task scheduling.
* Core assignment on the ESP32.
* Periodic task execution using FreeRTOS timing primitives.
* Inter-task communication using FreeRTOS queues.
* Protected access to shared measurement data using a mutex.
* Separation of network processing from local sensing and control.

## Current Status

The FreeRTOS task architecture has been implemented and validated on physical ESP32 hardware.

The next stage is to strengthen the measurement pipeline and implement a robust protection state machine.
