# ESP32 MOSFET Parameter Characterization Platform

> **Version:** 10.0.0  
> **Target Hardware:** ESP32 Wroom 32D  
> **Interface:** Web-based (zero-installation, responsive)  
> **Project Scope:** Scientific research platform (PIBIC, EESC-USP)  

---

## 1. Project Overview

This platform is a low-cost, self-contained instrument based on the ESP32 microcontroller designed to perform electrical characterization of low-power N-channel MOSFETs (e.g., BS170, 2N7000, and CD4007). By implementing closed-loop analog control and high-resolution measurement interfaces, it extracts key electrical parameters such as:
* **Threshold Voltage ($V_{th}$)**
* **Maximum Transconductance ($G_m$)**
* **Subthreshold Swing ($SS$)**

All of this is achieved without installing any software on the client computer. The user interface is a web page hosted directly on the ESP32, accessible via a browser on the local network.

> [!NOTE]
> **Cost-to-Benefit Paradox:**  
> A commercial Semiconductor Parameter Analyzer (Source Measure Unit - SMU) like the Keysight B2902C costs approximately **$15,000 USD** (R$ 85.000,00 in 2026). This platform performs the basic parameter extraction scope for less than **$40 USD** (R$ 200,00) — a cost reduction of over **99.7%**, while achieving metrological behavior suitable for academic and educational labs.

---

## 2. Operation Modes

The system operates in three distinct characterization modes, selectable via the Web UI:

| Mode | Sweep Range | Fixed Bias | Active Shunt | Parameters Extracted |
| :--- | :--- | :--- | :--- | :--- |
| **1 — Subthreshold** | $V_{GS}$: $0 \to 3.5\text{ V}$ | $V_{DS}$ fixed ($0.1\text{ to }0.5\text{ V}$) | $100\ \Omega$ | Subthreshold Swing ($SS$), $V_{th}$ |
| **2 — Saturation** | $V_{GS}$: $0 \to 5.0\text{ V}$ | $V_{DS}$ fixed ($0.1\text{ to }5.0\text{ V}$) | $1\ \Omega$ | $V_{th}$, Max $G_m$ |
| **3 — Output Curves** | $V_{DS}$: $0 \to 5.0\text{ V}$ | $V_{GS}$ fixed (family of steps) | $1\ \Omega$ | $I_{ds} \times V_{ds}$ curves, Triode/Saturation regions |

---

## 3. Repository Structure

```
├── docs/                        # Project documentation in English
│   ├── index.md                 # Entry point / Overview
│   ├── hardware.md              # Hardware topology and schematic details
│   ├── firmware.md              # Firmware architecture, tasks and RTOS
│   ├── webui.md                 # Frontend, Web Server and API endpoints
│   └── calibration.md           # Sensor calibration, tare and auto-range logic
├── src/
│   ├── main.cpp                 # Entry point, WebUI routing & REST handlers
│   ├── mosfet_controller.cpp    # Sweep state machine and measurement loop
│   ├── hardware_hal.cpp         # Hardware Abstraction Layer (DACs/ADCs)
│   └── ...
├── include/
│   ├── hardware_hal.h           # HAL constants and ADC conversion formulas
│   ├── mosfet_controller.h      # Sweep configurations and global limits
│   └── version.h                # Version definition (MAJOR.MINOR.SNAPSHOT)
├── scripts/
│   └── embed_web.py             # Pre-build hook: Compiles HTML/CSS/JS -> PROGMEM arrays
├── web/                         # Web UI source files (HTML/CSS/JS modules)
├── images/                      # Schematic diagrams and PCB layouts (2D/3D)
├── platformio.ini               # PlatformIO environment configurations
└── README.md                    # Main Portuguese README
```

---

## 4. Documentation Index

To explore the details of this platform, navigate through the specialized documentation sections:

* 🔌 **[Hardware Architecture](/home/luska/Documents/projects/esp32_mosfet_analysis/docs/hardware.md)**: Details the active topology, including the ESP32, dual MCP4725 DACs, ADS1115 ADC, LT1013 precision operational amplifier, and the active bleeder circuit.
* 🧠 **[Firmware & Algorithms](/home/luska/Documents/projects/esp32_mosfet_analysis/docs/firmware.md)**: Explains the closed-loop control system, dual-core task sharing, I2C resource locking (mutex), oversampling + trimmed mean math, and the Savitzky-Golay filtering.
* 🌐 **[Web Interface & API](/home/luska/Documents/projects/esp32_mosfet_analysis/docs/webui.md)**: Describes the design guidelines of the zero-dependency browser dashboard, Javascript modular structure, REST API specifications, and mDNS setup.
* 🛠️ **[Calibration & Diagnostics](/home/luska/Documents/projects/esp32_mosfet_analysis/docs/calibration.md)**: Contains the 2-point calibration equations, ADC input protection leakage compensation, auto-zeroing routine, and troubleshooting logs for hardware crosstalk.
