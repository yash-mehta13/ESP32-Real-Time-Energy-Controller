# Measurement Pipeline

## Overview

The measurement pipeline is responsible for acquiring electrical measurements from the PZEM-004T, validating and processing the data, and distributing measurements to other parts of the firmware.

The implementation uses FreeRTOS inter-task communication rather than directly sharing sensor data between tasks.

## Data Flow

```text
PZEM-004T
     |
     | UART
     v
 SensorTask
     |
     | Measurement structure
     v
FreeRTOS Queue
     |
     v
 ControlTask
     |
     v
System State Logic
```

The latest measurement is also maintained as a shared snapshot for tasks that require the most recent sensor values.

```text
SensorTask
     |
     v
latestMeasurement
     |
     | protected by mutex
     |
     +------------+-------------+
     |                          |
     v                          v
DisplayTask                NetworkTask
     |                          |
     v                          v
  I2C LCD                   Blynk
```

## Measurement Structure

The firmware uses the following structure:

```cpp
struct Measurement
{
    float voltage;
    float current;
    float power;
    float energy;
    float apparentPower;
    float powerFactor;
    bool valid;
    unsigned long timestamp;
};
```

### Parameters

| Parameter       | Unit | Description                     |
| --------------- | ---- | ------------------------------- |
| `voltage`       | V    | RMS voltage                     |
| `current`       | A    | RMS current                     |
| `power`         | W    | Real electrical power           |
| `energy`        | kWh  | Accumulated energy              |
| `apparentPower` | VA   | Calculated apparent power       |
| `powerFactor`   | —    | Ratio of real to apparent power |
| `valid`         | —    | Measurement validity flag       |
| `timestamp`     | ms   | Acquisition timestamp           |

## Apparent Power

Apparent power is calculated using:

```text
S = V × I
```

where:

* `S` = apparent power in VA
* `V` = voltage in V
* `I` = current in A

The PZEM-provided real power value is retained separately.

## Power Factor

Power factor is calculated using:

```text
PF = P / S
```

where:

* `P` = real power
* `S` = apparent power

The calculated value is constrained to the range:

```text
0 ≤ PF ≤ 1
```

## Measurement Validation

The sensor task checks the acquired values for invalid numerical results.

A measurement is marked invalid if any required measurement contains `NaN`.

The validity flag is then propagated with the measurement structure so that downstream tasks can determine whether the data is usable.

## FreeRTOS Queue

The measurement queue acts as the communication channel between the sensor and control tasks.

```text
SensorTask
    |
    | xQueueSend()
    v
Measurement Queue
    |
    | xQueueReceive()
    v
ControlTask
```

The queue is created with space for multiple `Measurement` structures.

This implements a producer-consumer architecture:

* **Producer:** SensorTask
* **Consumer:** ControlTask

If the queue cannot accept a new measurement within the configured timeout, the sensor task reports a queue-full warning.

## Mutex-Protected Snapshot

A separate `latestMeasurement` object stores the most recent measurement.

Because this object is accessed by multiple FreeRTOS tasks, a mutex protects access to it.

```text
SensorTask
    |
    | lock mutex
    v
latestMeasurement
    |
    | unlock mutex
    v

       +----------------+
       |                |
       v                v
 DisplayTask       NetworkTask
```

The mutex prevents concurrent access to the shared measurement structure.

## Why Two Mechanisms?

The queue and mutex serve different purposes.

### Queue

Used when a measurement needs to be **transferred between tasks**.

```text
SensorTask → ControlTask
```

### Mutex

Used when multiple tasks need access to the **latest shared measurement snapshot**.

```text
SensorTask → latestMeasurement
                   ↓
          DisplayTask / NetworkTask
```

Using the two mechanisms separately keeps the communication model explicit.

## Timing

The sensor task acquires measurements every:

```text
1000 ms
```

The control task executes every:

```text
100 ms
```

The display task executes every:

```text
500 ms
```

The network task services Blynk continuously with a short task delay and sends telemetry every:

```text
2000 ms
```

## Validation Status

The measurement pipeline has been validated on physical ESP32 hardware.

The serial output confirmed:

* Measurement mutex creation
* Measurement queue creation
* SensorTask startup
* ControlTask startup
* Measurement transfer through the queue
* ControlTask processing
* System state transition
* DisplayTask startup
* NetworkTask startup

Example:

```text
[SYSTEM] Measurement mutex created
[SYSTEM] Measurement queue created
[ControlTask] Started
[ControlTask] V=236.6V | I=36.67A | P=477.2W | S=8675.2VA | PF=0.055 | E=1.375kWh | Valid=1
[SYSTEM] INIT -> NORMAL
[NetworkTask] Started
[SYSTEM] All RTOS tasks created
[SYSTEM] Queue pipeline active
[DisplayTask] Started
```

## Current Limitation

The current pipeline provides measurement acquisition, processing, synchronization, and monitoring.

The next stage will introduce a dedicated protection state machine with warning thresholds, fault persistence, and fault handling.
