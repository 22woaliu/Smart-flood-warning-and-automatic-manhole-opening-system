# 🌧️ Smart Roadside Water Drainage & Purification System

An IoT-based embedded system that monitors rainfall, measures road water levels in real time, automatically operates a roadside purifier/drainage mechanism, and detects possible drainage blockages — with all data streamed to a live monitoring dashboard.

> The core idea: don't wait for a flooded road to become a crisis. Sense it, react to it, and flag it the moment something looks wrong — whether that's rising water or a clogged drain.

---

## 📖 Table of Contents

- [Overview](#overview)
- [Features](#features)
- [System Architecture](#system-architecture)
- [State Machine & Escalation Logic](#state-machine--escalation-logic)
- [Hardware Components](#hardware-components)
- [Software Components](#software-components)
- [Pin Configuration](#pin-configuration)
- [Setup Instructions](#setup-instructions)
- [System Workflow](#system-workflow)
- [Water Level Detection Engine](#water-level-detection-engine)
- [Water Flow & Blockage Detection](#water-flow--blockage-detection)
- [Servo-Based Purifier Mechanism](#servo-based-purifier-mechanism)
- [Auto-Close Safety Net](#auto-close-safety-net)
- [Dashboard & Real-Time Monitoring](#dashboard--real-time-monitoring)
- [Remote Notifications](#remote-notifications)
- [Sensor Calibration Notes](#sensor-calibration-notes)
- [Example Serial Output](#example-serial-output)
- [Applications](#applications)

---

## Overview

Urban and roadside flooding is often the result of two compounding problems: heavy rainfall raising water levels faster than drains can clear it, and undetected blockages silently reducing drainage capacity until it's too late. This project builds a self-contained embedded unit that:

- Continuously senses rainfall, road water level, and drainage flow rate.
- Automatically opens a servo-actuated purifier/drain mechanism once water crosses a danger threshold.
- Closes the mechanism again once conditions return to safe levels — without unnecessary repeated motor movement.
- Cross-references water level against flow rate to infer whether a drainage section is blocked.
- Pushes all of this to a real-time dashboard so the system's decisions are visible, not just automatic.

The firmware is built for a microcontroller (ESP32/ESP8266 class) using **PlatformIO** inside **VS Code**, with connectivity to an IoT dashboard for live telemetry.

## Features

- 🌦️ **Rain Detection** — Real-time rain / no-rain status from a dedicated rain sensor.
- 📏 **Water Level Sensing** — Ultrasonic distance measurement converted to road water level, classified as Safe or Danger.
- 🚪 **Automatic Purifier Control** — Servo-driven open/close mechanism triggered purely by water level thresholds.
- 💧 **Flow-Based Blockage Detection** — Water flow sensor readings cross-checked against water level to infer drainage blockages.
- 🔁 **Debounced Actuation** — Servo only moves on an actual state change (Closed → Open or Open → Closed), preventing jitter and mechanical wear.
- 📊 **Live Dashboard** — Rain status, water level, purifier state, and flow status updated in real time.
- 🔔 **Remote Alerts** — Push notifications when the system escalates (danger level reached, blockage suspected).
- 🧩 **Modular Sensor Layer** — Each sensor is independently read and validated, so a single faulty sensor degrades gracefully instead of crashing the logic.

## System Architecture

```
                ┌─────────────────────────────┐
                │        Sensor Layer         │
                │  Rain | Ultrasonic | Flow   │
                └───────────────┬─────────────┘
                                │
                                ▼
                ┌──────────────────────────────┐
                │     Microcontroller (MCU)    │
                │  - Reads sensors             │
                │  - Runs decision logic       │
                │  - Debounces servo state     │
                │  - Publishes telemetry       │
                └───────┬─────────────┬────────┘
                        │             │
                        ▼             ▼
              ┌────────────────┐  ┌────────────────────┐
              │  Servo Motor   │  │   IoT Connectivity │
              │ (Purifier arm) │  │  (WiFi → Dashboard)│
              └────────────────┘  └────────────────────┘
```

The MCU is the single decision point: sensors feed it raw readings, it derives status flags (rain, level, flow), decides on purifier position, actuates the servo only on state change, and reports everything upstream to the dashboard.

## State Machine & Escalation Logic

The purifier mechanism behaves as a simple two-state machine with a threshold-based transition:

| Current State | Condition | Next State | Servo Action |
|---|---|---|---|
| Closed | Water level ≥ Danger Threshold | **Open** | Rotate to 180° |
| Open | Water level ≤ Safe Threshold | **Closed** | Rotate back to 0° |
| Open | Water level between Safe and Danger | **Open** (hold) | No movement |
| Closed | Water level between Safe and Danger | **Closed** (hold) | No movement |

A hysteresis gap between the Safe and Danger thresholds prevents the servo from oscillating when the water level hovers near a single cutoff point.

Blockage detection runs as an independent flag alongside this state machine:

| Water Level | Flow Reading | Inference |
|---|---|---|
| Below Danger | Normal/expected flow | Drainage functioning normally |
| At/above Danger | Normal/expected flow | Purifier open, draining as expected |
| At/above Danger | Abnormally low or zero | ⚠️ Possible blockage suspected |

## Hardware Components

| Component | Purpose |
|---|---|
| Microcontroller (ESP32) | Core processing + WiFi connectivity for IoT telemetry |
| Rain Sensor Module | Detects presence of rainfall |
| Ultrasonic Sensor (HC-SR04) | Measures distance to water surface → derives water level |
| Water Flow Sensor (YF-S201) | Measures flow rate through the drainage channel |
| Servo Motor (MG99) | Actuates the purifier/drain mechanism |
| Power Supply | Stable 5V regulated supply for sensors and servo |
| Enclosure | Weatherproof housing for roadside deployment |

## Software Components

- **Firmware** — C++ on the Arduino framework, built and flashed via PlatformIO.
- **Sensor Drivers** — Polling routines for the rain sensor (digital/analog), ultrasonic ranging, and flow-sensor pulse counting (interrupt-based).
- **Decision Engine** — Threshold comparison, hysteresis handling, and blockage inference logic.
- **Servo Controller** — State-change-gated actuation to avoid redundant movement.
- **Connectivity Layer** — WiFi client publishing sensor states and events to the IoT dashboard (e.g., via Blynk, MQTT, or a custom REST/WebSocket endpoint).
- **Dashboard Frontend** — Real-time visualization of the four core parameters (rain, water level, purifier status, flow status).

## Pin Configuration

| Signal | Component | MCU Pin (example) |
|---|---|---|
| Rain Sensor (Digital Out) | Rain Sensor | GPIO 14 |
| Ultrasonic Trigger | HC-SR04 | GPIO 5 |
| Ultrasonic Echo | HC-SR04 | GPIO 18 |
| Flow Sensor Pulse | YF-S201 | GPIO 27 (interrupt) |
| Servo Signal (PWM) | Servo Motor | GPIO 13 |
| Status LED (optional) | Onboard/External LED | GPIO 2 |

> Pin assignments are illustrative — update `config.h` (or equivalent) to match your actual wiring.

## Setup Instructions

1. **Install prerequisites**
   - [VS Code](https://code.visualstudio.com/)
   - [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)

2. **Clone the repository**
   ```bash
   git clone https://github.com/22woaliu/Smart-flood-warning-and-automatic-manhole-opening-system.git
   ```

3. **Open in PlatformIO**
   - Open the project folder in VS Code.
   - PlatformIO will auto-detect `platformio.ini` and install the required libraries.

4. **Configure credentials & thresholds**
   - Set WiFi SSID/password and dashboard/API credentials in the config file.
   - Set `SAFE_THRESHOLD` and `DANGER_THRESHOLD` (in cm) to match your ultrasonic sensor's mounting height.

5. **Build and upload**
   ```bash
   pio run --target upload
   ```

6. **Monitor serial output**
   ```bash
   pio device monitor
   ```

7. **Connect the dashboard**
   - Open the paired dashboard (app or web) and confirm live data is streaming.

## System Workflow

```
Rain Sensor ────────► Rain Status (Yes/No)

Ultrasonic Sensor ──► Water Level ──► Compare vs Thresholds
                                          │
                          ┌───────────────┴─────────────────┐
                          ▼                                 ▼
                 Level ≥ Danger                    Level ≤ Safe
                          │                                 │
                          ▼                                 ▼
                 Open Purifier (Servo → 180°)     Close Purifier (Servo → 0°)
                          │                                 │
                          └───────────────┬─────────────────┘
                                          ▼
                       Water Flow Sensor ──► Compare Flow vs Expected
                                          │
                          High Level + Low/No Flow ──► Flag Possible Blockage
                                          │
                                          ▼
                              Publish all states to Dashboard
```

## Water Level Detection Engine

The ultrasonic sensor measures the distance from its mounted position down to the water surface. Since the distance shrinks as water rises, the raw reading is inverted against a known baseline (dry-road distance) to produce an effective **water level**:

```
water_level = baseline_distance − measured_distance
```

This level is then classified:

- **Safe** — below `SAFE_THRESHOLD`
- **Warning** — between `SAFE_THRESHOLD` and `DANGER_THRESHOLD`
- **Danger** — at or above `DANGER_THRESHOLD`

Multiple consecutive readings (simple moving average) are used before triggering a state change, filtering out noise from splashing or debris.

## Water Flow & Blockage Detection

The flow sensor outputs pulses proportional to the volume of water passing through it. The firmware counts pulses over a fixed window and converts them to a flow rate (L/min).

Blockage inference logic:

- If water level is **Danger** and flow rate stays **near zero or well below expected** for a sustained window → raise a **possible blockage** flag.
- If flow resumes at normal levels → clear the flag automatically.
- The flagged event includes the sensor node/location identifier, so operators can narrow down *where* along the drainage line the blockage likely is, based on which node reported the anomaly.

This is intentionally conservative — a single abnormal reading isn't enough; the system waits for a sustained mismatch to avoid false positives from air bubbles or sensor noise.

## Servo-Based Purifier Mechanism

- The servo drives a mechanical gate/valve that forms the purifier/drain opening.
- **Closed position:** 0° — mechanism sealed, no water diverted.
- **Open position:** 180° — mechanism fully open, allowing excess water to drain/pass through the purifier.
- The firmware tracks the *last commanded state* and only issues a new servo command when the desired state actually differs from the current one — this avoids redundant PWM writes, reduces mechanical wear, and prevents flicker-like behavior if the water level hovers near a threshold.

## Auto-Close Safety Net

To avoid the mechanism staying open indefinitely due to a sensor fault or communication drop:

- If valid ultrasonic readings stop arriving for longer than a configurable timeout, the system defaults to a **safe fallback state** (configurable: fail-open or fail-closed, depending on deployment risk profile).
- A watchdog timer resets the MCU if the main loop stalls, ensuring the actuation logic keeps running.

## Dashboard & Real-Time Monitoring

The dashboard displays, at minimum:

| Parameter | Displayed Values |
|---|---|
| **Rain Status** | Rain Detected / No Rain |
| **Water Level** | Current measured level (cm) + Safe/Danger status |
| **Purifier Mechanism** | Open / Closed |
| **Water Flow** | Current flow rate (L/min) + Normal/Possible Blockage status |

All four parameters update in real time as new sensor data arrives, and any automatic purifier open/close event is visibly logged/highlighted on the dashboard the moment it happens.

## Remote Notifications

The system can push alerts to a connected notification channel (e.g., Blynk app, Telegram bot, or email/SMS gateway) whenever:

- Water level crosses into the **Danger** zone.
- The purifier mechanism opens or closes.
- A **possible blockage** is flagged or cleared.

## Sensor Calibration Notes

- **Ultrasonic baseline** — Measure and record the sensor-to-road distance during a dry, no-water condition; this becomes `baseline_distance`.
- **Threshold tuning** — Set `SAFE_THRESHOLD` and `DANGER_THRESHOLD` based on the specific road/drain geometry; leave a meaningful hysteresis gap between them.
- **Flow sensor pulse constant** — Each flow sensor model has a pulses-per-liter constant (check the datasheet); calibrate this value for accurate L/min readings.
- **Rain sensor sensitivity** — Adjust the onboard potentiometer (if analog) so light drizzle vs. heavy rain vs. dry are reliably distinguished.

## Example Serial Output

```
[12:04:31] Rain: DETECTED
[12:04:31] Water Level: 42 cm (SAFE)
[12:04:31] Purifier: CLOSED
[12:04:31] Flow Rate: 6.2 L/min (NORMAL)
-----------------------------------------
[12:07:18] Rain: DETECTED
[12:07:18] Water Level: 78 cm (DANGER)
[12:07:18] >> Threshold crossed — Opening Purifier
[12:07:18] Servo -> 180°
[12:07:18] Purifier: OPEN
[12:07:19] Flow Rate: 0.1 L/min (LOW)
[12:07:19] ⚠ WARNING: Possible drainage blockage detected
-----------------------------------------
[12:15:47] Water Level: 25 cm (SAFE)
[12:15:47] >> Threshold cleared — Closing Purifier
[12:15:47] Servo -> 0°
[12:15:47] Purifier: CLOSED
```

## Applications

- Municipal road drainage monitoring in flood-prone areas.
- Smart city infrastructure for automated stormwater management.
- Early-warning systems for waterlogging on highways and underpasses.
- Predictive maintenance for drainage networks via blockage detection.
- Research and academic demonstrations of IoT-based environmental monitoring.

---

### 🛠️ Built With

`PlatformIO` · `C++ (Arduino Framework)` · `ESP32/ESP8266` · `IoT Dashboard Integration`

