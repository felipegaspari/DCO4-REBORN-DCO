# LFO (Q15 bus and depth scales)

DCO is the sole LFO clock after Mainboard absorption. Live waves are **full-scale bipolar Q15**; musical travel is set by **depth scales in `LFO.h`**, baked at param time into Q24 depths.

Code: [`LFO.h`](../LFO.h), [`LFO.ino`](../LFO.ino), bakers in [`params.ino`](../params.ino). Library: repo-root [`mo-lfo/`](../../mo-lfo/) (vendored under `_build_libs/mo-lfo`). CV LFO→VCA/VCF scales: [`CV_MOD_SCALES.md`](CV_MOD_SCALES.md).

---

## Depth scales (tune in `LFO.h`)

Hot path:

```text
lfo*_pitch_mod_q24[slot] = applyDepthQ24(level_q15, depth_q24)
                         = (level_q15 * depth_q24) >> 15
```

Bake (params):

```text
depth_q24 = lfo_pitch_depth_q24(amt, SCALE)
amt       = expConverterFloat(panel) / 275000   // octave-fraction unit
```

Larger `SCALE` → deeper mod at the same panel setting.

| Symbol | Value | Role |
|--------|------:|------|
| `LFO1_PITCH_DEPTH_SCALE` | 1700 | LFO1→pitch (`LFO1toDCO` + per-osc); also LFO2 coarse |
| `LFO2_PITCH_DEPTH_SCALE` | 512 | LFO2→fine pitch (OSC2 / OSC3) |
| `DRIFT_PITCH_DEPTH_SCALE` | 1000 | Analog drift→pitch (with unit below) |
| `DRIFT_PITCH_UNIT_Q24` | ~8 | Base octave unit for drift |
| `lfo_pitch_depth_q24(amt, scale)` | helper | `amt * scale * 2^24` → Q24 depth |

Drift runtime scale:

```text
drift_pitch_scale_q24 = analogDrift * DRIFT_PITCH_UNIT_Q24 * DRIFT_PITCH_DEPTH_SCALE
```

LFO1 is deeper than LFO2 fine at the same panel curve by design (1700 vs 512). CV LFO→VCA/VCF uses a separate `/1024` domain — see [`CV_MOD_SCALES.md`](CV_MOD_SCALES.md).

---

## Live bus

| Global | Source | Domain |
|--------|--------|--------|
| `LFO1Level` | `LFO1_class.getWaveQ15` | ±32767 (`MO_LFO_Q15_ONE`) |
| `LFO2Level` | `LFO2_class.getWaveQ15` | same |
| `LFO_DRIFT_LEVEL[i]` | `-LFO_DRIFT_CLASS[i].getWaveQ15` | same (negated for Mainboard polarity) |

Init uses `setAmplQ15(MO_LFO_Q15_ONE)` only — not `setAmpl` / `getWave`.

Ctor `dacSize` is unused on the Q15 path (`LFO_DAC_SIZE_UNUSED = 1`). Changing it must not change audio; changing the depth scales above **does**.

---

## Other sinks

| Sink | Format | Notes |
|------|--------|-------|
| LFO → VCA/VCF | `/1024` on Q15 | [`CV_MOD_SCALES.md`](CV_MOD_SCALES.md) |
| Drift → VCF | `vcf_drift_scale_q15 = analogDrift` | Soft CV |
| LFO2 → PW | `(q15 * LFO2toPW) >> 15` | Full ±1.0 → `LFO2toPW` counts |
| Mod matrix | Q15 pass-through | [`MOD_MATRIX.md`](MOD_MATRIX.md) |

---

## Call sites

| Function | Core | Role |
|----------|------|------|
| `init_LFOs` / `init_DRIFT_LFOs` | 0 boot | Waveform, full-scale Q15 amp, initial Hz |
| `LFO1` / `LFO2` / `DRIFT_LFOs` | 0 ~100 µs | Refresh levels + pitch mods |
| `apply_param_lfo*` | param table | Speeds, waveforms, `*_q24` depth bake |
| `update_CV_outs` / voices / matrix | 1 | Consume Q15 levels |

---

## Related

- [`CV_MOD_SCALES.md`](CV_MOD_SCALES.md) — LFO1→VCA / LFO2→VCF scale bake
- [`MOD_MATRIX.md`](MOD_MATRIX.md) — LFO1/LFO2 as Q15 sources
- [`UPDATE_CV_OUTS_HOT_PATH.md`](UPDATE_CV_OUTS_HOT_PATH.md) — live CV combine
