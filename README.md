## DCO4 – RP2040 / RP2350 DCO Voice Board (4‑voice, 2 DCOs per voice)

DCO4 is a **digitally‑controlled oscillator (DCO) voice board** built around an RP2040 / RP2350–class MCU.  
It implements **4 independent synth voices**, each with **2 DCOs**, envelopes, LFOs, calibration and MIDI control, and is designed to be driven by a separate main controller and UI.

The aim of the project is to create a FULLY DIGITALLY CONTROLLED ANALOG SYNTH, with patch saving for all parameters.

This repository contains the **firmware for the DCO4 voice board**. How it fits with the mainboard, input controller, and screen board is described in **[`docs/SYSTEM_OVERVIEW.md`](docs/SYSTEM_OVERVIEW.md)**.

Those boards talk to DCO4 over MIDI and a high‑speed UART link; this repo focuses on generating accurate, calibrated DCO waveforms.

All detailed documentation lives under **[`docs/`](docs/)**. This README stays at the repo root as the entry point.

---

## Documentation

| Doc | Status | Contents |
|-----|--------|----------|
| [`docs/DOCUMENTATION_PROCEDURE.md`](docs/DOCUMENTATION_PROCEDURE.md) | Current | Reusable phased guide to document any DCO4 board like this repo |
| [`docs/SYSTEM_OVERVIEW.md`](docs/SYSTEM_OVERVIEW.md) | Current | Four-board system roles, UART topology, ownership boundaries |
| [`docs/ENGINE_OPTIONS.md`](docs/ENGINE_OPTIONS.md) | Current | `USE_FLOAT_ENGINE`, fixed/float precision–speed paths, amp-comp, traps, quick-pick flag sets |
| [`docs/FILE_INDEX.md`](docs/FILE_INDEX.md) | Current | Every source file, every function, call sites (“Called from / When”) |
| [`docs/REFERENCE_AI.md`](docs/REFERENCE_AI.md) | Current | Deep semantic map for developers / AI assistants |
| [`docs/README_serial_and_params.md`](docs/README_serial_and_params.md) | Current | Shared serial / `ParamId` how-to (cross-board) |
| [`docs/Serial_comms_and_params_reference.txt`](docs/Serial_comms_and_params_reference.txt) | Historical | Earlier serial/param design notes |
| [`docs/AUTOTUNE.md`](docs/AUTOTUNE.md) | Current | Calibration algorithms and behaviour |
| [`docs/AUTOTUNE_REFACTORED.md`](docs/AUTOTUNE_REFACTORED.md) | Current | Post-refactor autotune file roles (`autotune_*.h`, etc.) |
| [`docs/FIXED_POINT_ANALYSIS.md`](docs/FIXED_POINT_ANALYSIS.md) | **Archive** | Pre-migration float inventory |
| [`docs/FIXED_POINT_PLAN.md`](docs/FIXED_POINT_PLAN.md) | **Archive** | Early FixMath migration plan (not the final approach) |

**Suggested reading order**

1. This README (features, build, hardware pins)
2. [`docs/SYSTEM_OVERVIEW.md`](docs/SYSTEM_OVERVIEW.md) — how boards connect
3. [`docs/ENGINE_OPTIONS.md`](docs/ENGINE_OPTIONS.md) — before changing math flags
4. [`docs/FILE_INDEX.md`](docs/FILE_INDEX.md) or [`docs/REFERENCE_AI.md`](docs/REFERENCE_AI.md) — navigate code
5. Autotune / serial docs as needed for calibration or protocol work

To document another board the same way, follow [`docs/DOCUMENTATION_PROCEDURE.md`](docs/DOCUMENTATION_PROCEDURE.md).

---

## Features

- **Polyphony & Oscillators**
  - 4 voices, each with 2 DCOs (8 oscillators total).
  - Mono, poly and stacked/unison voice modes.
  - Oscillator intervals and OSC2 detune.
  - Multiple oscillator sync modes (hard sync, phase alignment, etc.).

- **Sound Shaping & Modulation**
  - Bezier‑based ADSR envelope per voice (high‑resolution, microsecond timing).
  - LFO1 and LFO2 for pitch/detune and PWM or other modulation.
  - Per‑oscillator “analog drift” LFOs to simulate warm, unstable DCOs.
  - ADSR→pitch and ADSR→PWM modulation paths.
  - Velocity, pitch bend and CC support via MIDI.

