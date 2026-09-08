# Fault Management

## States

```text
INIT
  |
  v
NORMAL <----> WARNING
  |             |
  |             |
  +-------------+
        |
        v
      FAULT
```

`FAULT` is latched.

## Fault codes

| Code | Meaning | Relay |
|---|---|---|
| `NONE` | No active fault | State dependent |
| `SENSOR_INVALID` | Measurement contains invalid data | OFF |
| `OVERPOWER` | Power remained above fault threshold | OFF |

## Thresholds

```text
Warning threshold = 485 W
Fault threshold   = 550 W
Confirmation      = 2000 ms
```

## Overpower behavior

A single sample above 550 W does not immediately latch the fault.

Instead:

```text
Power > 550 W
     |
     v
Start timer
     |
     +---- drops below threshold --> cancel timer
     |
     +---- remains above threshold for 2 s
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

## Sensor failure

An invalid measurement immediately produces:

```text
SENSOR_INVALID -> FAULT -> Relay OFF
```

## Acknowledgement

ACK is an operator acknowledgement.

It does **not** clear the fault.

```text
FAULT
  |
  +---- ACK ----> FAULT + acknowledged=true
```

## Reset

Reset clears the active fault and returns the system to `INIT`.

The next valid measurement determines whether the system becomes NORMAL or WARNING.

```text
FAULT
  |
 RESET
  v
INIT
  |
 next measurement
  v
NORMAL / WARNING / FAULT
```

## Fail-safe principle

Any state outside the explicitly allowed operating states results in the actuator's safe behavior:

```text
Relay OFF
```

The relay is also initialized OFF during boot.
