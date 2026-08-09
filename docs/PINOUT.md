# DCO4-REBORN pinout — 4 voices × 2 oscillators

**Status:** Live RESET / RANGE / PW / cal match **old DCO4 WEACT**. **Not frozen for PCB fab.** Four SUB GPIOs are TBD (`SUBOSC_PINS[]` still `0xFF`). Hub/CV absorption below is a **DCO3 leftover** — it collides with the 8-osc map; do **not** enable `ENABLE_CV_OUTS` / `ENABLE_WAVE_MUX` until that remap.

**Platform:** RP2040 and RP2350 (WEACT / Pico-class). Dual-MCU: [`DUAL_MCU.md`](DUAL_MCU.md). Live constants: [`globals.h`](../globals.h).

Related: [`MAINBOARD_ABSORPTION.md`](MAINBOARD_ABSORPTION.md), [`PIO_OSCILLATORS.md`](PIO_OSCILLATORS.md).

---

## Live DCO outs (old DCO4 WEACT)

From [`globals.h`](../globals.h):

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

`VOICE_TO_PIO[]` = `{0,0,0,0,1,1,1,1}`. A voice pair (A+B) **must share one PIO block** so hard/soft sync can share a RESET pin. `VOICE_TO_SM[]` is mutable: within a pair the slave takes the lower local SM so hard-sync sideset wins. `PW_PINS[]` is length 8 with voices 0–3 = `{3,2,4,5}` and osc 4–7 = `0xFF` (one PW per MIDI voice).

| Function | GPIO | Notes |
|----------|------|-------|
| Cal sense | **10** | `DCO_calibration_pin` (old DCO4). GP6 is free. |
| Board fix rails | 23, 24 | GPIO out HIGH (`DCO.ino`) |
| SUB ×4 | **TBD** | RP2350 only: pio2 SM0–3, one per voice, wait on that voice’s OSC A RESET (`RESET_PINS[v*2]`). All `SUBOSC_PINS[]` = `0xFF` until assigned. **GP8 is OSC8 RESET — do not reuse DCO3’s single SUB.** RP2040: no sub PIO. |
| Noise PIO LFSR | off | CPU `DCO_Noise` only. `NOISE_OUT_PIN` 2 unused. |
| RANGE PWM | HW slice | `RANGE0_PIO_DITHER_TEST` off (8 oscs). |

Full programs / sync / phase align: [`PIO_OSCILLATORS.md`](PIO_OSCILLATORS.md).

---

## UART allocation (only 2 hardware UARTs)

| Role | Peripheral | Pins | Baud | Notes |
|------|------------|------|------|-------|
| **Input** (panel + gap) | HW `UART1` / `Serial2` | **GP20 TX / GP21 RX** | 2 500 000 | Only peer link, against the Input's `Serial1`: GP21 RX ← Input TX GP0 (panel data); GP20 TX → Input RX GP1 (`'x'` 154/155). Input relays gap 154 to Screen |
| ~~Screen (direct)~~ | *(removed)* | — | 2 500 000 | Was SerialPIO GP8/9; deleted — gap reaches Screen through Input. GP8 is OSC8 RESET. |
| **DIN MIDI** | HW `UART0` interim → **PIO UART** | **GP0 TX / GP1 RX** | 31 250 | Keep USB MIDI |
| ~~Mainboard link~~ | *(removed)* | — | — | Archived STM32; no firmware path remains |

Soft bit-bang at 2.5 M is not acceptable.

**Current:** DIN on HW UART0 @ GP0/1 (`Serial1` in Arduino-Pico); Input on HW UART1 @ GP20/21 (`Serial2`). Both hardware UARTs are spoken for, and the DCO has no Screen port — gap display goes out on the Input link and Input forwards it on its own `Serial2`.

The DCO's pins on the Input link are fixed at GP20 TX / GP21 RX, and both wires terminate on the Input's `Serial1`: GP21 RX comes from the Input's TX (GP0), GP20 TX goes to the Input's RX (GP1). The Input's other UART, `Serial2`, drives the Screen from GP4; its RX (GP5) is not wired.

---

## Hub / CV / mux (NOT LIVE — DCO3 leftover)

Do **not** enable `ENABLE_CV_OUTS` or `ENABLE_WAVE_MUX` on this 4×2 map. The draft below collides with 8-osc RESET/RANGE (Cut0 GP15 = V2 A RESET, HC595 on RESET/RANGE GPIOs, level PWM on RANGE pins, etc.). Keep for a later remap (RP2350B extra pins and/or VOICE-AUX).

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

**Wave mux:** 2× 74HC595 drive 3× DG411 (OSC1–3 × Saw/Pulse/Tri). Provisional bits 0–8; 9–15 unused. Active-low. Not wired on 4×2 until remap.

**I2C level DAC dropped** — osc/sub levels are PWM → analog level VCAs when CV is remapped. Soft bases still update with `ENABLE_CV_OUTS` off.

**Do not use for level PWM (when remapping):** GP2 (aliases GP18), GP6 (aliases RANGE OSC V0 B GP22).

**GP2** — leftover `NOISE_OUT_PIN`; noise PIO LFSR is off.

---

## Pin occupancy summary (live 4×2 only)

| GPIO | Role |
|------|------|
| 0,1 | HW MIDI UART0 |
| 2,3,4,5 | PW voices 0–3 |
| 6 | free (later SUB candidate) |
| 7,9,11,14,16,17,22,28 | RANGE ×8 |
| 8,12,13,15,18,19,27,29 | RESET ×8 |
| 10 | Cal sense |
| 20,21 | HW UART1 Input |
| 23,24 | Board fix |
| 25 | Pico LED (not on header) |
| 26 | free (Dist Mix only after CV remap) |
| SUB ×4 | TBD (RP2350 pio2 SM0–3) |

---

## PWM mux cautions

- Formula (SDK): slice = `(gpio >> 1) & 7` for gpio &lt; 32; channel = `gpio & 1`.
- GPIOs that differ by **16** can alias the same slice/channel — do not use both as PWM.
- Channels on one slice share **wrap/clock**; duty is independent.

---

## Feature flags (code)

```text
ENABLE_CV_OUTS           // NOT LIVE on 4×2 — DCO3 leftover collides with RESET/RANGE
ENABLE_WAVE_MUX          // NOT LIVE on 4×2 — same
ENABLE_VOICE_AUX         // Dist/mode on RP2040; DCO reuses GP9/26 for OSC3/Sub levels (after remap)
ENABLE_PIO_RESET_INVERT  // RESET pad active-low (DG411 discharge); OUTOVER+INOVER
ENABLE_NOISE_OUT         // leftover; noise PIO LFSR is off on both MCUs
ENABLE_PIO_MIDI          // DIN on PIO UART — later PCB bring-up
```
PCM5102 I2S noise listen lives on **VOICE-AUX** (see [`../../VOICE-AUX/docs/I2S_NOISE.md`](../../VOICE-AUX/docs/I2S_NOISE.md)).

Uncomment Phase 3 HW flags in `DCO.ino` only after a 4×2 CV remap. The Input link on Serial2 is unconditional: the `ENABLE_INPUT_UART`, `ENABLE_SCREEN_UART` and `ENABLE_LEGACY_MAINBOARD_LINK` flags were removed along with the Mainboard and SerialPIO paths.