- **Calibration & Stability**
  - Per‑oscillator DCO calibration using edge‑timing and a PID controller.
  - Frequency‑dependent amplitude compensation to keep level consistent across the spectrum.
  - Automatic PW center and low‑limit calibration.
  - Calibration data stored in flash using LittleFS and reused at boot.

- **Performance & Implementation**
  - Runs on both RP2040 cores:
    - Core 0: MIDI and serial I/O, LFO evaluation, cross‑core detune FIFO.
    - Core 1: envelopes, timers, calibration routines and the real‑time voice/DCO engine.
  - **Dual math engines** (compile-time): float path (default, FPU-friendly) or fixed-point path (RP2040 / no-FPU). Precision vs speed knobs for pitch interpolation and clock-divider are documented in [`docs/ENGINE_OPTIONS.md`](docs/ENGINE_OPTIONS.md).
  - PIO‑based DCO pulse generation with per‑oscillator clock dividers and phase control.

---

## High‑Level Architecture

At a very high level, the firmware is split into a few major subsystems:

- **Voice engine (`voices.*`)**
  - Converts MIDI note events into per‑DCO frequencies, including portamento, unison, pitch‑bend, LFO and ADSR modulation.
  - Computes PIO clock dividers and range PWM levels, and writes them to the PIO state machines and PWM slices.
  - Entry point: `voice_task_main()` → `voice_task_float()` or `voice_task()` depending on `USE_FLOAT_VOICE_TASK`.

- **Modulation (`adsr.*`, `LFO.*`)**
  - ADSR envelopes via the external Bezier ADSR library (`ADSR_Bezier`).
  - LFOs via the external mo‑thunderz LFO class (`mo-lfo`), used for LFO1, LFO2 and per‑oscillator drift.

- **Calibration (`autotune.*`, `amp_comp.h`, `PID.*`, `FS.*`)**
  - Autotune module measures DCO duty cycle and frequency at a calibration pin while sweeping amplitudes/PW.
  - Builds amplitude‑compensation tables per oscillator, precomputes windows/coefficients and stores them in flash.
  - Helpers: `autotune_constants.h`, `autotune_context.h`, `autotune_measurement.h`.

- **I/O (`midi.*`, `Serial.*`, `params.ino`)**
  - USB and DIN MIDI via Adafruit TinyUSB + `MIDI.h`.
  - High‑speed Serial2 link to the main controller with a simple binary protocol (`serial_protocol.h` + shared parser).
  - `params.ino` + `param_router.h` apply incoming `ParamId` values to the engine.

For a **deep, file‑by‑file technical map**, see [`docs/REFERENCE_AI.md`](docs/REFERENCE_AI.md). For float/fixed build flags, see [`docs/ENGINE_OPTIONS.md`](docs/ENGINE_OPTIONS.md). For a flat inventory of every function and who calls it, see [`docs/FILE_INDEX.md`](docs/FILE_INDEX.md).

---

## Hardware Overview

The firmware targets an RP2040 / RP2350 board wired roughly as follows (active pin map is **WEACT** in `globals.h`; a Pico variant is commented):

- 8 DCO outputs driven by PIO state machines (frequency and phase controlled by clock dividers).
- Per‑DCO **range PWM** outputs to set amplitude based on calibration (`RANGE_PINS`).
- Per‑voice **PW PWM** outputs to modulate pulse width (`PW_PINS` = 3, 2, 4, 5).
- Calibration input pin: **GPIO 10** (`DCO_calibration_pin`).
- UARTs:
  - `Serial1`: DIN MIDI — RX **1** / TX **0** @ 31250 baud.
  - `Serial2`: mainboard link — RX **21** / TX **20** @ 2.5 Mbaud.
- USB: MIDI over USB for testing and DAW hosts.
- System clock constant: **225 MHz** (`sysClock` in `globals.h`).

Exact pin mappings and hardware constants are defined in `globals.h`.

---

## Building and Flashing

The project is written as a standard Arduino sketch plus additional `.ino`/`.h` files.

### 1. Prerequisites

- **Toolchain**
  - Arduino IDE or another environment that supports the RP2040 / RP2350 Arduino core (e.g. Earle Philhower’s core).

- **Board support**
  - Install the Raspberry Pi Pico / RP2040 (and RP2350 if used) board package for Arduino.
  - Select the correct board (e.g. *Raspberry Pi Pico*, WEACT RP2040, or your RP2350 variant).

