# Modulation matrix (v2)

Sparse control-rate mod matrix for DCO3. Panel bases stay on ParamIds / `'d'` blocks; each tick sums active slots onto hardware CVs.

**Never a matrix destination:** main **VCA** — fixed EnvVCA + LFO1 + velocity bus in [`cv_out.ino`](../cv_out.ino).

**Dual-bus policy:** LFO1 and LFO2 remain on their fixed depth params (`PARAM_LFO1_TO_DCO`, `PARAM_LFO2_TO_PW`, etc.) *and* are available as matrix sources (IDs 8–9). Matrix routing is independent.

Related: [`DUAL_MCU.md`](DUAL_MCU.md), [`PINOUT.md`](PINOUT.md).

---

## Model

```text
slot: source × dest × depth
contribution = (src_q15 * depth) >> 15    // src_q15: ±32768 ≈ ±1.0
hw[d] = clamp(panel_base[d] ± sum)       // − for level attenuators, + for reso / cutoff / dist
```

Eight slots (`MOD_SLOT_COUNT`). Empty when `source` or `dest` is `0xFF` (or out-of-range ParamId value).

Depth is bipolar `int16` (typically ±4095 for full-scale swing). Hot path is **integer Q15** on both RP2040 and RP2350 (no float normalize per slot). Noise sources pass through `noiseLevel[]` (already Q15).

---

## Sources

| ID | Name | Q15 norm | Notes |
|----|------|----------|-------|
| 0 | ADSR3 | `0..32768` | EnvDCO `ADSR1Level_q15[0]` (library Q15 tap) |
| 1 | ADSR4 | `0` | Stub until engine exists |
| 2 | LFO3 | `0` | Stub (`PARAM_LFO3_*` reserved) |
| 3 | LFO4 | `0` | Stub |
| 4 | Velocity | `0..32766` | `midi_velocity[0] * 258` |
| 5 | Keytrack | `±32768` | `(note − 60) * 682`; note 0 → 0; independent of `VCFKeytrack` |
| 6 | Random | `±32000` | DCO: S&H Q15 on note-on; aux: ~5 Hz free-run Q15 |
| 7 | Aftertouch | `0..32766` | Channel AT `* 258` |
| 8 | LFO1 | `±32768` | `LFO1Level` already Q15 from mo-lfo `getWaveQ15()` |
| 9 | LFO2 | `±32768` | Same |
| 10 | Pitch bend | `±32768` | `(bend − 8192) << 2` |
| 11 | Mod wheel | `0..32766` | MIDI CC 1 `* 258` |
| 12 | Noise 0 | `±32767` | Pass-through `noiseLevel[0]` (already Q15) |
| 13 | Noise 1 | `±32767` | Pass-through `noiseLevel[1]` |
| 14 | Noise 2 | `0` | Reserved — no generator (`NUM_NOISE_GENS == 2`) |
| 15 | Noise 3 | `0` | Reserved — no generator |

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
| 9 | Pitch | RP2350 / DCO voice | Shared OSC1/2/3; Q24 octave-fraction into `modifiersBase` (with pitch bend). **Depth ±1023 → ±1.0 oct** (clamped); dual-bus with `LFO1toDCO` / EnvDCO |

Pitch is latched from `dest_sums[9]` in `update_CV_outs()` → `matrix_pitch_mod_q24`; not applied via `mod_matrix_apply_cv`.

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
