# MQTT Protocol

## Transport

The firmware uses:

```text
MQTT over TLS
Port: 8883
```

The client verifies the broker certificate using the ISRG Root X1 trust anchor.

The firmware intentionally does **not** use:

```cpp
setInsecure();
```

NTP synchronization is required before the first TLS connection attempt because certificate validation depends on a valid system clock.

## Topics

### Telemetry

```text
energy/device01/telemetry
```

Published periodically.

### State

```text
energy/device01/status/state
```

Retained state:

```text
INIT
NORMAL
WARNING
FAULT
```

### Fault

```text
energy/device01/fault/code
```

Retained fault code:

```text
NONE
SENSOR_INVALID
OVERPOWER
```

### Availability

```text
energy/device01/status/availability
```

Expected values:

```text
online
offline
```

The MQTT Last Will and Testament publishes `offline` when the broker detects an ungraceful disconnect.

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

## Suggested broker permissions

For the device account, use the narrowest ACL possible.

Publish:

```text
energy/device01/telemetry
energy/device01/status/#
energy/device01/fault/#
```

Subscribe:

```text
energy/device01/cmd/#
```

Avoid granting unrestricted `#` access.

## Telemetry schema

```json
{
  "device_id": "device01",
  "firmware_version": "1.2.0",
  "voltage": 230.00,
  "current": 0.200,
  "power": 40.00,
  "energy": 0.000,
  "apparent_power": 46.00,
  "power_factor": 0.870,
  "valid": true,
  "state": "NORMAL",
  "fault": "NONE",
  "fault_active": false,
  "fault_acknowledged": false,
  "relay_on": true,
  "source": "SIMULATION",
  "uptime_ms": 123456
}
```

## Command processing

The MQTT callback:

1. Receives topic/payload.
2. Copies a bounded payload into a local buffer.
3. Validates exact topic and payload.
4. Converts it to an internal `CommandType`.
5. Sends it to `commandQueue`.
6. Returns immediately.

`ControlTask` performs the actual reset/ACK operation.

This keeps MQTT transport separate from safety/control logic.
