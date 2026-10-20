# Hardware and Wiring

> **Mains warning.** The PZEM-004T measures AC mains and the relay may switch
> mains loads. Mains voltage can kill. This document does not give step-by-step
> mains wiring instructions. Use qualified electrical work, suitable
> enclosures, isolation, correctly rated components and fusing, and
> supervised testing. Read docs/safety-and-limitations.md first.

## 1. Pin mapping (defaults in `src/config.example.h`)

| Function | ESP32 GPIO | Notes |
|---|---:|---|
| UART2 RX (ESP32 receives) | 16 | connect to the PZEM **TX** pin |
| UART2 TX (ESP32 transmits) | 17 | connect to the PZEM **RX** pin |
| Normal LED | 18 | HIGH = on |
| Alert LED | 19 | HIGH = on |
| Relay control | 23 | active-**low** by default (`RELAY_ON_LEVEL = LOW`) |
| I2C LCD 16x2 | default SDA/SCL (GPIO21/22 on a standard esp32dev) | address `0x27` |
| Serial monitor | USB | 115200 baud |
| PZEM UART | - | 9600 baud 8N1 |

The README previously labelled GPIO16/17 as "PZEM RX/TX"; they are the ESP32's
UART pins and must be crossed to the PZEM's TX/RX.

## 2. Relay driver assumptions

- Polarity is a configuration (`RELAY_ON_LEVEL`/`RELAY_OFF_LEVEL`), applied in
  one place (`relayLevel()` + `actuator.cpp`). It is **not verified for your
  module by this repository**. Many relay boards are active-low, some are
  active-high, and some invert through an opto stage; test yours with **no
  load connected** before enabling real output.
- Check the module's coil/driver supply, the logic level it needs from a 3.3 V
  GPIO, and the contact **rating for your load type** (resistive vs. inductive,
  AC vs. DC, derating). The firmware assumes nothing about the contact rating.
- There is **no relay feedback input**. The firmware reports the *commanded*
  state; it cannot confirm that the contacts opened or are welded.

## 3. Safe state when the ESP32 is not driving the pin

The ESP32 releases GPIOs to a high-impedance state during reset and while
unpowered, and at boot before `actuatorBegin()` runs. In that time the relay
driver input is defined only by your hardware. To guarantee OFF:

- add a resistor that holds the driver input at the **OFF level** (pull-up
  for an active-low module, pull-down for active-high) if the module does not
  already do so (verify; do not assume);
- choose a relay arrangement whose de-energised state is the safe state of
  the load (normally-open contact in the supply path for "off = safe");
- keep protection independent of the ESP32: use an appropriately rated fuse
  or breaker (and RCD/GFCI where required). Firmware limits are not a
  substitute.

GPIO23 is not a strapping pin on a standard ESP32, but check your board for
strapping-pin conflicts if you move the relay output (GPIO0/2/5/12/15).

## 4. Pre-power verification checklist (no mains connected)

1. Flash `esp32dev_sim`; confirm `status` shows `Relay GPIO output: DISABLED`.
2. With a multimeter on the relay control pin, run through the whole
   simulation profile: the pin must stay at the OFF level.
3. Disconnect the ESP32 USB power, and confirm the relay stays off.
4. Verify polarity of your relay module with the ESP32 only (no load, or an
   indicator lamp on a low-voltage supply): drive GPIO23 manually.
5. Verify PZEM UART wiring and logic levels per the PZEM documentation.
6. Only then flash `esp32dev_pzem`.

## 5. Power considerations

- Power the ESP32, relay coil and PZEM interface from supplies suited to
  their current; a relay coil switching can brown out a weak 3.3 V/5 V rail
  and reset the ESP32 (a reboot leaves the load state to the hardware).
- The PZEM-004T v3.0 interface is isolated by the module's design; whether
  the isolation of **your** module and build meets your requirements is not
  established by this project.

## 6. Photographs

No hardware photographs are committed. Add real photos under
`docs/assets/photos/` with an accurate caption and the date when available;
conceptual renders (`docs/assets/lcd-mockup.svg`) are labelled as such.
