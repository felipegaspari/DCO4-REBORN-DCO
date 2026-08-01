# Modulation matrix (v1)

Sparse control-rate mod matrix for DCO3. Panel bases stay on ParamIds / `'d'` blocks; each tick sums active slots onto hardware CVs.

**Never matrix destinations:** main **VCA** and **VCF1 cutoff** — those keep fixed buses in [`cv_out.ino`](../cv_out.ino).

Related: [`DUAL_MCU.md`](DUAL_MCU.md), [`PINOUT.md`](PINOUT.md).

---

## Model

```text
slot: source × dest × depth
contribution = source_norm * depth
hw[d] = clamp(panel_base[d] ± sum)   // − for level attenuators, + for reso / Dist Drive
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

Legacy ADSR1/2 and LFO1/2 stay on fixed buses only.

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

VCF cutoff S&H is a future fixed path, not a matrix dest.

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

- **DCO:** [`mod_matrix.ino`](../mod_matrix.ino) — `mod_matrix_apply_cv()` from `update_CV_outs()` (~10 kHz with ADSR). Skipped under manual calibration (cal forces absolute level PWMs).
- **VOICE-AUX:** same ParamIds; only dest 6 applied in `mod_matrix_apply_dist()` each `loop()`. Vel/keytrack/AT/ADSR/LFO stubbed until performance broadcast.
- Level panel applies update **bases only**; PWM is written after the matrix sum.

---

## Code map

| File | Role |
|------|------|
| [`mod_matrix.h`](../mod_matrix.h) / [`mod_matrix.ino`](../mod_matrix.ino) | Slots, sources, sum |
| [`cv_out.ino`](../cv_out.ino) | Calls apply; VCA / cutoff untouched |
| [`PWM.ino`](../PWM.ino) | `write_level_pwm_raw`, per-filter `RESONANCE_PWM[]` |
| [`VOICE-AUX/mod_matrix.*`](../../VOICE-AUX/mod_matrix.h) | Dist Drive re-sum |
