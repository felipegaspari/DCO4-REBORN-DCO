# Modulation matrix (v2)

Sparse control-rate mod matrix for DCO3. Panel bases stay on ParamIds / `'d'` blocks; each tick sums active slots onto hardware CVs.

**Never a matrix destination:** main **VCA** — fixed EnvVCA + LFO1 + velocity bus in [`cv_out.ino`](../cv_out.ino).

**Dual-bus policy:** LFO1 and LFO2 remain on their fixed depth params (`PARAM_LFO1_TO_DCO`, `PARAM_LFO2_TO_PW`, etc.) *and* are available as matrix sources (IDs 8–9). Matrix routing is independent.

Related: [`DUAL_MCU.md`](DUAL_MCU.md), [`PINOUT.md`](PINOUT.md).

---

## Model

```text
slot: source × dest × depth
contribution = source_norm * depth
hw[d] = clamp(panel_base[d] ± sum)   // − for level attenuators, + for reso / cutoff / dist
```

Eight slots (`MOD_SLOT_COUNT`). Empty when `source` or `dest` is `0xFF` (or out-of-range ParamId value).

Depth is bipolar `int16` (typically ±4095 for full-scale swing).

---

## Sources

| ID | Name | Norm | Notes |
|----|------|------|-------|
| 0 | ADSR3 | `0..1` | EnvDCO (`ADSR1Level[0] / 4095`) |
| 1 | ADSR4 | `0..1` | Stub `0` until engine exists |
| 2 | LFO3 | `-1..1` | Stub `0` (`PARAM_LFO3_*` reserved) |
| 3 | LFO4 | `-1..1` | Stub `0` |
| 4 | Velocity | `0..1` | `midi_velocity[0] / 127` |
| 5 | Keytrack | `-1..1` | Note vs pivot 60, independent of `VCFKeytrack` |
| 6 | Random | `-1..1` | DCO: S&H on note-on; aux: ~5 Hz free-run |
| 7 | Aftertouch | `0..1` | Channel AT / 127 |
| 8 | LFO1 | `-1..1` | `LFO1Level / LFO1_CC_HALF`; fixed `LFO1toDCO` / `LFO1toVCA` unchanged |
| 9 | LFO2 | `-1..1` | `LFO2Level / LFO2_CC_HALF`; fixed detune/PW/VCF depths unchanged |
| 10 | Pitch bend | `-1..1` | `(midi_pitch_bend − 8192) / 8192`; fixed pitch path in voice engine unchanged |
| 11 | Mod wheel | `0..1` | MIDI CC 1 / 127 |

EnvVCA (ADSR1) and EnvVCF (ADSR2) stay on fixed buses only.

---

## Destinations

| ID | Name | Owner | Sign |
|----|------|-------|------|
| 0 | OSC1 level | RP2350 | Subtract from attenuator base (positive depth → louder) |
| 1 | OSC2 level | RP2350 | same |
| 2 | OSC3 level | RP2350 | same |
| 3 | Sub level | RP2350 | same |
| 4 | VCF1 resonance | RP2350 | Add to panel `RESONANCE` → `RESONANCE_PWM[0]` |
| 5 | VCF2 resonance | RP2350 | → `RESONANCE_PWM[1]` |
| 6 | Dist Drive | RP2040 aux / solo DCO | Add to panel `DIST_DRIVE` |
| 7 | VCF cutoff | RP2350 | Add to shared `CUTOFF` sum → both filter cutoff paths |
| 8 | Dist Mix | RP2040 aux / solo DCO | Add to panel `DIST_MIX` |

---

## ParamIds (60–83)

Per slot `i` (0..7):

| Field | ParamId |
|-------|---------|
| source | `60 + 3*i` |
| dest | `61 + 3*i` |
| depth | `62 + 3*i` |

Mirrored in DCO / Input / Screen / VOICE-AUX `params_def.h`. Names: `PARAM_MOD_SLOT0_SOURCE` … `PARAM_MOD_SLOT7_DEPTH`.

---

## Runtime

- **DCO:** [`mod_matrix.ino`](../mod_matrix.ino) — `mod_matrix_accumulate()` then `mod_matrix_apply_cv()` from `update_CV_outs()` (~10 kHz with ADSR). Cutoff sum applied before VCF PWM math. Skipped under manual calibration.
- **VOICE-AUX:** same ParamIds; dest 6 (Dist Drive) and dest 8 (Dist Mix) in `mod_matrix_apply_dist()` each `loop()`.
- Level panel applies update **bases only**; PWM is written after the matrix sum.

---

## Code map

| File | Role |
|------|------|
| [`mod_matrix.h`](../mod_matrix.h) / [`mod_matrix.ino`](../mod_matrix.ino) | Slots, sources, sum, apply |
| [`cv_out.ino`](../cv_out.ino) | Accumulate cutoff; VCA untouched |
| [`PWM.ino`](../PWM.ino) | `write_level_pwm_raw`, per-filter `RESONANCE_PWM[]` |
| [`midi.ino`](../midi.ino) | CC 1 → mod wheel; note-on random; aftertouch |
| [`VOICE-AUX/mod_matrix.*`](../../VOICE-AUX/mod_matrix.h) | Dist Drive + Dist Mix re-sum |
