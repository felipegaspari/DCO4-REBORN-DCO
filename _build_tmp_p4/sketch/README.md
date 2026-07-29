#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/README.md"
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

## Hub mode (Mainboard absorption)

Opt-in hub flags in `DCO.ino`: `ENABLE_INPUT_UART` (Serial2 = Input), `ENABLE_SCREEN_UART` (SerialPIO GP8/9 → Screen gap). Soft EnvVCA/EnvVCF always run; uncomment `ENABLE_CV_OUTS` / `ENABLE_WAVE_MUX` / `ENABLE_MCP4728` for hardware writers. See [`docs/MAINBOARD_ABSORPTION.md`](docs/MAINBOARD_ABSORPTION.md).

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
- [`docs/MAINBOARD_ABSORPTION.md`](docs/MAINBOARD_ABSORPTION.md) — plan to absorb Mainboard into DCO
- `AUTOTUNE.md`, `SYSTEM_OVERVIEW.md`, `FILE_INDEX.md` (some docs may still mention DCO4; prefer this README for monosynth facts)

Removed dead code lives under `_removed/` and is not compiled.