- **Libraries** (Arduino Library Manager or installed system libraries)
  - Adafruit TinyUSB for RP2040 (`Adafruit_TinyUSB`)
  - MIDI library (`MIDI.h`, FortySevenEffects)
  - PID controller (`PID_v1`)
  - LittleFS (provided by the RP2040 core)
  - **ADSR Bezier** (`ADSR_Bezier.h`) — install as a library; not vendored under `src/` in this repo
  - **mo-lfo** (`mo-lfo.h`) — same

### 2. Engine flags

Before building, review the flags at the top of `DCO4_DCO.ino`:

- `#define USE_FLOAT_ENGINE` — current default (float hot path). Comment out for the fixed-point engine.
- When fixed: `PITCH_USE_RATIO_Q16`, `PITCH_INTERP_USE_Q12` / `Q8`, `HIGH_PRECISION_CLKDIV`.

See [`docs/ENGINE_OPTIONS.md`](docs/ENGINE_OPTIONS.md) for precision vs speed trade-offs.

### 3. Build steps

1. Open `DCO4_DCO.ino` in the Arduino IDE (or configure your alternative build system to use it as the main sketch).
2. Select the appropriate board and serial port.
3. Ensure the required libraries are installed.
4. Compile and upload the sketch to the DCO4 board.

Once flashed, the board will enumerate as a USB MIDI device and will also listen on DIN MIDI / Serial2 as wired.

---

## Calibration Workflow (Overview)

Calibration is normally triggered from the main controller / UI via the high‑speed Serial2 protocol.  
At a high level, the flow is:

1. The controller sets the appropriate flags/parameters (e.g. “start calibration”).
2. DCO4 runs through:
   - Per‑DCO amplitude calibration across multiple notes.
   - PW center and (optionally) low‑limit finding.
3. Calibration data is written to LittleFS (`voiceTables`, `PWCenter`, `PWLowLimit`, etc.).
4. On the next boot, `init_FS()` loads those tables and `precompute_amp_comp_for_engine()` prepares fast lookup structures for the active engine.

Because the calibration routines directly control the oscillators and PWM outputs and can take a while, they are not normally run at every power‑up – the stored data is reused.

Details: [`docs/AUTOTUNE.md`](docs/AUTOTUNE.md), [`docs/AUTOTUNE_REFACTORED.md`](docs/AUTOTUNE_REFACTORED.md).

---

## Repository Layout (Quick Guide)

- **`README.md`** – this file (entry point).
- **`docs/`** – all project documentation (see table above).
- **`DCO4_DCO.ino`** – main sketch, core‑0/1 setup and loops, **engine build flags**.
- **`voices.*`** – polyphony, float and fixed voice engines, DCO clock‑divider logic.
- **`adsr.*`**, **`LFO.*`** – envelope and LFO integration (external libs).
- **`autotune.*`**, **`autotune_*.h`**, **`amp_comp.h`**, **`PID.*`**, **`FS.*`** – calibration and amplitude compensation.
- **`midi.*`**, **`Serial.*`**, **`serial_*.h`**, **`params_def.h`**, **`param_router.h`**, **`params.ino`** – MIDI, serial protocols, parameters.
- **`globals.h`**, **`state_machines.*`**, **`PWM.*`**, **`Timer_millis.*`**, **`noteList.h`**, **`utils.*`** – infrastructure, timing and helpers.
- **`pico-dco.pio` / `.pio.h`** – PIO DCO pulse programs.
- **`src/`** – empty placeholder (ADSR/LFO are external Arduino libraries, not vendored here).

---

## Contributing / Hacking

- Start with [`docs/SYSTEM_OVERVIEW.md`](docs/SYSTEM_OVERVIEW.md) and [`docs/REFERENCE_AI.md`](docs/REFERENCE_AI.md); use [`docs/FILE_INDEX.md`](docs/FILE_INDEX.md) to find call sites quickly.
- When changing the real‑time engine (`voices.*`):
  - Respect the active engine flags (`USE_FLOAT_*` vs fixed Q-formats).
  - Prefer the documented precision modes in [`docs/ENGINE_OPTIONS.md`](docs/ENGINE_OPTIONS.md) over ad-hoc float/fixed mixes.
- For new modulation paths or parameters, route them through:
  - `params.ino` / `params_def.h` (for serial/MIDI control),
  - appropriate globals in `globals.h`,
  - and the relevant engine module (`voices`, `adsr`, `LFO`, etc.).

Feel free to adapt this firmware to other RP2040 / RP2350–based synth projects; the voice engine, ADSR and LFO layers are quite modular and can be reused with different front‑ends or UI controllers.
