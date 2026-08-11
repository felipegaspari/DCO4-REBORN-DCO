# DCO4-REBORN System Overview

DCO4-REBORN is a **digitally controlled analog polysynth** on **classic DCO4 PCB wiring**: STM32 Mainboard in the UART middle, **4 MIDI voices × 2 oscillators**. Control math is DCO3-style (Q15/Q24, bake-on-write, slim LE serial, jump-table ParamIds).

Board-specific detail lives in each folder’s `docs/`. Reintegration contract: [`MAINBOARD_REINTEGRATION.md`](MAINBOARD_REINTEGRATION.md) and [`../../MAINBOARD-CONTROLLER/docs/MAINBOARD_REINTEGRATION.md`](../../MAINBOARD-CONTROLLER/docs/MAINBOARD_REINTEGRATION.md).

---

## Boards and ownership

| Board | Folder | MCU | Owns |
|-------|--------|-----|------|
| **DCO** | `DCO/` | RP2040 / RP2350 | MIDI (USB+DIN), voice alloc (mono/poly/stack), PIO pitch, RANGE/PW PWM, amp-comp, autotune, LittleFS (cal + **the instrument's 256 preset slots**), Character pitch/PW jitter, per-osc **pitch** drift |
| **Mainboard** | `MAINBOARD-CONTROLLER/` | STM32 | EnvVCA ×4, EnvVCF ×4, EnvDCO ×4, LFO1/LFO2, VCF drift, mod matrix, VCA/VCF/reso PWM, MCP4728, 74HC595, Input RX, DCO peer, **preset frame relay** |
| **Input** | `INPUT-CONTROLLER/` | RP2040 | Panel scan, Screen UI frames; relays gap 154. **No filesystem** — RAM-only cache of the 256 preset names, refilled from the DCO |
| **Screen** | `SCREEN-CONTROLLER/` | RP2040 | LVGL UI |
| Voice aux | `VOICE-AUX/` | RP2040 | Optional Dist / filter-mode helper |

OSC3 ParamIds (33–35, 38, 87–89) stay in the enum for presets. Analog 4×2 has SQR1 / SQR2 / Sub only — OSC3 level dest is a no-op on Mainboard. Dist 52–53 is stubbed unless that analog exists.

**ParamId space:** `params_def.h` is one canonical superset, byte-identical across all seven live board copies of both projects; the master lives at `DCO3-MONOSYNTH/DCO/params_def.h` — edit there and copy out. It is not a per-board fork: each board routes a subset, and existing ids are never renumbered. `serial_input_protocol.h` shares its command values and payload lengths the same way, but each board's copy is trimmed to the commands that board parses or sends.

---

## Inter-board links (classic DCO4 PCB)

```mermaid
flowchart LR
  MIDI["MIDI USB+DIN"] --> DCO["DCO RP2040/2350"]
  DCO -->|"Serial2 GP20/21 2.5M"| MB["STM32 Mainboard"]
  Input["Input"] -->|"Serial2 GP4/5"| MB
  Input -->|"Serial1 GP0/1"| Screen["Screen"]
  MB -->|"Serial1 PA9/PA10"| Screen
  MB -->|"Serial8 PE0/PE1"| Input
  MB --> Analog["4x VCA + 4x VCF + reso\nMCP4728 + 74HC595"]
```

| Link | Baud | Peers | Role |
|------|------|-------|------|
| DCO `Serial1` | 31250 | DIN MIDI | MIDI in |
| DCO `Serial2` GP20/21 | 2.5M | Mainboard `Serial2` PD5/PD6 | `'n'`/`'o'`/`'e'`/`'x'`/`'p'`/`'O'`/`'L'` DCO→MB; `'m'`/`'p'`/`'t'` + relayed `'a'`–`'d'`/`'q'`/`'N'` MB→DCO |
| Input `Serial2` GP4/5 | 2.5M | Mainboard `Serial8` PE0/PE1 | slim `'a'`–`'d'`/`'p'`/`'q'`/`'N'` Input→MB; `'x'`/`'p'`/`'O'`/`'L'` MB→Input |
| Input `Serial1` GP0 | 2.5M | Screen `Serial1` GP13 | UI frames + relayed gap 154 |
| Mainboard `Serial1` | 2.5M | Screen (optional second feed) | unused if Input already mirrors UI |

Protocol is **slim little-endian**, no finish byte. PW = `'p'` 210, ADSR1→VCA = `'p'` 222. Do not restore BE `'p'` or Input `'e'`/`'f'` blocks.

Gap 154 / cal 155: DCO `'x'` → Mainboard → Input → Screen (154 only on Screen).

**The Mainboard is a relay, not a bus.** Nothing crosses it implicitly. A pass-through
command byte (`'a'`–`'d'`, `'q'`, `'N'`, `'O'`, `'L'`, `'x'`) only reaches the far side if
it has a row in `inputSerial8Commands[]` (Input→DCO) or `mainSerial2Commands[]`
(DCO→Input) in
[`../../MAINBOARD-CONTROLLER/Serial.ino`](../../MAINBOARD-CONTROLLER/Serial.ino); an
unregistered byte is dropped in transit, silently, on both ends. `'p'` is not forwarded
blindly: the Mainboard applies each ParamId through its own `paramTable[]` and re-emits
the DCO-owned ones with `forward_dco()`, so a new DCO ParamId needs an applier row there
too.

---

## Preset authority

The DCO owns the only preset storage in the instrument: 256 LittleFS slots
([`PRESET_STORE.md`](PRESET_STORE.md)). Input has no filesystem; it caches the 256 slot
names in RAM and asks the DCO to refill that cache with `'N'`, receives 256 `'O'`
`[slot][name:16]` entries, and learns the current slot from `'L'` `[slot]` after every
successful load — including loads it did not trigger (boot recall, MIDI program change,
USB `dco_control`).

```mermaid
flowchart LR
  Input["Input (RAM name cache)"] -->|"'N' request, 'q' name, 'p' 170/171 save/load"| MB["Mainboard"]
  MB -->|"relay / forward_dco"| DCO["DCO (LittleFS 256 slots)"]
  DCO -->|"256x 'O' entries, 'L' loaded slot"| MB
  MB -->|relay| Input
  MIDI["MIDI PC + Bank Select"] --> DCO
  USB["USB dco_control"] --> DCO
```

Panel edits of envelope/filter blocks (`'a'`–`'d'`) and the preset name (`'q'`) are
applied on the Mainboard **and** forwarded to the DCO, because the DCO builds each
record from its own copies of those values and has no direct link to the panel.

---

## Feature flags (`DCO.ino`)

| Flag | Default | Role |
|------|---------|------|
| `ENABLE_MAINBOARD_LINK` | on | Serial2 = Mainboard peer |
| `ENABLE_MB_MOD_STREAM` | on | Consume `'m'`; skip local LFO1/2 + EnvDCO clocks |
| `ENABLE_CV_OUTS` / `WAVE_MUX` | off | Analog writers stay compiled-out on this board |
| `ENABLE_USB_CONTROL` | on | USB CDC Input-style frames for bench |

Pin map: [`PINOUT.md`](PINOUT.md). Mod matrix (DCO depth apply): [`MOD_MATRIX.md`](MOD_MATRIX.md).
