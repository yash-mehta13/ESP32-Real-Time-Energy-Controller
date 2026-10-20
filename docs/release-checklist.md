# Release Checklist

Complete before tagging. Record the actual command output; do not tick a box
without running the step.

- [ ] Clean clone builds: `cp src/config.example.h src/config.h` then
      `pio run -e esp32dev_sim` and `pio run -e esp32dev_pzem` succeed
- [ ] `make test` reports 0 failures
- [ ] CI is green on the release commit; `pio pkg list` output saved and the
      platform/library versions pinned in `platformio.ini`
- [ ] `python3 scripts/scan_secrets.py` and `python3 scripts/check_docs.py` pass
- [ ] `git check-ignore -v src/config.h` confirms `config.h` is ignored; no
      credentials in history (`git log -p` review) - rotate if any
- [ ] Default environment is `esp32dev_sim`; the boot banner says
      `Relay GPIO output: DISABLED (simulation)`; relay pin held OFF verified
- [ ] Source mode, relay mode and version visible in `version`/`status`
- [ ] Relay polarity re-verified for the intended module
- [ ] CHANGELOG, README status table, requirements-traceability and docs agree
      with the code (thresholds, topics, schema version)
- [ ] Known issues listed in docs/roadmap.md are still accurate
- [ ] Hardware evidence: any physical-test claim has a record under
      `docs/validation/`; otherwise it is labelled author-reported
- [ ] License status decided (no license file exists today)
- [ ] CA certificate still valid and matches the broker chain
- [ ] Photos/diagrams: conceptual renders labelled as such
