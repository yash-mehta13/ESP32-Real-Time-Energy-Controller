## Summary

<!-- What and why. -->

## Checks

- [ ] `make test` result (paste the summary line):
- [ ] `pio run -e esp32dev_sim` result:
- [ ] `pio run -e esp32dev_pzem` result:
- [ ] `python3 scripts/scan_secrets.py` / `check_docs.py` pass

## Safety review

- [ ] Fault-state behaviour unchanged, or changed with new/updated tests
- [ ] Relay safety: only `actuator.cpp` writes GPIO; relay OFF on every failure path
- [ ] Simulation isolation: simulation builds cannot drive the relay pin
- [ ] Reset/ack semantics unchanged, or documented
- [ ] Hardware or wiring changes (pins, polarity, parts):
- [ ] Network/MQTT changes (topics, schema, ACL impact):
- [ ] Security implications (secrets, TLS, commands, telemetry):
- [ ] Documentation and CHANGELOG updated

## Evidence

<!-- For any physical-test claim, link the record in docs/validation/.
     State clearly what was only simulated or host-tested. Do not include
     credentials. -->
