# Mainboard reintegration (DCO pointer)

STM32 Mainboard is back in the live UART graph on **classic DCO4 PCB wiring**. This DCO board owns MIDI + PIO pitch **and local LFO1/2 + envelopes + matrix→pitch**. Analog VCA/VCF CVs still clock on Mainboard.

**Canonical write-up:** [`../../MAINBOARD-CONTROLLER/docs/MAINBOARD_REINTEGRATION.md`](../../MAINBOARD-CONTROLLER/docs/MAINBOARD_REINTEGRATION.md)

DCO `Serial2` (GP20/21 @ 2.5 M) peers with Mainboard `Serial2`, not Input.

| DCO TX | DCO RX |
|--------|--------|
| `'n'`/`'o'` note edges (raw MIDI note + flags) | slim `'p'` (DCO-owned ParamIds) |
| `'e'` AT / MW / pitch bend | `'m'` LFO1/2 Q15 + EnvDCO Q15×4 + matrix pitch Q24 |
| `'x'` gap 154 / cal 155 | `'t'` bench ASCII chunk `[n][n bytes ≤15]` |
| USB/MIDI `'a'`/`'b'`/`'d'` analog mirror | relayed panel `'a'`–`'d'` + `'q'` (preset record shadow) |
| `'O'` 256 preset directory entries, `'L'` loaded slot | relayed `'N'` preset directory request |

**Default:** Core0 runs `LFO1()` / `LFO2()`, `ADSR_update` (EnvDCO/VCA/VCF), and local `mod_matrix_eval_pitch_q24`. Pitch-drift LFOs + Character stay here. `'m'` is still parsed but ignored for pitch mailboxes unless `ENABLE_MB_MOD_STREAM` is defined (opt-in cutover). Panel `'a'`–`'d'` go Input → Mainboard; USB/MIDI `'a'`/`'b'`/`'d'` are mirrored DCO → Mainboard so analog VCA/VCF CVs move. `'c'` EnvDCO stays DCO-local.

**Preset traffic is relayed, not terminated.** The DCO owns the instrument's 256 preset slots, so the Mainboard also applies *and* forwards Input's `'a'`–`'d'`/`'q'` (otherwise a saved record would capture stale envelope, filter and name values) and passes `'N'`/`'O'`/`'L'` straight through. Each of those command bytes needs its own row in `inputSerial8Commands[]` / `mainSerial2Commands[]` or it is dropped in transit. See [`PRESET_STORE.md`](PRESET_STORE.md).
