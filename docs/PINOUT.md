# DCO4-REBORN pinout — 4 voices × 2 oscillators

**Status:** Live RESET / RANGE selected by `DCO_MCU_BOARD` in [`project_config.h`](../../project_config.h) (default **WeAct RP2040**). **PW ×4** / cal match old DCO4. **Not frozen for PCB fab.** Four SUB GPIOs are TBD (`SUBOSC_PINS[]` still `0xFF`). Cut/Res/VCA/dist/levels/wave-mux **firmware is kept** behind `ENABLE_CV_OUTS` / `ENABLE_WAVE_MUX` (off) for later expansion — not used on this 4×2 board (draft pins collide with RESET/RANGE).

**Platform:** RP2040 and RP2350 (WEACT / Pico / Pico 2). Dual-MCU: [`DUAL_MCU.md`](DUAL_MCU.md). Live constants: [`globals.h`](../globals.h).

Related: [`MAINBOARD_ABSORPTION.md`](MAINBOARD_ABSORPTION.md), [`PIO_OSCILLATORS.md`](PIO_OSCILLATORS.md).

---

## Live DCO outs (`DCO_MCU_BOARD`)

From [`globals.h`](../globals.h). Switch the MCU module in [`project_config.h`](../../project_config.h) (`DCO_MCU_WEACT_RP2040` / `DCO_MCU_PICO` / `DCO_MCU_PICO2`). Official Pico and Pico 2 share the 40-pin header; only osc 0/1 move because WeAct breaks out GPIO 29 and Pico/Pico 2 use that pad as the VSYS ADC.

**WeAct RP2040** (`DCO_MCU_WEACT_RP2040`, this tree’s default):

| Osc / voice | RESET | RANGE | PIO block / default SM | PW (per voice) |
|-------------|-------|-------|------------------------|----------------|
| V0 A (osc 0) | 29 | 28 | pio0 SM0 | GP3 |
| V0 B (osc 1) | 27 | 22 | pio0 SM1 | — |
| V1 A (osc 2) | 19 | 17 | pio0 SM2 | GP2 |
| V1 B (osc 3) | 18 | 16 | pio0 SM3 | — |
| V2 A (osc 4) | 15 | 14 | pio1 SM0 | GP4 |
| V2 B (osc 5) | 13 | 11 | pio1 SM1 | — |
| V3 A (osc 6) | 12 | 9 | pio1 SM2 | GP5 |
| V3 B (osc 7) | 8 | 7 | pio1 SM3 | — |

**Raspberry Pi Pico / Pico 2** (`DCO_MCU_PICO` / `DCO_MCU_PICO2`) — osc 2–7 unchanged:

| Osc / voice | RESET | RANGE |
|-------------|-------|-------|
| V0 A (osc 0) | 28 | 27 |
| V0 B (osc 1) | 26 | 22 |
| V1–V3 (osc 2–7) | same as WeAct | same as WeAct |

`VOICE_TO_PIO[]` = `{0,0,0,0,1,1,1,1}`. A voice pair (A+B) **must share one PIO block** so hard/soft sync can share a RESET pin. `VOICE_TO_SM[]` is mutable: within a pair the slave takes the lower local SM so hard-sync sideset wins. `PW_PINS[]` is length **4**: `{3,2,4,5}` (one PW PWM per MIDI voice — shipping).

| Function | GPIO | Notes |
|----------|------|-------|
| Cal sense | **10** | `DCO_calibration_pin` (old DCO4). GP6 is free. |
| WeAct KEY | **23** | `USER_KEY_PIN`: `INPUT_PULLUP`, hold = MIDI 69 / A440. Not a header output. |
| Analog board-fix | **24** | `BOARD_FIX_PIN` WeAct only, OUT HIGH. Pico/Pico 2: GP24 is VBUS sense — not driven. |
| SMPS Power Save | **23** | Pico/Pico 2 only (`SMPS_PS_PIN`): OUT HIGH forces RT6150 PWM (less 3.3 V ripple). |
| SUB ×4 | **TBD** | RP2350 only: pio2 SM0–3, one per voice, wait on that voice’s OSC A RESET (`RESET_PINS[v*2]`). All `SUBOSC_PINS[]` = `0xFF` until assigned. **GP8 is OSC8 RESET — do not reuse DCO3’s single SUB.** RP2040: no sub PIO. |
| Noise PIO LFSR | off | CPU `DCO_Noise` only. `NOISE_OUT_PIN` 2 unused. |
| RANGE PWM | HW slice | `RANGE0_PIO_DITHER_TEST` off (8 oscs). |

Full programs / sync / phase align: [`PIO_OSCILLATORS.md`](PIO_OSCILLATORS.md).

---

## UART allocation (only 2 hardware UARTs)

| Role | Peripheral | Pins | Baud | Notes |
|------|------------|------|------|-------|
| **Mainboard** | HW `UART1` / `Serial2` | **GP20 TX / GP21 RX** | 2 500 000 | Classic PCB: DCO Serial2 ↔ STM32 Serial2. TX `'n'`/`'o'`/`'e'`/`'x'`/`'p'`; RX `'m'`/`'p'` |
| ~~Screen (direct)~~ | *(removed)* | — | 2 500 000 | Gap 154 reaches Screen via Mainboard → Input → Screen. GP8 is OSC8 RESET. |
| **DIN MIDI** | HW `UART0` | **GP0 TX / GP1 RX** | 31 250 | Keep USB MIDI |

Soft bit-bang at 2.5 M is not acceptable.

