# Security Policy

## Reporting

Please report vulnerabilities privately (GitHub private vulnerability
reporting / a security advisory on this repository) rather than in a public
issue. Include the firmware version, build environment and reproduction
steps. **Never include real credentials, tokens or private keys.**

## Credential exposure

If you find a credential in the code or history, report the *category*
(Wi-Fi, MQTT, Blynk) and file/commit, not the value. Credentials must be
rotated at the provider; deleting them from the latest commit is not enough.

## Scope and risks

- **Remote commands:** anyone with publish rights on `energy/<id>/cmd/#` can
  request `reset`/`ack`. The device still applies its local interlock, but
  restrict broker ACLs and do not retain command topics
  (docs/mqtt-protocol.md).
- **Safety-sensitive limits:** this firmware is an educational prototype, not
  a certified protection device. It must not be the only protection for a
  mains-connected load (docs/safety-and-limitations.md). Reports about
  physical safety weaknesses are welcome.
- No secure boot, flash encryption or message signing is implemented.
- A certificate-validation bypass (e.g. `setInsecure()`) is never acceptable.
