# DCO4-REBORN – DCO Voice Board

Firmware for the **DCO voice board** of DCO4-REBORN: **4 MIDI voices × 2 oscillators**, forked from DCO3-MONOSYNTH and restored to old DCO4 voicing. Targets **RP2040 and RP2350**.

## Target model

| Item | Value |
|------|--------|
| Voices | `NUM_VOICES_TOTAL = 4` |
| Oscillators | `NUM_OSCILLATORS = 8` |
| Freq SMs | pio0+pio1 (8); RP2350 pio2 ×4 sub TBD |
| Amplitude | RANGE HW PWM (`RANGE0_PIO_DITHER_TEST` off) |
| Voice entry | `voice_task_main()` → float or fixed engine |
| Sync | per-voice A↔B |
| PW | 4 channels `{3,2,4,5}` |
| CV / mux | `ENABLE_CV_OUTS` / `WAVE_MUX` **off**; code kept |

`setVoiceMode`: 0 mono / 1 poly / 2 stack.

## Mainboard peer (classic PCB)

Serial1 is DIN MIDI. Serial2 GP20/21 is the **STM32 Mainboard** (not Input). TX `'n'`/`'o'`/`'e'`/`'x'`/`'p'`; RX slim `'p'` + `'m'` (LFO1/2 Q15, EnvDCO Q15×4, matrix pitch Q24). `ENABLE_MB_MOD_STREAM` (default) disables local LFO1/2 + EnvDCO clocks; pitch drift + Character + depth bakes stay here. Leave `ENABLE_CV_OUTS` / `ENABLE_WAVE_MUX` off — analog lives on Mainboard. Docs: [`docs/MAINBOARD_REINTEGRATION.md`](docs/MAINBOARD_REINTEGRATION.md), [`docs/SYSTEM_OVERVIEW.md`](docs/SYSTEM_OVERVIEW.md).

## Build

```bash
arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico2:usbstack=tinyusb \
  --libraries ./_build_libs \
  .
```

Main sketch: `DCO.ino`.

### Libraries (`_build_libs`)

Vendored Arduino libraries used by the build (passed via `--libraries ./_build_libs`):

| Library | Path | Notes |
|---------|------|--------|
| `ADSR_Bezier` | `_build_libs/ADSR_Bezier` → `../../ADSR_Bezier` | Symlink to monorepo root; track **`Q15`**. Toggle math backend in [`adsr.h`](adsr.h) via `ADSR_BEZIER_USE_FLOAT` (default `0` = fixed-point). |
| `DCO_Noise` | `_build_libs/DCO_Noise` → `../../DCO_Noise` | Symlink to monorepo root; noise engines (`begin`/`next`). Fleet size in [`noise.h`](noise.h). |
| `mo-lfo` | `_build_libs/mo-lfo` → `../../mo-lfo` | Symlink to monorepo root; track **`q15`**. Isolated like ADSR: `LFO.h` includes the header via `_build_libs/...`; `LFO.ino` `#include`s `mo-lfo.cpp` so Arduino IDE links it (no sketchbook library). |
| `MIDI_Library` | `_build_libs/MIDI_Library` | Vendored copy |
| `PID_v1` | `_build_libs/PID_v1` | Vendored copy |

**Monorepo layout:** clone `DCO4-REBORN` with sibling [`ADSR_Bezier`](../ADSR_Bezier/), [`DCO_Noise`](../DCO_Noise/), and [`mo-lfo`](../mo-lfo/) at the repo root so the symlinks resolve.

**Standalone DCO clone:** replace the symlink with a submodule:

```bash
cd _build_libs
rm -f ADSR_Bezier
git submodule add -b Q15 https://github.com/felipegaspari/ADSR_Bezier.git ADSR_Bezier
git submodule update --init _build_libs/ADSR_Bezier
```

Ensure `ADSR_Bezier` is on branch **`Q15`** (and `mo-lfo` on **`q15`**). To try the float envelope path on Pico 2, set `ADSR_BEZIER_USE_FLOAT` to `1` in `adsr.h` before building.

## Docs

See `docs/` — especially:

- [`docs/BUILD_FLAGS.md`](docs/BUILD_FLAGS.md) — complete compile-time flag catalog (`DCO.ino` + headers + vendored libs)
- [`docs/ENGINE_OPTIONS.md`](docs/ENGINE_OPTIONS.md) — float vs fixed voice/amp/CV math depth
- [`docs/BENCHMARKING.md`](docs/BENCHMARKING.md) — hot-path CPU profiler
- [`docs/MEMORY.md`](docs/MEMORY.md) — SRAM / heap / stack (`__not_in_flash_func`, dump cmd 13)
- [`docs/PINOUT.md`](docs/PINOUT.md) — live 4×2 RESET/RANGE/PW/cal + UART; CV/mux draft unused
- [`docs/MOD_MATRIX.md`](docs/MOD_MATRIX.md) — sparse mod matrix (levels / dual reso / Dist Drive)
- [`docs/CV_MOD_SCALES.md`](docs/CV_MOD_SCALES.md) — VCA/VCF mod depth bakers, Q15 peak math (`/512` vs `/1024`), when to call
- [`docs/LFO.md`](docs/LFO.md) — LFO Q15 bus; pitch/drift depth scales in `LFO.h`
- [`docs/UPDATE_CV_OUTS_HOT_PATH.md`](docs/UPDATE_CV_OUTS_HOT_PATH.md) — `update_CV_outs` ~10 kHz call-graph map
- [`docs/CHARACTER.md`](docs/CHARACTER.md) — Character / noise imperfection knobs, inject sites, and removal checklist
- [`../VOICE-AUX/docs/I2S_NOISE.md`](../VOICE-AUX/docs/I2S_NOISE.md) — PCM5102 I2S noise listen on voice-aux (not on DCO)
- [`docs/WAVE_MUX.md`](docs/WAVE_MUX.md) — per-osc Saw/Pulse/Tri via dual 595 + DG411
- [`docs/DUAL_MCU.md`](docs/DUAL_MCU.md) — RP2350A + RP2040 aux; `ENABLE_VOICE_AUX`; ParamId ownership
- [`../VOICE-AUX/`](../VOICE-AUX/) — RP2040 helper firmware (Dist / filter mode)
- [`docs/DISTORTION.md`](docs/DISTORTION.md) — post-LP Drive/Mix distortion stage (hardware idea + CV prototype)
- [`docs/FILTER_ROUTING.md`](docs/FILTER_ROUTING.md) — SSI2144 → dist → AS3320 multimode concept
- [`docs/PIO_OSCILLATORS.md`](docs/PIO_OSCILLATORS.md) — PIO programs, state machine topology, period model, sync modes, phase align, sub-osc, and the invariants behind them
- [`tools/dco_control/`](tools/dco_control/README.md) — Linux bench controller: drive every parameter over USB with no Input board or Screen attached
- [`docs/MIDI_CC_MAP.md`](docs/MIDI_CC_MAP.md) — MIDI CC implementation chart: the same control surface over 7-bit CC, for a panel app or a DAW (generated, along with the Open Stage Control session in `tools/panels/`)
- [`docs/MAINBOARD_ABSORPTION.md`](docs/MAINBOARD_ABSORPTION.md) — plan to absorb Mainboard into DCO
- `AUTOTUNE.md`, `SYSTEM_OVERVIEW.md`, `FILE_INDEX.md`

Removed dead code lives under `_removed/` and is not compiled.
