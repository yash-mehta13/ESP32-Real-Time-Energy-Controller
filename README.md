# ⚡ ESP32 Smart Energy Monitor

Real-time energy monitoring system built on the ESP32, using FreeRTOS to track voltage, current, and power draw and stream it to a live dashboard.

![C++](https://img.shields.io/badge/C++-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![ESP32](https://img.shields.io/badge/ESP32-E7352C?style=flat-square&logo=espressif&logoColor=white)
![FreeRTOS](https://img.shields.io/badge/FreeRTOS-00979D?style=flat-square&logo=freertos&logoColor=white)
![Status](https://img.shields.io/badge/status-active-brightgreen?style=flat-square)

## Overview

This project uses a PZEM-004T energy sensor to measure voltage, current, and power in real time, with the ESP32 running FreeRTOS tasks to handle sensor polling, local display, and data transfer independently.

**v1 (current, stable):** Data pushed to the Blynk app for live remote monitoring.
**v2 (in progress):** Migrating telemetry from Blynk to MQTT (HiveMQ Cloud) for more flexible, self-hosted dashboards — Phase 1 (Wi-Fi, TLS handshake, MQTT auth, LWT status, sensor data topic) is verified end-to-end on hardware; Phase 2 adds CT-clamp based anomaly detection.

## Features

- Real-time voltage, current, and power monitoring via PZEM-004T
- FreeRTOS task/queue architecture for concurrent sensor polling and I/O
- On-device LCD display with status LEDs
- Remote monitoring via Blynk (v1) / MQTT + cloud broker (v2)

## Hardware

| Component | Purpose |
|---|---|
| ESP32 | Main microcontroller |
| PZEM-004T | Voltage/current/power sensing |
| 16x2 LCD | Local readout |
| Status LEDs | Visual alerts |

<!-- TODO: add a wiring diagram or photo of the physical setup here -->

## Software Stack

- Arduino framework (ESP32)
- FreeRTOS (tasks + queues)
- PubSubClient (MQTT) + ArduinoJson *(v2)*
- Blynk library *(v1)*

## Getting Started

```bash
git clone https://github.com/yash-mehta13/ESP32-Smart-Energy-Monitor.git
```

1. Open the project in Arduino IDE / PlatformIO
2. Install dependencies: `PubSubClient`, `ArduinoJson`, `Blynk` (for v1)
3. Add your Wi-Fi credentials and Blynk/MQTT broker details in `config.h`
4. Wire the PZEM-004T to the ESP32 as shown in the wiring diagram
5. Flash and monitor via Serial

## Roadmap

- [x] v1: Blynk-based real-time dashboard
- [x] v2 Phase 1: MQTT/TLS pipeline verified on hardware
- [ ] v2 Phase 2: CT-clamp calibration + anomaly detection
- [ ] Historical data logging / cloud storage

## License

<!-- TODO: pick a license, e.g. MIT — add a LICENSE file to the repo root -->
