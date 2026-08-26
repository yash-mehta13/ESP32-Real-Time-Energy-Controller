# ESP32 Smart Energy Monitor

Real-time, non-invasive AC energy monitoring system built on the ESP32, using FreeRTOS to run sensor acquisition and cloud communication as separate concurrent tasks.

> **Before pushing:** create an `assets/` folder in the repo root and move your existing images into it with these names (rename as you move):
> - `Circuit Design1.jpeg` → `assets/circuit-diagram.png` (or keep .jpeg, just rename)
> - `Circuit Design 2.jpeg` → `assets/hardware.jpg`
> - `LCD display.jpeg` → `assets/lcd-display.jpg`
> - `BlynkIoT output.jpeg` → `assets/blynk-dashboard.png`
> - Keep whichever `Screenshot ....png` files are actually useful (e.g. Blynk web dashboard) and rename them descriptively; delete the rest — a repo root full of `Screenshot 2026-04-06 144714.png` files looks unmanaged.
> Then the `![...]()` links below will render correctly.

---

## Overview

A low-cost, real-time IoT energy monitoring system that tracks voltage, current, active power, and cumulative energy consumption on a single AC load. The ESP32 handles sensor polling and cloud communication concurrently using FreeRTOS, pushing live data to a Blynk dashboard and triggering local + push-notification alerts on excessive usage.

## Features

- **Dual-core task split (FreeRTOS):** sensor polling and cloud/Wi-Fi communication run as independent tasks on separate cores, so a slow network call never blocks a measurement cycle.
- **Non-invasive AC sensing:** PZEM-004T v3.0 module with a CT coil — no direct contact with mains conductors required for current sensing.
- **Local display:** live voltage/current/power on a 16x2 I2C LCD.
- **Threshold alerts:** LED indication + Blynk push notification when consumption exceeds a configurable daily/monthly threshold.

## System Architecture

```
                AC LOAD
                   │
                   ▼
             PZEM-004T v3.0
           (voltage, current,
            power via CT coil)
                   │ UART (RX2/TX2)
                   ▼
                 ESP32
                   │
        ┌──────────┴──────────┐
        │     FreeRTOS         │
        │                       │
   Core 1: Measurement    Core 0: Wi-Fi / Blynk
   - Poll PZEM over UART   - Maintain Wi-Fi connection
   - Update LCD            - Push V/I/P/E to Blynk
   - Threshold check        - Send alert notification
        │                       │
        ▼                       ▼
   LED indicator          Blynk Cloud Dashboard
   (local alert)          (remote monitoring)
```

## Hardware

| Component | Role |
|---|---|
| ESP32 Dev Module | Main controller (Wi-Fi + dual-core) |
| PZEM-004T v3.0 | AC voltage/current/power sensing (UART) |
| CT coil (PZEM-004T) | Non-invasive current sensing on the live wire |
| 16x2 I2C LCD | Local readout |
| Green/Red LEDs | Normal / alert indication |
| 5V supply (USB or Hi-Link AC-DC) | Power for ESP32 + peripherals |

**Hardware photo:**
![Hardware setup](assets/hardware.jpg)

## Circuit / Wiring

**PZEM-004T → ESP32:**

| PZEM Pin | ESP32 Pin |
|---|---|
| VCC | 5V (VIN) |
| GND | GND |
| RX | GPIO 17 (TX2) |
| TX | GPIO 16 (RX2) |

![Circuit diagram](assets/circuit-diagram.png)

**⚠️ HIGH VOLTAGE WARNING**
This project interfaces with 220V AC mains. Only the Live (Phase) wire of the monitored load passes through the CT coil; the PZEM's AC voltage screw terminals connect directly to Live and Neutral. Do not touch any part of the circuit while it is connected to mains. Double-check all wiring before powering on. If you're not confident working with mains voltage, don't build this — use a mains-isolated dev setup or consult someone qualified.

## Firmware Architecture

Two FreeRTOS tasks run independently:

- **`measurementTask` (Core 1):** polls the PZEM-004T over UART on a fixed interval, updates the LCD, checks the reading against the configured threshold, and drives the LED.
- **`networkTask` (Core 0):** maintains the Wi-Fi connection, pushes V/I/P/E readings to Blynk virtual pins, and sends a push notification (via Blynk → WhatsApp integration) when a threshold is crossed.

Tasks communicate via a shared, mutex-protected struct holding the latest sensor readings, so the network task always sends the most recent value without blocking the measurement task.

## FreeRTOS Implementation

```cpp
// Illustrative — replace with your actual task signatures from main.ino
xTaskCreatePinnedToCore(measurementTask, "Measurement", 4096, NULL, 1, NULL, 1); // Core 1
xTaskCreatePinnedToCore(networkTask,     "Network",     4096, NULL, 1, NULL, 0); // Core 0
```

This split exists because Blynk's Wi-Fi calls are blocking and can stall for hundreds of milliseconds on a weak connection — pinning them to Core 0 keeps sensor polling on Core 1 running on schedule regardless of network conditions.

## Data Flow

1. PZEM-004T measures voltage, current, and active power on the CT-sensed line.
2. ESP32 polls PZEM over UART on a fixed cycle.
3. `measurementTask` updates the LCD and evaluates the alert threshold.
4. `networkTask` pushes the latest readings to Blynk virtual pins (V0–V3).
5. On threshold breach: LED turns red, Blynk sends a push/WhatsApp notification.

## Dashboard

![Blynk dashboard](assets/blynk-dashboard.png)

## Results

*(Add real captured data here — this section is currently empty in the write-up and should not be, since it's the easiest way to prove the system actually works. Include, e.g.: a table of measured vs. a reference meter's readings for a known load, a short accuracy note, and/or a screenshot of the LCD mid-measurement.)*

## Safety Considerations

- All mains-side connections are made through the PZEM-004T's isolated terminals; the ESP32 and logic-side wiring never contact mains directly.
- Only the Live conductor passes through the CT coil — never both Live and Neutral together.
- Circuit is built and tested with the load de-energized; power is applied only after all connections are verified.

## Limitations

- Wi-Fi credentials and Blynk auth token are currently hardcoded in the source and must be edited before flashing — no runtime configuration (e.g. via captive portal).
- No persistent local storage (SD card / NVS) — historical data lives only in Blynk's retention window.
- Single-load monitoring only; no support for monitoring multiple circuits from one unit.
- No calibration routine — accuracy depends entirely on the PZEM-004T's factory calibration.

## Future Improvements

- Move Wi-Fi/Blynk credentials to a captive-portal setup (e.g. WiFiManager) instead of hardcoding.
- Add local logging (SD card or ESP32 NVS) so history survives a Blynk outage or account change.
- Add OTA firmware updates.
- Support multiple PZEM-004T units for multi-circuit monitoring.
- Add a calibration/verification step against a known-good reference meter, with results published in this README.
