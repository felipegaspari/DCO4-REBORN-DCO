# Post-filter distortion (Drive + Mix)

**Status:** hardware idea locked for breadboard / PCB; firmware prototype exposes the two CVs.

Related: [`PINOUT.md`](PINOUT.md), [`MAINBOARD_ABSORPTION.md`](MAINBOARD_ABSORPTION.md). Soft/PWM writers live behind `ENABLE_CV_OUTS` like the other panel CVs.

---

## Signal chain

```text
Osc / sub mix (level VCAs — may run hot into the LP)
  → Filter 1 LOWPASS
  → Distortion (Drive + Mix only)
  → Filter 2 HIGHPASS
  → Main Amp VCA
  → FV-1 (later)
  → outs
```

Dry for Mix is taken at **post-LP / pre-dist** (same node that feeds the Drive VCA). No symmetry, tone, or pre/post switch in v1.

```text
LP out ──┬── dry ──────────────────────────────┐
         │                                     │
         ▼                                     │
      Drive VCA ◄── CV Drive (0..~3 V)         │
         │                                     │
         ▼                                     │
      Presence shelf (fixed RC, slight HF boost)
         │                                     │
         ▼                                     │
      Asymmetric soft clip (unequal diodes / LED)
         │                                     │
         ▼                                     │
      Light fold stage (mainly at high Drive)
         │                                     │
         ▼                                     │
      AC couple + mild LPF (kill DC / ultrasonic)
         │                                     │
         ▼                                     │
      Wet ──► Mix crossfade VCAs ◄── CV Mix ──► sum ──► HP in
              (dry rises as wet falls)
```

---

## Detailed circuit

One audio input (`LP_OUT`), one audio output (`DIST_OUT` → HP). Two control voltages from the Pico (PWM+RC or DAC): **Drive** and **Mix**, roughly 0…3 V into the VCA CVs (exact scaling depends on 2164-style expo CV vs linear OTA).

### 1. Split dry / wet

`LP_OUT` feeds a short bus:

1. **Dry path** — unity op-amp follower so the Mix stage does not load the LP.
2. **Wet path** — into the Drive VCA.

Keep both taps AC-coupled from the LP if the LP output sits on a DC bias; otherwise one coupling cap at `LP_OUT` is enough.

### 2. Drive VCA

**Job:** set how hard the nonlinear core is hit. At Drive = 0 the wet path should be quiet enough that Mix ≈ dry.

**Part:** one channel of **SSI2164 / AS2164 / V2164** (same family as many synth VCAs), or an OTA (e.g. LM13700 half) as a VCA.

- Audio in: post-LP (after the dry tap)
- CV: **Drive** (after RC filter / buffer from PWM)
- Gain law: expo (2164) is fine; trim so mid-Drive is “warm” and max reaches the fold, not an instant brickwall

Optional: a small **fixed gain** after the VCA (op-amp, about ×2…×4) so the clipper is easier to reach without extreme CV.

### 3. Presence shelf (fixed)

**Job:** a little more high-mid into the clipper so grit is brighter and less dull, without a Tone knob.

Use a series **RC shelf** or a mild non-inverting shelf:

- Boost on the order of **+3…+6 dB** above roughly 1–2 kHz
- Fixed resistors/caps only (tune on the bench)

### 4. Asymmetric soft clip

**Job:** first nonlinearity — warm saturation, not a hard square.

**Op-amp** inverting or non-inverting stage with diodes in the feedback or to a bias point, **asymmetric**:

- One polarity: **1N4148** (or two in series)
- Other polarity: **LED** (red/amber) or a different diode stack

Different positive/negative thresholds mix even and odd harmonics; less “dead” than a single matched 4148 pair.

Targets:

- Soft onset (diodes in feedback of a gain stage, or resistor in series with the diodes)
- Low Drive: diodes barely conducting
- High Drive: strong soft limiting **before** the fold stage still has headroom to work

### 5. Light fold (high Drive)

**Job:** extra harmonic bloom at high Drive; almost transparent at low Drive.

One **simple Lockhart-style** (or diode-bridge / current) fold is enough — not a multi-fold Serge monster.

Practical sketch:

- Soft-clipped signal → series resistor → **fold cell** → buffer
- Set the fold threshold **above** soft-clip onset so the sequence is: clean → soft clip → fold

If the fold is too aggressive, pad with a series R or voltage divider before the fold cell.

