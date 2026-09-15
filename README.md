# MachineGuard

![Version](https://img.shields.io/badge/version-v1.0-blue?style=for-the-badge)
![Status](https://img.shields.io/badge/status-Active%20Development-orange?style=for-the-badge)
![Target](https://img.shields.io/badge/FETIN-2026-red?style=for-the-badge)
![Platform](https://img.shields.io/badge/MCU-ESP32--S3-success?style=for-the-badge)
[![CI](https://img.shields.io/github/actions/workflow/status/freitasj1/MachineGuard/ci.yml?style=for-the-badge&label=CI)](https://github.com/freitasj1/MachineGuard/actions/workflows/ci.yml)

### Embedded Predictive Maintenance System for Rotating Machinery

**Embedded DSP · Edge Computing · Condition Monitoring · ESP32-S3**

<!--
Add project/prototype hero image here.
Suggested image: final MachineGuard prototype mounted on the motor.
-->

![MachineGuard prototype](docs/imgs/README/prototype.jpg)

---

## Overview

MachineGuard is an embedded condition monitoring system designed to detect changes in the operating condition of rotating machinery through vibration analysis.

The system performs signal acquisition, digital signal processing, feature extraction and condition assessment locally on an **ESP32-S3**, combining embedded software, real-time processing and statistical analysis in a single edge device.

The project was developed as an engineering and research project and prepared as a prototype for **FETIN 2026**.

---

## The Problem

Rotating machines can develop mechanical abnormalities that alter their vibration behavior before more visible symptoms appear.

Vibration-based condition monitoring provides a way to observe these changes continuously. However, many monitoring solutions rely on specialized equipment, external processing or complex infrastructure.

MachineGuard explores a different approach:

> **Can a low-cost embedded platform perform the complete vibration analysis and condition assessment locally?**

The project focuses on the embedded implementation of this concept, from sensor acquisition to the final machine state.

---

## The Solution

MachineGuard uses an accelerometer mounted directly on the machine to acquire vibration data.

The acquired signal is processed in real time by the ESP32-S3. Instead of sending raw vibration data to an external computer, the firmware extracts relevant features locally and compares them against a statistical representation of healthy operation.

The resulting system combines:

* vibration acquisition;
* digital signal processing;
* spectral analysis;
* rotational-speed estimation;
* statistical baseline modeling;
* anomaly detection;
* local machine-state assessment;
* temperature monitoring;
* local visualization;
* optional IoT telemetry.

The core decision remains local to the embedded system.

---

## How It Works

The main processing concept can be summarized as:

```text
                 Vibration
                     │
                     ▼
             Signal Acquisition
                     │
                     ▼
                DSP Pipeline
                     │
          ┌──────────┴──────────┐
          │                     │
          ▼                     ▼
   Time-domain Features      FFT Analysis
          │                     │
          │                1×RPM / RPM
          └──────────┬──────────┘
                     │
                     ▼
             Feature Extraction
                     │
                     ▼
             Healthy Baseline
                     │
                     ▼
                Z-score
                     │
                     ▼
                2/3 Voting
                     │
                     ▼
            Temporal Persistence
                     │
                     ▼
              Machine State
```

The system currently uses three main features for condition assessment:

* **RMS**
* **Kurtosis**
* **1×RPM amplitude**

The baseline is constructed from healthy machine operation.

For each new evaluation, the extracted features are compared against the baseline using statistical deviation. A feature is considered abnormal when it exceeds both the configured statistical threshold and the baseline-relative margin.

The final evaluation uses a **2-out-of-3 decision**, followed by temporal persistence to avoid reacting to isolated variations.

---

## Machine States

The embedded system represents the machine condition through a small state model:

```text
             ┌─────────┐
             │  INIT   │
             └────┬────┘
                  │
                  ▼
             ┌─────────┐
             │ WARMUP  │
             └────┬────┘
                  │
                  ▼
             ┌─────────┐
        ┌────►│ HEALTHY│◄────┐
        │     └────┬────┘     │
        │          │          │
        │          ▼          │
        │     ┌─────────┐     │
        └─────│  ALARM  │─────┘
              └─────────┘

        No valid vibration
                │
                ▼
          ┌──────────┐
          │ NO_MOTOR │
          └──────────┘
```

`NO_MOTOR` is used when valid vibration is absent for a sustained sequence of evaluations. When vibration returns, the system resumes monitoring using the existing baseline when available.

The state machine is handled locally by the embedded firmware.

---

## System Architecture

<!--
Add high-level system architecture diagram here.
Suggested diagram:
Accelerometer → DSP → System → HMI / DAC / Telemetry
Sensors → System
Telemetry → MQTT → ThingsBoard
-->

![System architecture](docs/diagrams/system_architecture.png)

The system is organized around four main concepts:

```text
Hardware
    │
    ▼
Signal Acquisition
    │
    ▼
Embedded Processing
    │
    ▼
Condition Assessment
    │
    ├── Local HMI
    ├── Analog Output
    └── Telemetry
```

The architecture separates signal processing, condition assessment and peripheral interfaces.

The ESP32-S3 performs the computational core locally, while external interfaces are treated as consumers of the processed information.

---

## Embedded Processing

The vibration processing pipeline operates on blocks of **2048 samples** acquired from the LSM6DS3TR-C accelerometer.

The current configuration uses an approximate sampling rate of:

```text
6.66 kHz
```

The DSP pipeline includes:

* DC offset removal;
* time-domain statistics;
* Hann window;
* real FFT;
* magnitude calculation;
* spectral peak detection;
* parabolic peak interpolation;
* 1×RPM amplitude extraction;
* RPM estimation.

### Time-domain analysis

The firmware calculates:

```text
RMS
Standard Deviation
Minimum
Maximum
Peak-to-Peak
Crest Factor
Kurtosis
```

### Frequency-domain analysis

The FFT is used to identify the dominant rotational component.

The estimated rotational speed is obtained from:

```text
RPM = f_peak × 60
```

where `f_peak` represents the frequency associated with the machine's 1×RPM component.

---

## Condition Monitoring

The condition monitoring layer operates independently from the DSP implementation.

The DSP produces signal features.

The system layer evaluates those features.

This separation allows the signal-processing pipeline to remain focused on measurement while the system layer handles the machine-state logic.

### Healthy baseline

The baseline represents the statistical behavior of the machine during healthy operation.

The current baseline uses:

```text
RMS
Kurtosis
1×RPM amplitude
```

The statistics are calculated incrementally, avoiding the need to store the complete set of observations.

### Statistical detection

For each feature:

```text
Z = (x - μ) / σ
```

The current detector combines:

```text
Z-score threshold
        +
10% baseline-relative margin
        +
2/3 feature voting
        +
5 consecutive evaluations
```

This produces the final `HEALTHY` or `ALARM` state.

---

## Technologies

### Embedded

* **C**
* **ESP-IDF**
* **FreeRTOS**
* **ESP32-S3**
* FreeRTOS Tasks
* FreeRTOS Queues
* Mutex-based resource synchronization

### Digital Signal Processing

* **ESP-DSP**
* FFT
* Hann window
* RMS
* Kurtosis
* Crest Factor
* Spectral peak detection
* Frequency interpolation
* Statistical baseline
* Z-score analysis

### Hardware

* **ESP32-S3 N16R8**
* **LSM6DS3TR-C** triaxial accelerometer
* **DS18B20** temperature sensor
* TFT display
* **MCP4725** DAC
* Magnetic sensor mounting

### Communication

* SPI
* I²C
* Wi-Fi
* MQTT
* TLS

### IoT

* ThingsBoard
* MQTT telemetry

### Development & Engineering

- **Git**
- **GitHub**
- **GitHub Actions**
- **Continuous Integration (CI)**
- Feature-based Git workflow
- Pull Requests
- Code review
- Issue tracking
- Kanban
- Technical documentation
- Doxygen
---

## Hardware

<!--
Add final hardware photo here.
Suggested image:
MachineGuard board/device mounted on the motor.
-->

![MachineGuard hardware](docs/imgs/README/hardware.jpg)

The prototype is built around the ESP32-S3 and a triaxial accelerometer magnetically mounted to the motor housing.

Additional peripherals provide local visualization, temperature measurement and analog signal output.

| Component      | Role                            |
| -------------- | ------------------------------- |
| ESP32-S3 N16R8 | Embedded processing             |
| LSM6DS3TR-C    | Vibration acquisition           |
| DS18B20        | Temperature measurement         |
| TFT Display    | Local visualization             |
| MCP4725        | Analog output                   |
| Motor          | Experimental rotating machinery |

---

## Local Interface

<!--
Add HMI screenshots here.
Suggested images:
1. Status screen
2. FFT screen
3. Diagnostic screen
-->

![Status screen](docs/imgs/README/status.jpg)

![FFT screen](docs/imgs/README/fft.jpg)

![Diagnostic screen](docs/imgs/README/diagnostic.jpg)

The local interface provides direct access to the machine condition and signal information.

The main views include:

* machine status;
* vibration indicators;
* temperature;
* RPM;
* frequency;
* FFT visualization;
* diagnostic information for the features used by the detector.

The interface allows the embedded processing to be observed without requiring an external computer.

---

## Experimental Results

MachineGuard has been validated progressively through experimental tests covering different stages of the system.

The tests include:

* time-domain vibration acquisition;
* signal statistics;
* frequency-domain analysis;
* RPM estimation;
* healthy baseline construction;
* statistical condition monitoring;
* machine-state transitions.

The experimental records are maintained separately in the repository:

* [DSP #001 — Time-domain acquisition and statistics](https://github.com/freitasj1/MachineGuard/blob/main/docs/Tests/DSP_001.md?utm_source=chatgpt.com)
* [DSP #002 — Spectral analysis and RPM estimation](https://github.com/freitasj1/MachineGuard/blob/main/docs/Tests/DSP_002.md?utm_source=chatgpt.com)
* [System #001 — Baseline and condition detection](https://github.com/freitasj1/MachineGuard/blob/main/docs/Tests/SYSTEM_001.md?utm_source=chatgpt.com)
* [Complete test collection](https://github.com/freitasj1/MachineGuard/tree/main/docs/Tests?utm_source=chatgpt.com)

These documents contain the experimental configurations, measurements and observations obtained during development.

---

## Project Context

MachineGuard was developed as an engineering and research project focused on embedded systems, digital signal processing and machine condition monitoring.

The project brings together:

```text
Embedded Systems
       +
Digital Signal Processing
       +
Real-Time Software
       +
Statistical Analysis
       +
IoT Connectivity
```

The prototype was developed for presentation at **FETIN 2026**.

The repository also serves as a technical portfolio demonstrating the implementation of embedded C, real-time software architecture, signal processing and hardware/software integration.

---

## Team

| Member                         | Contribution                                                                                 |
| ------------------------------ | -------------------------------------------------------------------------------------------- |
| **João Pedro Maciel Freitas**  | Embedded firmware, DSP, system architecture, hardware/software integration and documentation |
| **João Pedro Siqueira Job**    | Mechanical design, prototype manufacturing and hardware                                      |
| **Núbia Ariela Rezende Costa** | Documentation, organization and project management                                           |

### Academic Advisor

Prof. M.Sc. Daniel Albino Mosca Rodrigues

---

## License

This project is licensed under the **PolyForm Noncommercial License 1.0.0**.

See the [LICENSE file](https://github.com/freitasj1/MachineGuard/blob/main/LICENSE?utm_source=chatgpt.com) for the complete terms.

---

<p align="center">
  <sub>MachineGuard · Embedded Predictive Maintenance System for Rotating Machinery</sub>
</p>
