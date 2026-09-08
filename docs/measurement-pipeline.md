# Measurement Pipeline

## Source abstraction

The measurement layer exposes:

```cpp
Measurement readMeasurement();
```

The implementation selects either:

- `MeasurementSource::PZEM`
- `MeasurementSource::SIMULATION`

The downstream firmware does not branch on the source.

## Measurement structure

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
    uint32_t timestamp;
};
```

## Derived values

Apparent power:

```text
S = V × I
```

Power factor:

```text
PF = P / S
```

The calculated power factor is constrained to the physical range 0 to 1 for this monitoring model.

## Validation

A reading is accepted only when the numeric fields are finite and non-negative and the power factor is within 0 to 1.

Invalid PZEM values therefore propagate as:

```text
Measurement.valid = false
        |
        v
ControlTask
        |
        v
SENSOR_INVALID
        |
        v
FAULT
```

## Simulation profile

The deterministic simulation repeats every 15 seconds:

| Time | Voltage | Current | Power | Expected state |
|---|---:|---:|---:|---|
| 0–5 s | 230 V | 0.20 A | 40 W | NORMAL |
| 5–10 s | 230 V | 2.17 A | 500 W | WARNING |
| 10–15 s | 230 V | 2.60 A | 600 W | WARNING → FAULT after 2 s |

This permits protection testing without the physical PZEM.

## Real PZEM mode

Set:

```cpp
constexpr uint8_t MEASUREMENT_SOURCE = 0;
```

The UART configuration is:

```text
RX = GPIO16
TX = GPIO17
Baud = 9600
```

Verify the PZEM wiring, supply and UART level compatibility before connecting it.

## Important safety boundary

The PZEM is a measurement device. It is not treated as the protection mechanism itself. The firmware's protection state machine determines the relay command.