### 6. AC couple + mild LPF

**Job:** remove DC from asymmetric clip/fold, and ultrasonic hash before the Mix VCAs and HP.

- Series **coupling cap** (e.g. 1 µF film)
- Mild 1-pole (or 2-pole) LPF around **15–25 kHz** (RC or simple Sallen–Key)

Output of this stage is **WET**.

### 7. Mix crossfade → HP

**Job:** blend dry and wet with one CV.

Preferred: **two VCA sections** (two more 2164 channels, or a dual VCA):

| Path | CV |
|------|-----|
| Dry VCA | ∝ (1 − Mix) |
| Wet VCA | ∝ Mix |

Generate the complementary CV in analog (inverter from Mix around 0…Vref). For v1, **one Mix CV + analog inverter** is enough.

Sum dry and wet in a **passive mix into an op-amp summer** (equal resistors), buffer → **`DIST_OUT` → HP filter input**.

| Mix | Result |
|-----|--------|
| 0 | Dry only (distortion effectively out) |
| Mid | Parallel grit |
| Max | Full wet |

### CV conditioning (from Pico)

Same pattern as cutoff / main VCA:

```text
GP9  PWM ── RC (e.g. 10k + 100n…1µ) ──► optional buffer ──► Drive VCA CV
GP26 PWM ── RC ──► buffer ──► Mix (+ inverter for dry CV)
```

Scale/offset so:

- Drive = 0 → wet path near mute / below diode conduction
- Drive = max → into fold
- Mix = 0 → dry open, wet muted

Manual calibration in firmware parks both CVs at 0.

### Parts budget (typical)

| Qty | Role |
|-----|------|
| 1× 2164 (or similar) | Drive + dry Mix + wet Mix (3 of 4 sections; 4th spare) |
| 2–3× op-amps (TL074 / better audio) | Dry buffer, clip stage, summer, CV buffers |
| Diodes + 1 LED | Asymmetric soft clip |
| Small fold network | Transistors or diodes |
| R’s and C’s | Presence, coupling, LPF, CV filters |

### Not in v1

- No symmetry CV, tone pot, or pre/post filter switch (chain is fixed: LP → dist → HP)
- No digital audio path — only two analog CVs

### Bench tune order

1. Mix = 0 — confirm unity dry, silent wet.
2. Mix = max, raise Drive slowly — soft-clip character.
3. Drive to top — fold appears without exploding level.
4. Mix mid — parallel blend feels musical.
5. Sweep LP resonance into it — peaks should push the clipper; HP after cleans mud.

---

## Digital control

| Param | ID | Range | Default | Meaning |
|-------|---:|------:|--------:|---------|
| Drive | `PARAM_DIST_DRIVE` (52) | 0..4095 | 0 | Drive VCA CV (0 = no push into the clipper) |
| Mix | `PARAM_DIST_MIX` (53) | 0..4095 | 0 | 0 = dry, 4095 = full wet |

Prototype CV outs (PWM + RC, same domain as cutoff/VCA):

| CV | Provisional GPIO | PWM notes |
|----|------------------|-----------|
| Drive | **GP9** | Slice 4 B, wrap `DIV_COUNTER_CV` (4095) |
| Mix | **GP26** | Slice 5 A — shares slice with main VCA (GP11 B); both CV, wrap OK |

Pins are provisional pending the final RP2350B map. Do not place these on a slice that must keep `DIV_COUNTER` / `DIV_COUNTER_PW` wraps (RANGE / PW).

MIDI CC: **81** = Drive, **82** = Mix (see [`MIDI_CC_MAP.md`](MIDI_CC_MAP.md)).

---

## Gain staging

- **Osc level VCAs** above unity: grit / saturation **into the LP** (filter character).
- **Drive**: grit **after** the LP (clip/fold character). Keep enough headroom so the two are distinct tools.
- **Mix**: parallel dry keeps body; wet adds harmonics. Manual calibration parks Drive = 0 and Mix = 0 (full dry).

---

## Software prototype

- Panel / USB / MIDI → `apply_param_dist_*` → `DIST_DRIVE` / `DIST_MIX` in [`cv_state.h`](../cv_state.h).
- With `ENABLE_CV_OUTS`, [`PWM.ino`](../PWM.ino) writes the two GPIOs from those values.
- Bench UI: Filter tab sliders in [`tools/dco_control/`](../tools/dco_control/).
