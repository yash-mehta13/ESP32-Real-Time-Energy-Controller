# Safety and Limitations

## 1. What this project is

An educational embedded-systems project: ESP32 firmware that measures with a
PZEM-004T v3.0 (or a simulation), applies a configurable power policy and
commands a relay. It is **not** certified, not automotive-grade, not a
substitute for a breaker/fuse/RCD, and not qualified for production use.
No electrical-safety certification, calibrated energy-meter accuracy or
guaranteed fault interruption is claimed.

## 2. Mains electricity

The PZEM-004T connects to AC mains. A relay may switch mains loads. Mains
voltage can cause fatal shock and fire.

- Do the electrical work only if you are qualified, or have it done and
  checked by someone who is.
- Use a suitable insulated enclosure, strain relief, correct conductor sizes,
  correctly rated fuse/breaker (and RCD where applicable), and verified relay
  contact ratings for the actual load type.
- Never test an untested build on an unattended or hazardous load.
- Keep the low-voltage side (ESP32, USB) isolated from mains parts; do not
  touch the board while it is connected to mains.
- This repository gives no live-mains wiring instructions on purpose.

## 3. Low-risk development workflow

1. Work with `esp32dev_sim` (default). Simulated values cannot close the
   relay pin and no mains is needed.
2. Run `make test`.
3. Verify the relay driver's polarity and the safe state with the ESP32
   alone and no load (docs/hardware-and-wiring.md section 4).
4. Test the PZEM with a safe, low-energy, fused arrangement and a
   non-hazardous load, supervised.
5. Only then consider a real load, with independent protection in place.

## 3a. Build-mode summary

| Build | Source | Relay pin | Intended use |
|---|---|---|---|
| `esp32dev_sim` | simulation | held OFF always | development, demos, CI |
| `esp32dev_pzem` | PZEM | follows protection | supervised hardware tests |

## 4. Limits of firmware-only protection

- Thresholds (485 W / 550 W / 2 s) are project settings, not safety ratings.
  The 2 s confirmation means a load above 550 W is allowed to run for at
  least 2 s by design; the relay stays ON during confirmation.
- Detection granularity is the 1 s sampling period plus the PZEM's own
  update rate. Short transients can be missed. No response-time figure has
  been measured or is claimed.
- No relay feedback: the firmware knows what it commanded, not that the
  contacts opened. A welded contact is not detected.
- If the ESP32 hangs, resets, browns out or loses power, the relay's state is
  defined by hardware (pull resistor, relay type). The watchdog resets the
  ESP32; it does not guarantee an interrupt.
- Fault state is RAM only and lost on reset.
- If the PZEM is downstream of the relay, an open relay hides the overload
  from the reset interlock.
- Only overpower, invalid/stale data and internal failure are handled. There
  is no overcurrent, short-circuit, ground-fault, over/under-voltage,
  temperature or frequency protection.
- Simulation-mode LCD/telemetry show simulated values; do not use them for
  decisions.

## 5. Evidence status

Hardware validation described in the README is **author-reported**. No
committed measurement records exist. The automated tests cover decision logic
on a host only. See docs/test-procedure.md section 7 for the record needed to
turn author-reported results into reproducible evidence.