**Current:** DIN on HW UART0 @ GP0/1 (`Serial1`); Mainboard on HW UART1 @ GP20/21 (`Serial2`). Input talks to Mainboard on its Serial2 (GP4/5) and to Screen on Serial1 (GP0). See [`MAINBOARD_REINTEGRATION.md`](MAINBOARD_REINTEGRATION.md).

---

## Hub / CV / mux (code retained, unused on this board)

Cut/Res/VCA/dist/osc-sub levels/wave mux are **not wired** on this 4×2 project. Leave `ENABLE_CV_OUTS` / `ENABLE_WAVE_MUX` off. The writers stay in-tree for later expansion; the draft pins below still collide with 8-osc RESET/RANGE (not a near-term remap).

| Function | GPIO | PWM slice | Ch | Notes |
|----------|------|-----------|----|-------|
| Cutoff 0 | **15** | 7 | B | Collides with V2 A RESET |
| Cutoff 1 | **4** | 2 | A | Collides with PW V1 |
| Resonance 0 | **5** | 2 | B | Collides with PW V3 |
| Resonance 1 | **7** | 3 | B | Collides with V3 B RANGE |
| VCA | **11** | 5 | B | Collides with V2 B RANGE |
| Dist Drive | **9** | 4 | B | Collides with V3 A RANGE. Dual-MCU: `ENABLE_VOICE_AUX` — aux pins in [`VOICE-AUX/docs/README.md`](../../VOICE-AUX/docs/README.md) |
| Dist Mix | **26** | 5 | A | Free on live 4×2; solo-B draft |
| OSC1 level | **16** | 0 | A | Collides with V1 B RANGE |
| OSC2 level | **18** | 1 | A | Collides with V1 B RESET |
| OSC3 level | **9** / **32** | — | — | Dual-MCU: Dist Drive **GP9**. Solo-B: **GP32** |
| Sub level | **26** / **33** | — | — | Dual-MCU: Dist Mix **GP26**. Solo-B: **GP33** |

| Function | GPIO | Notes |
|----------|------|-------|
| 74HC595 DATA | **12** | Collides with V3 A RESET — [`WAVE_MUX.md`](WAVE_MUX.md) |
| 74HC595 LATCH | **13** | Collides with V2 B RESET |
| 74HC595 CLK | **14** | Collides with V2 A RANGE |
| AS3320 mode | *TBD* | Dual-MCU → **RP2040**; solo-B → DCO spares — [`FILTER_ROUTING.md`](FILTER_ROUTING.md) |

**Wave mux:** 2× 74HC595 drive 3× DG411 (OSC1–3 × Saw/Pulse/Tri). Provisional bits 0–8; 9–15 unused. Active-low. Not used on this board.

**I2C level DAC dropped** — osc/sub levels would be PWM → analog level VCAs if CV is enabled later. Soft bases still update with `ENABLE_CV_OUTS` off.

**Do not use for level PWM (when remapping):** GP2 (aliases GP18), GP6 (aliases RANGE OSC V0 B GP22).

**GP2** — leftover `NOISE_OUT_PIN`; noise PIO LFSR is off.

---

## Pin occupancy summary (live 4×2 only)

| GPIO | Role |
|------|------|
| 0,1 | HW MIDI UART0 |
| 2,3,4,5 | PW voices 0–3 |
| 6 | free (later SUB candidate) |
| 7,9,11,14,16,17,22,28 | RANGE ×8 (WeAct). Pico / Pico 2: RANGE osc 0 is GP27, not GP28 |
| 8,12,13,15,18,19,27,29 | RESET ×8 (WeAct). Pico / Pico 2: RESET osc 0/1 are GP28/26, not GP29/27 |
| 10 | Cal sense |
| 20,21 | HW UART1 Input |
| 23 | WeAct: onboard KEY (A440). Pico/Pico 2: SMPS PS, OUT HIGH |
| 24 | WeAct: analog board-fix OUT HIGH. Pico/Pico 2: VBUS sense, not driven |
| 25 | Pico LED (not on header) |
| 26 | Pico / Pico 2: RESET osc 1. WeAct: free (Dist Mix only if CV is enabled later) |
| SUB ×4 | TBD (RP2350 pio2 SM0–3) |

---

## PWM mux cautions

- Formula (SDK): slice = `(gpio >> 1) & 7` for gpio &lt; 32; channel = `gpio & 1`.
- GPIOs that differ by **16** can alias the same slice/channel — do not use both as PWM.
- Channels on one slice share **wrap/clock**; duty is independent.

---

## Feature flags (code)

```text
ENABLE_CV_OUTS           // off — Cut/Res/VCA/dist/levels writers kept for later expansion
ENABLE_WAVE_MUX          // off — same; draft pins collide with 8-osc RESET/RANGE
ENABLE_VOICE_AUX         // Dist/mode on RP2040 when aux is used
ENABLE_PIO_RESET_INVERT  // RESET pad active-low (DG411 discharge); OUTOVER+INOVER
ENABLE_NOISE_OUT         // leftover; noise PIO LFSR is off on both MCUs
ENABLE_PIO_MIDI          // DIN on PIO UART — later PCB bring-up
```
PCM5102 I2S noise listen lives on **VOICE-AUX** (see [`../../VOICE-AUX/docs/I2S_NOISE.md`](../../VOICE-AUX/docs/I2S_NOISE.md)).

Leave `ENABLE_CV_OUTS` / `ENABLE_WAVE_MUX` commented on this board. Serial2 is the Mainboard link (`ENABLE_MAINBOARD_LINK` / `ENABLE_MB_MOD_STREAM`).
