# Security and Privacy

## 1. Secrets

- `src/config.h` holds Wi-Fi, Blynk and MQTT credentials. It is in `.gitignore`
  and must never be committed. `src/config.example.h` contains placeholders
  only.
- `python3 scripts/scan_secrets.py` flags non-placeholder credential
  assignments and key blocks in tracked/unignored files; CI runs it.
- Git history was inspected during the audit: `src/config.h` was committed once
  (`7df609f`) and contained placeholders only. No credentials were found.
  Nothing was rewritten.
- The firmware does not print Wi-Fi/MQTT/Blynk credentials. `BLYNK_PRINT`
  is enabled to `Serial` for development, which prints Blynk connection
  messages; disable it for deployed units if serial output is exposed.
- If a credential is ever exposed: rotate it at the provider (Wi-Fi, HiveMQ
  user, Blynk token). Deleting it from the latest commit does not help.

## 2. TLS

- CA pinned: ISRG Root X1, `setCACert()`. No `setInsecure()` and no
  validation bypass; do not add one.
- NTP must be synchronised before connecting (certificate validity checks).
- The CA expires in 2035; the broker chain can change. See mqtt-protocol.md.

## 3. Broker access control

TLS authenticates the broker to the device, not the commander to the device.
Use per-device accounts with narrow ACLs (see mqtt-protocol.md), no `#`
grants, and no retained command topics. Anyone able to publish to
`cmd/reset` can request a reset; the local interlock still applies.

## 4. Command validation

Exact topic, exact payload bytes, <= 16 bytes, bounded queue, hold-off after
subscribe. No dynamic allocation in the callback. No command can set a
threshold, change relay polarity or force the relay.

## 5. Telemetry exposure

Telemetry contains the device ID, firmware version, electrical readings and
state. Electrical usage can reveal occupancy patterns; treat the broker and
Blynk project as holding personal data and restrict access accordingly. No
Wi-Fi SSID, IP or credentials are published. Uptime is published as `uptime_ms`.

## 6. Serial CLI

The CLI is unauthenticated and local: anyone with USB access can run `reset`
and `ack` (still subject to the interlock). Physical access is assumed
trusted.

## 7. Limitations

- No message signing, replay protection or secure boot / flash encryption.
- Fault state is not persisted.
- Dependencies (Blynk, PubSubClient, PZEM, LiquidCrystal_I2C) are used as-is
  with caret version ranges; they have not been independently reviewed.
- `BLYNK_AUTH_TOKEN` is a bearer secret held in firmware flash; anyone who
  can read the flash can recover it.
