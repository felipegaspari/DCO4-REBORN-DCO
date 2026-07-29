#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/docs/PINOUT.md"
# DCO board pinout (provisional) — hub + CV absorption

**Status:** Phase 0 complete (provisional). **Not frozen for PCB fab** until reviewed against the physical monosynth carrier.

**Platform assumption:** RP2350 with GPIO0–29 available (custom / WEACT-style). Stock Pico 2 header only exposes **26** GPIOs and may omit some pins used below (notably **GPIO29**). Production default: prefer **RP2350B carrier (48 GPIO)** for headroom.

Related: [`MAINBOARD_ABSORPTION.md`](MAINBOARD_ABSORPTION.md). Live constants still only cover RESET/RANGE/PW/cal in [`globals.h`](../globals.h); hub/CV pins are commented there until Phase 3.

---

## Phase 0 locked defaults

| Decision | Choice |
|----------|--------|
| Carrier | Target **RP2350B** for production; bench may use Pico 2 / WEACT if pins fit |
| UART split | **Input + Screen on HW UARTs**; **DIN MIDI on PIO UART** (interim: HW MIDI @ GP0/1, Screen on PIO) |
| Cut0 vs PW | Cut0 on **GP15** (slice 7 B) — **avoid sharing slice 1 with PW** (wrap 1024 vs CV ~4095) |
| Reso1 | **GP7** (slice 3 B), not GP6 (would collide with RANGE OSC2 slice 3 A) |

---

## UART allocation (only 2 hardware UARTs)

| Role | Peripheral | Pins (provisional) | Baud | Notes |
|------|------------|--------------------|------|-------|
| **Input** (panel) | HW `UART0` / `Serial1` | **GP20 TX / GP21 RX** | 2 500 000 | Recycle today’s Mainboard link pins |
| **Screen** | HW `UART1` / `Serial2` | **GP8 TX / GP9 RX** | 2 500 000 | New; former Mainboard Serial1 peer |
| **DIN MIDI** | **PIO UART** | **GP0 TX / GP1 RX** | 31 250 | Move off PL011; keep USB MIDI |
| Mainboard link | — | *(removed)* | — | Today’s Serial2 @ GP20/21 retired |

Soft bit-bang at 2.5 M is not acceptable.

**Interim bring-up (safer MIDI):** DIN on HW UART0 @ GP0/1; Screen on PIO UART @ GP8/9; Input on HW UART1 @ GP20/21.

---

## Live DCO outs (unchanged until PCB freeze)

From [`globals.h`](../globals.h) today:

| Function | GPIO | Block | PWM slice (`(gpio>>1)&7` for gpio &lt; 32) | Channel |
|----------|------|-------|---------------------------------------------|---------|
| OSC1 RESET | 29 | PIO0 SM0 | — | — |
| OSC2 RESET | 27 | PIO1 SM0 | — | — |
| OSC3 RESET | 19 | PIO2 SM0 | — | — |
| OSC1 RANGE | 28 | PWM | 6 | A |
| OSC2 RANGE | 22 | PWM | 3 | A |
| OSC3 RANGE | 17 | PWM | 0 | B |
| PW (voice 0) | 3 | PWM | 1 | B |
| Cal sense | 10 | GPIO in | — | — |
| Board fix rails | 23, 24 | GPIO out HIGH | — | — |

---

## New CV / mux / DAC (provisional)

| Function | GPIO | PWM slice | Ch | Notes |
|----------|------|-----------|----|-------|
| Cutoff 0 | **15** | 7 | B | Not on slice 1 with PW |
| Cutoff 1 | **4** | 2 | A | `NUM_FILTERS=2` |
| Resonance 0 | **5** | 2 | B | Same slice as cut1 (shared wrap OK for CV) |
| Resonance 1 | **7** | 3 | B | **Shares slice 3 with RANGE OSC2 (GP22)** — firmware scales duty into `DIV_COUNTER` |
| VCA | **11** | 5 | B | |

| Function | GPIO | Notes |
|----------|------|-------|
| 74HC595 DATA | **12** | Port Mainboard `waveSelector` |
| 74HC595 LATCH | **13** | |
| 74HC595 CLK | **14** | |
| I2C0 SDA | **16** | MCP4728 ×3 @ 0x63/0x64/0x65 |
| I2C0 SCL | **18** | |

**Spare / TBD:** GP2, GP6, GP25, GP26 (ADC-capable). Prefer leaving ADC pins free unless needed.

---

## Pin occupancy summary (provisional full map)

| GPIO | Role |
|------|------|
| 0,1 | PIO MIDI (or HW MIDI interim) |
| 3 | PW PWM |
| 4 | Cutoff 1 PWM |
| 5 | Resonance 0 PWM |
| 7 | Resonance 1 PWM |
| 8,9 | HW UART Screen |
| 10 | Cal sense |
| 11 | VCA PWM |
| 12–14 | 74HC595 |
| 15 | Cutoff 0 PWM |
| 16,18 | I2C MCP4728 |
| 17,22,28 | RANGE ×3 |
| 19,27,29 | RESET ×3 (PIO) |
| 20,21 | HW UART Input |
| 23,24 | Board fix |
| 2,6,25,26 | Spare / TBD |

Approx **24** GPIOs used → stock Pico 2 is tight; **RP2350B recommended** for production.

---

## PWM mux cautions

- Formula (SDK): slice = `(gpio >> 1) & 7` for gpio &lt; 32; channel = `gpio & 1`.
- GPIOs that differ by **16** can alias the same slice/channel — do not use both as PWM.
- Channels on one slice share **wrap/clock**; duty is independent. CV pairs (cut1+reso0 on slice 2) are intentional.

---

## Feature flags (code)

```text
ENABLE_CV_OUTS      // PWM VCF/VCA/reso writers — landed (PWM.ino / cv_out.ino)
ENABLE_WAVE_MUX     // 74HC595 — landed (wave_mux.ino)
ENABLE_MCP4728      // Wire DAC levels — landed (mcp4728_dco.ino)
ENABLE_INPUT_UART   // panel hub — landed
ENABLE_SCREEN_UART  // UI / cal — Phase 4
ENABLE_PIO_MIDI     // DIN on PIO UART — Phase 4
```

Uncomment flags in `DCO.ino`. Pin constants appear when any of the Phase 3 HW flags is set (`globals.h`).
