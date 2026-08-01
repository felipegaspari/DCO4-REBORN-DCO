# DCO3-MONOSYNTH – Pico 2 DCO Voice Board

Firmware for the **DCO voice board** of DCO3-MONOSYNTH: **1 voice × 3 oscillators**, based on DCO4 but retargeted to **Raspberry Pi Pico 2 (RP2350)** with 3 PIO blocks.

Branch baseline: `autotune-improvements` (refactored autotune / amp-comp), then monosynth voicing.

## Target model

| Item | Value |
|------|--------|
| Voices | `NUM_VOICES_TOTAL = 1` |
| Oscillators | `NUM_OSCILLATORS = 3` |
| Freq SMs | PIO0/1/2 SM0 |
| Amplitude | Hardware RANGE PWM (`get_chan_level_for_engine`) |
| Voice entry | `voice_task_main()` → float or fixed engine |
| Sync | OSC1↔OSC2; OSC3 free-running |
| PW | One shared PW channel (voice 0); inverted `PW_LOOKUP` |

Poly/`setVoiceMode` scaffolding kept for a later paraphonic mode.

## Hub mode

Serial1 is DIN MIDI, Serial2 is the Input board — the DCO's only peer link. It pairs with the Input's Serial1: the DCO's Serial2 RX (GP21) is fed by the Input's TX (GP0), while the DCO's Serial2 TX (GP20) drives the Input's RX (GP1). Gap 154 and cal offsets 155 go out as `'x'` frames on that TX, and Input forwards 154 to the Screen; the DCO has no Screen port of its own. Soft EnvVCA/EnvVCF always run; uncomment `ENABLE_CV_OUTS` / `ENABLE_WAVE_MUX` for hardware CV / mux / OSC level PWM writers. Docs: [`docs/MAINBOARD_ABSORPTION.md`](docs/MAINBOARD_ABSORPTION.md), [`docs/SYSTEM_OVERVIEW.md`](docs/SYSTEM_OVERVIEW.md).

## Build

```bash
arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico2:usbstack=tinyusb \
  --libraries ./_build_libs \
  .
```

Main sketch: `DCO.ino`.

## Docs

See `docs/` — especially:

- [`docs/PINOUT.md`](docs/PINOUT.md) — provisional hub + CV pin / UART map (Phase 0)
- [`docs/MOD_MATRIX.md`](docs/MOD_MATRIX.md) — sparse mod matrix (levels / dual reso / Dist Drive)
- [`docs/WAVE_MUX.md`](docs/WAVE_MUX.md) — per-osc Saw/Pulse/Tri via dual 595 + DG411
- [`docs/DUAL_MCU.md`](docs/DUAL_MCU.md) — RP2350A + RP2040 aux; `ENABLE_VOICE_AUX`; ParamId ownership
- [`../VOICE-AUX/`](../VOICE-AUX/) — RP2040 helper firmware (Dist / filter mode)
- [`docs/DISTORTION.md`](docs/DISTORTION.md) — post-LP Drive/Mix distortion stage (hardware idea + CV prototype)
- [`docs/FILTER_ROUTING.md`](docs/FILTER_ROUTING.md) — SSI2144 → dist → AS3320 multimode concept
- [`docs/PIO_OSCILLATORS.md`](docs/PIO_OSCILLATORS.md) — PIO programs, state machine topology, period model, sync modes, phase align, sub-osc, and the invariants behind them
- [`tools/dco_control/`](tools/dco_control/README.md) — Linux bench controller: drive every parameter over USB with no Input board or Screen attached
- [`docs/MIDI_CC_MAP.md`](docs/MIDI_CC_MAP.md) — MIDI CC implementation chart: the same control surface over 7-bit CC, for a panel app or a DAW (generated, along with the Open Stage Control session in `tools/panels/`)
- [`docs/MAINBOARD_ABSORPTION.md`](docs/MAINBOARD_ABSORPTION.md) — plan to absorb Mainboard into DCO
- `AUTOTUNE.md`, `SYSTEM_OVERVIEW.md`, `FILE_INDEX.md` (some docs may still mention DCO4; prefer this README for monosynth facts)

Removed dead code lives under `_removed/` and is not compiled.
