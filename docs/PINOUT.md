# DCO board pinout (provisional) — hub + CV absorption

**Status:** Phase 0 complete (provisional). **Not frozen for PCB fab** until reviewed against the physical monosynth carrier.

**Platform assumption:** RP2350 with GPIO0–29 available (custom / WEACT-style). Stock Pico 2 header only exposes **26** GPIOs and may omit some pins used below (notably **GPIO29**). Production default: prefer **RP2350B carrier (48 GPIO)** for headroom.

Related: [`MAINBOARD_ABSORPTION.md`](MAINBOARD_ABSORPTION.md). Live constants still only cover RESET/RANGE/PW/cal in [`globals.h`](../globals.h); hub/CV pins are commented there until Phase 3.

---

## Phase 0 locked defaults

| Decision | Choice |
|----------|--------|
| Carrier | Target **RP2350B** for production; bench may use Pico 2 / WEACT if pins fit |
| UART split | **Input on HW UART** (only peer link); Screen UI and gap via Input relay; **DIN MIDI on PIO UART** long-term (interim: HW MIDI @ GP0/1) |
| Cut0 vs PW | Cut0 on **GP15** (slice 7 B) — **avoid sharing slice 1 with PW** (wrap 1024 vs CV ~4095) |
| Reso1 | **GP7** (slice 3 B), not GP6 (would collide with RANGE OSC2 slice 3 A) |

---

## UART allocation (only 2 hardware UARTs)

| Role | Peripheral | Pins (provisional) | Baud | Notes |
|------|------------|--------------------|------|-------|
| **Input** (panel + gap) | HW `UART1` / `Serial2` | **GP20 TX / GP21 RX** | 2 500 000 | Only peer link, against the Input's `Serial1`: GP21 RX ← Input TX GP0 (panel data); GP20 TX → Input RX GP1 (`'x'` 154/155). Input relays gap 154 to Screen |
| ~~Screen (direct)~~ | *(removed)* | *(GP8 → sub-osc, GP9 spare)* | 2 500 000 | Was SerialPIO GP8/9; deleted — gap reaches Screen through Input |
| **DIN MIDI** | HW `UART0` interim → **PIO UART** | **GP0 TX / GP1 RX** | 31 250 | Keep USB MIDI |
| ~~Mainboard link~~ | *(removed)* | — | — | Archived STM32; no firmware path remains |

Soft bit-bang at 2.5 M is not acceptable.

**Current:** DIN on HW UART0 @ GP0/1 (`Serial1` in Arduino-Pico); Input on HW UART1 @ GP20/21 (`Serial2`). Both hardware UARTs are spoken for, and the DCO has no Screen port at all — gap display goes out on the Input link and Input forwards it on its own `Serial2`. GP8 now carries the sub-oscillator square; GP9 is free.

The DCO's pins on the Input link are fixed at GP20 TX / GP21 RX, and both wires terminate on the Input's `Serial1`: GP21 RX comes from the Input's TX (GP0), GP20 TX goes to the Input's RX (GP1). The Input's other UART, `Serial2`, drives the Screen from GP4; its RX (GP5) is not wired.

---

## Live DCO outs (unchanged until PCB freeze)

From [`globals.h`](../globals.h) today:

| Function | GPIO | Block | PWM slice (`(gpio>>1)&7` for gpio &lt; 32) | Channel |
|----------|------|-------|---------------------------------------------|---------|
| OSC1 RESET | 29 | PIO0 SM0/1 | — | — |
| OSC2 RESET | 27 | PIO0 SM0/1 | — | — |
| OSC3 RESET | 19 | PIO0 SM2 | — | — |
| Sub-osc out | 8 | PIO1 SM0 | — | — |
| OSC1 RANGE | 28 | PWM | 6 | A |
| OSC2 RANGE | 22 | PWM | 3 | A |
| OSC3 RANGE | 17 | PWM | 0 | B |
| PW (voice 0) | 3 | PWM | 1 | B |
| Cal sense | 10 | GPIO in | — | — |
| Board fix rails | 23, 24 | GPIO out HIGH | — | — |

**All three oscillators must stay on PIO0.** A GPIO's function select names exactly one PIO
block, so oscillators split across blocks cannot share a reset pin — the second
`pio_gpio_init()` steals the pin from the first, which silently breaks hard sync. OSC1 and
OSC2 swap SM indices depending on `syncMode` (`assign_sm_mapping()`) so the master always
outranks its slave; OSC3 is always SM2. `pio_topology_report()` verifies this at runtime.

PIO block budget: **PIO0** oscillators (25 of 32 instructions: `frequency_sync_4_jumps` 12 +
`frequency_sync_poll` 13), **PIO1** sub-oscillator (12 of 32), **PIO2** reserved for
`ENABLE_PIO_MIDI`.

Full detail on the programs, the period model, sync modes and phase align:
[`PIO_OSCILLATORS.md`](PIO_OSCILLATORS.md).

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
| 8 | Sub-osc square out (PIO1) — needs a mixer input on the carrier |
| 9 | Spare (was the SerialPIO Screen UART, now removed) |
| 10 | Cal sense |
| 11 | VCA PWM |
| 12–14 | 74HC595 |
| 15 | Cutoff 0 PWM |
| 16,18 | I2C MCP4728 |
| 17,22,28 | RANGE ×3 |
| 19,27,29 | RESET ×3 (all PIO0) |
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
ENABLE_PIO_MIDI     // DIN on PIO UART — Phase 5 / next PCB bring-up
```

Uncomment Phase 3 HW flags in `DCO.ino` as needed. The Input link on Serial2 is unconditional: the `ENABLE_INPUT_UART`, `ENABLE_SCREEN_UART` and `ENABLE_LEGACY_MAINBOARD_LINK` flags were removed along with the Mainboard and SerialPIO paths.
