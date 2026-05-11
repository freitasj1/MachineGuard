<div align="center">

# MachineGuard
### Embedded Predictive Maintenance System for Rotating Machinery

Embedded DSP · Anomaly Detection · Edge Computing · No Infrastructure Required

[![Version](https://img.shields.io/badge/version%20v1.4-blue?style=for-the-badge)]()
[![Status](https://img.shields.io/badge/status-In%20Development-yellow?style=for-the-badge)]()
<!--[![Platform](https://img.shields.io/badge/ESP32--S3-N16R8-red?style=for-the-badge)]() -->
[![Event](https://img.shields.io/badge/FETIN-2026-orange?style=for-the-badge)]()

</div>

---

## Overview

MachineGuard is an embedded predictive maintenance system for rotating motors. It runs a full DSP pipeline — FFT, kurtosis, RMS, adaptive baseline — entirely on an ESP32-S3, with no cloud, no gateway, and no subscription required.

The system targets the gap between reactive maintenance and expensive professional solutions (SKF, TRACTIAN, Emerson AMS cost R$8k–R$40k per monitoring point). MachineGuard delivers early fault detection at approximately **R$239 in hardware**.

> Currently in active development. Target: **FETIN 2026** — INATEL Technology and Science Fair.

---

## The Problem

Vibration captures mechanical deterioration at an early stage — an imbalanced rotor generates a 1×RPM force component that shows up as an isolated peak in the FFT spectrum long before any temperature rise is measurable. Temperature-only monitoring detects problems only when damage is already severe.

Most of the industry still operates reactively or on calendar-based maintenance schedules — both wasteful — because continuous vibration monitoring systems have historically been restricted to large operations with high instrumentation budgets.

---

## Technical Approach

The LIS3DH accelerometer samples vibration at **5 kHz via SPI DMA**, writing directly to memory without CPU intervention. A DSP pipeline runs continuously on **Core 0** of the ESP32-S3 at maximum FreeRTOS priority, fully isolated from I/O operations.

### DSP Pipeline

> 📌 FreeRTOS task diagram to be added.


**Detection logic:** an alert fires only when at least 2 of 3 indicators (kurtosis, RMS, 1×RPM bin) simultaneously exceed 2.5 sigma above the learned baseline — reducing false positives without sacrificing sensitivity.

### Core Architecture
<!--
| Core | Role | Peripherals |
|---|---|---|
| **Core 0** | DSP only — max priority, no I/O ever | LIS3DH via SPI2 DMA |
| **Core 1** | All I/O | ILI9341 display, SD card, MCP4725 DAC, DS18B20, PCNT |

Inter-core communication via FreeRTOS queues only — no unprotected shared globals.

### Fault Injection

Faults are injected deterministically via a 3D-printed PLA piece coupled to the motor shaft, with two symmetric M5 bolt holes positioned ~20 mm from the geometric center. Removing one bolt (~4–5 g) generates a centrifugal force at 1×RPM — a textbook imbalance signature. The experiment is fully reversible and repeatable.

```
Balanced:    [bolt] ── center ── [bolt]   →  baseline vibration
Unbalanced:  [ — ] ── center ── [bolt]   →  growing 1xRPM peak in FFT
```

---
-->
## Hardware
<!-- Add system photo here -->
<!-- ![MachineGuard Hardware](img/hardware.jpg) -->
<!--

| Component | Model | Role |
|---|---|---|
| MCU | ESP32-S3 N16R8 | Dual-core Xtensa LX7, 16 MB Flash, 8 MB PSRAM OPI |
| Accelerometer | LIS3DH SPI breakout | 5 kHz ODR via SPI DMA — vibration acquisition |
| Display | ILI9341 2.8" TFT | 320×240 px — 4 navigable screens |
| External DAC | MCP4725 I2C | 12-bit analog output for oscilloscope visualization |
| RPM Sensor | Hall A3144 + neodymium magnet | Hardware pulse counting via ESP32-S3 PCNT |
| Temperature | DS18B20 1-Wire | Bearing temperature — validates vibration-first detection |
| Storage | MicroSD SPI | Offline CSV logging |

-->

### Hardware Diagram

<!-- Add schematic diagram here -->
<!-- ![Hardware Diagram](img/hardware_diagram.png) -->

> 📌 Schematic to be added during PCB revision (JLCPCB — month 6).

---

## Firmware Architecture

<!-- Add firmware diagram here -->
<!-- ![Firmware Architecture](img/firmware_diagram.png) -->

> 📌 FreeRTOS task diagram to be added.

---

## Display Screens
<!--
Four navigable TFT screens (320×240 px):

| Dashboard | FFT Spectrum |
|:---------:|:------------:|
| ![Dashboard](img/screen_dashboard.png) | ![FFT](img/screen_fft.png) |

| Waveform | SD History |
|:--------:|:----------:|
| ![Waveform](img/screen_waveform.png) | ![History](img/screen_history.png) |

> 📌 Screen captures to be added during hardware testing.

---

## Project Status

- [x] System architecture defined (dual-core isolation, DMA pipeline)
- [x] GPIO mapping and bus allocation documented
- [x] DSP pipeline designed (FFT, kurtosis, RMS, EMA Z-score)
- [x] Hardware BOM finalized (≈ R$239 Tier 1+2)
- [ ] DSP pipeline validated with synthetic signal
- [ ] Physical prototype assembled
- [ ] EMA alpha empirical calibration
- [ ] PCB manufactured (JLCPCB)
- [ ] Full validation with Equacional EA2-80-B3/4 motor
- [ ] FETIN 2026 presentation

---
-->
## Team

| Name | Role |
|---|---|
| João Pedro Maciel Freitas | Firmware, HW/FW integration, DSP, embedded systems, documentation |
| João Pedro Siqueira Job | Mechanics, hardware, 3D prototyping |
| Núbia Ariela Rezende Costa | Documentation, organization, project management, hardware support |

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
