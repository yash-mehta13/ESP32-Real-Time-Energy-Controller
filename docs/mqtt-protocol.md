# MQTT Protocol

![MQTT flow](assets/mqtt-flow.svg)

## 1. Transport

MQTT 3.1.1 via PubSubClient over `WiFiClientSecure`, port 8883, with the ISRG
Root X1 trust anchor (`src/hivemq_ca.h`) pinned via `setCACert()`. The code
never calls `setInsecure()` and must not. NTP time must be valid before the
first connection attempt, otherwise certificate validation fails (the network
task waits for it).

Keep-alive 30 s, socket timeout 5 s, buffer 1024 bytes. Reconnect interval
`MQTT_RECONNECT_INTERVAL_MS` (5 s).

## 2. Identity and topics

`MQTT_DEVICE_ID` (config.h, a string-literal macro, 1-32 chars of
`[A-Za-z0-9_-]`) is the single source. All topics derive from it
(`energy_topics.h`); an invalid ID is a compile error. Client ID:
`<device-id>-<chip-MAC-hex>`.

| Topic (device01) | Direction | Payload | Retained |
|---|---|---|---|
| `energy/device01/telemetry` | device -> broker | JSON v2 (below), every 2 s | no |
| `energy/device01/status/state` | device -> broker | `INIT` `NORMAL` `WARNING` `FAULT` | yes |
| `energy/device01/fault/code` | device -> broker | `NONE` `SENSOR_INVALID` `OVERPOWER` `SENSOR_STALE` `PROTECTION_INTERNAL` | yes |
| `energy/device01/status/availability` | device -> broker | `online` / `offline` (LWT, QoS 1) | yes |
| `energy/device01/cmd/reset` | broker -> device | `reset` | **should not be retained** |
| `energy/device01/cmd/ack` | broker -> device | `ack` | **should not be retained** |

State and fault are republished on every fresh MQTT connection and on change.

## 3. Telemetry schema (version 2)

Illustrative message. **Values are synthetic** (simulation build), not
measurements.

```json
{
  "schema_version": 2,
  "device_id": "device01",
  "firmware_version": "1.3.0",
  "source": "SIMULATION",
  "simulated": true,
  "uptime_ms": 123456,
  "valid": true,
  "measurement_status": "OK",
  "measurement_age_ms": 40,
  "voltage": 230.00,
  "current": 2.170,
  "power": 500.00,
  "energy": 0.000,
  "apparent_power": 499.10,
  "power_factor": 1.000,
  "state": "WARNING",
  "fault": "NONE",
  "fault_active": false,
  "fault_acknowledged": false,
  "last_reset_result": "NONE",
  "relay_commanded_on": true,
  "relay_output_enabled": false
}
```

| Field | Meaning |
|---|---|
| `source`, `simulated` | Measurement source actually compiled in |
| `valid`, `measurement_status` | `OK`, `SENSOR_ERROR`, `OUT_OF_RANGE`, `INCONSISTENT`, `NOT_INITIALIZED`. When invalid, numeric fields are 0 (or `null` if non-finite) and must be ignored |
| `energy` | kWh, the meter's accumulator |
| `apparent_power` | VA, derived `V x I` |
| `relay_commanded_on` | What the firmware commanded. **Not** a contact reading |
| `relay_output_enabled` | `false` in simulation builds (pin held OFF) |
| `uptime_ms` | `millis()` since boot, wraps after ~49.7 days. No wall-clock timestamp is published even though NTP is available |
| `last_reset_result` | Last reset request outcome (see fault-management.md) |

Schema changes from v1 (firmware 1.2.0): `relay_on` was replaced by
`relay_commanded_on`; added `schema_version`, `simulated`,
`measurement_status`, `measurement_age_ms`, `last_reset_result`,
`relay_output_enabled`. JSON is produced by a bounded writer that escapes
strings and emits `null` for non-finite numbers.

## 4. Command handling

The callback runs inside `mqttClient.loop()` on NetworkTask and only:

1. matches the topic exactly;
2. checks the payload as raw bytes: length <= 16 and exactly `reset` (reset
   topic) or `ack` (ack topic) - no trimming, no case folding, no embedded NUL;
3. drops commands inside a 2 s hold-off after subscribing (see below);
4. enqueues a `CommandType` with zero wait.

It never touches GPIO or protection state. ControlTask executes the request;
a reset is then judged by the interlock. Rejected, oversized, wrong-topic,
duplicate and queue-dropped commands cannot cause an unsafe action: the worst
case is that a requested reset/ack does not happen.

### Retained messages

PubSubClient does not expose the retain flag, so a retained `reset` would be
delivered immediately after SUBSCRIBE. Commands arriving within
`MQTT_COMMAND_HOLDOFF_MS` (2 s) of subscribing are dropped and logged. This is
a heuristic: a genuine command in that window is lost and must be resent.
Also configure the broker/clients so command topics are never published with
the retain flag. The reset interlock is the final protection against a stale
command.

## 5. Authentication, authorization, interlocks

| Layer | Provided by | Does not provide |
|---|---|---|
| Transport | TLS with CA verification | who may publish commands |
| Authentication | Broker username/password | per-message authenticity |
| Authorization | Broker ACLs (**you configure**) | - |
| Command validation | Firmware exact-match parser | trust in the sender |
| Safety | Local interlock (fresh valid data, P <= fault threshold) | physical isolation |

There is no message signing, nonce or replay protection. Anyone with publish
rights on `cmd/#` can *request* resets and acks; they cannot force the relay
on while the hazard persists.

## 6. Suggested broker permissions (per-device account)

Publish: `energy/device01/telemetry`, `energy/device01/status/#`,
`energy/device01/fault/#`. Subscribe: `energy/device01/cmd/#`.
Dashboard/operator accounts: subscribe `energy/device01/#`; publish only
`energy/device01/cmd/#` and only for operators allowed to request resets. No
account should have `#`. Deny the retain flag on `cmd/#` if the broker supports it.

## 7. Certificate maintenance

ISRG Root X1 is valid until 2035-06-04. If HiveMQ changes CA chain, update
`src/hivemq_ca.h` (fingerprint is recorded in the file) and re-test the TLS
connection. No certificate-validation bypass is acceptable as a workaround.
