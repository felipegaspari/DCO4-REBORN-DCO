# MIDI CC implementation chart

Generated from `tools/dco_control/params.py` by `tools/dco_control/gen_midi_map.py`. Do not edit by hand.

Every control the bench app exposes is reachable from a 7-bit CC on any channel (the DCO listens omni), over USB MIDI or the DIN input. The board does not send anything back, so a panel should push its state after connecting.

A controller value scales into the parameter's native range as

```
value = lo + ((hi - lo) * cc + 63) / 127
```

so CC 0 lands on `CC 0` below and CC 127 on `CC 127`. Envelope attack, decay and release then go through `linearToExponential(value, 50, 25000)`, the same curve the Input board applies to its faders, because the `'a'`-`'c'` block frames carry those values already exp-mapped.

Menu-style parameters use a 0..127 range so the scaling is an identity and a menu entry can pick an exact native value. Switches scale to 0..1, so under 64 is off and 64 or over is on.

## Map

| CC | Control | Group | Target | CC 0 | CC 127 | Curve |
| --- | --- | --- | --- | --- | --- | --- |
| 2 | OSC1 interval (semitones) | Oscillators | `PARAM_OSC1_INTERVAL` | 0 | 127 | linear |
| 3 | OSC2 interval (semitones) | Oscillators | `PARAM_OSC2_INTERVAL` | 0 | 60 | linear |
| 4 | OSC3 interval (semitones) | Oscillators | `PARAM_OSC3_INTERVAL` | 0 | 60 | linear |
| 5 | OSC2 detune | Oscillators | `PARAM_OSC2_DETUNE_VAL` | 0 | 512 | linear |
| 8 | OSC3 detune | Oscillators | `PARAM_OSC3_DETUNE_VAL` | 0 | 512 | linear |
| 9 | SQR1 level | Oscillators | `PARAM_SQR1_LEVEL` | 0 | 127 | linear |
| 12 | SQR2 level | Oscillators | `PARAM_SQR2_LEVEL` | 0 | 127 | linear |
| 13 | Sub level | Oscillators | `PARAM_SUB_LEVEL` | 0 | 127 | linear |
| 14 | SQR1 enable | Oscillators | `PARAM_SQR1_STATUS` | 0 | 1 | linear |
| 15 | SQR2 enable | Oscillators | `PARAM_SQR2_STATUS` | 0 | 1 | linear |
| 16 | Saw enable | Oscillators | `PARAM_SAW_STATUS` | 0 | 1 | linear |
| 17 | Saw2 enable | Oscillators | `PARAM_SAW2_STATUS` | 0 | 1 | linear |
| 18 | Tri enable | Oscillators | `PARAM_TRI_STATUS` | 0 | 1 | linear |
| 19 | Sine enable | Oscillators | `PARAM_SINE_STATUS` | 0 | 1 | linear |
| 20 | Hard sync topology | Sync and PIO | `PARAM_SYNC_MODE` | 0 | 127 | linear |
| 21 | Soft sync | Sync and PIO | `PARAM_SOFT_SYNC` | 0 | 1 | linear |
| 22 | Sub-oscillator divide | Sync and PIO | `PARAM_SUBOSC_DIVIDE` | 0 | 127 | linear |
| 23 | Osc sync / phase align OSC2 | Sync and PIO | `PARAM_OSC_SYNC_MODE` | 0 | 127 | linear |
| 24 | EnvDCO (ADSR3) enabled | Envelopes | `PARAM_ADSR3_ENABLED` | 0 | 1 | linear |
| 25 | ADSR3 to osc select | Envelopes | `PARAM_ADSR3_TO_OSC_SELECT` | 0 | 127 | linear |
| 26 | ADSR3 to OSC1 detune | Envelopes | `PARAM_ADSR3_TO_DETUNE1` | -511 | 511 | linear |
| 27 | ADSR1 attack curve | Envelopes | `PARAM_ADSR1_ATTACK_CURVE` | 0 | 7 | linear |
| 28 | ADSR1 decay curve | Envelopes | `PARAM_ADSR1_DECAY_CURVE` | 0 | 7 | linear |
| 29 | ADSR2 attack curve | Envelopes | `PARAM_ADSR2_ATTACK_CURVE` | 0 | 7 | linear |
| 30 | ADSR2 decay curve | Envelopes | `PARAM_ADSR2_DECAY_CURVE` | 0 | 7 | linear |
| 31 | VCA ADSR restart | Envelopes | `PARAM_VCA_ADSR_RESTART` | 0 | 1 | linear |
| 33 | VCF ADSR restart | Envelopes | `PARAM_VCF_ADSR_RESTART` | 0 | 1 | linear |
| 34 | EnvVCA times: Attack | Envelopes | `CC_LOCAL_ADSR_VCA_ATTACK` | 0 | 25000 | exp |
| 35 | EnvVCA times: Decay | Envelopes | `CC_LOCAL_ADSR_VCA_DECAY` | 0 | 25000 | exp |
| 36 | EnvVCA times: Sustain | Envelopes | `CC_LOCAL_ADSR_VCA_SUSTAIN` | 0 | 4095 | linear |
| 37 | EnvVCA times: Release | Envelopes | `CC_LOCAL_ADSR_VCA_RELEASE` | 0 | 25000 | exp |
| 39 | EnvVCF times: Attack | Envelopes | `CC_LOCAL_ADSR_VCF_ATTACK` | 0 | 25000 | exp |
| 40 | EnvVCF times: Decay | Envelopes | `CC_LOCAL_ADSR_VCF_DECAY` | 0 | 25000 | exp |
| 41 | EnvVCF times: Sustain | Envelopes | `CC_LOCAL_ADSR_VCF_SUSTAIN` | 0 | 4095 | linear |
| 43 | EnvVCF times: Release | Envelopes | `CC_LOCAL_ADSR_VCF_RELEASE` | 0 | 25000 | exp |
| 44 | EnvDCO times (pitch and PW): Attack | Envelopes | `CC_LOCAL_ADSR_DCO_ATTACK` | 0 | 25000 | exp |
| 45 | EnvDCO times (pitch and PW): Decay | Envelopes | `CC_LOCAL_ADSR_DCO_DECAY` | 0 | 25000 | exp |
| 46 | EnvDCO times (pitch and PW): Sustain | Envelopes | `CC_LOCAL_ADSR_DCO_SUSTAIN` | 0 | 4095 | linear |
| 47 | EnvDCO times (pitch and PW): Release | Envelopes | `CC_LOCAL_ADSR_DCO_RELEASE` | 0 | 25000 | exp |
| 48 | EnvVCA to VCA amount: ADSR1 to VCA | Envelopes | `CC_LOCAL_ADSR1_TO_VCA_AMOUNT` | 0 | 512 | linear |
| 49 | VCF keytrack | Filter | `PARAM_VCF_KEYTRACK` | -256 | 255 | linear |
| 50 | Velocity to VCF | Filter | `PARAM_VELOCITY_TO_VCF` | 0 | 20 | linear |
| 51 | Resonance amp compensation | Filter | `PARAM_RESONANCE_COMPENSATION` | 0 | 1 | linear |
| 52 | Filter block: Cutoff | Filter | `CC_LOCAL_FILTER_CUTOFF` | 0 | 4095 | linear |
| 53 | Filter block: Resonance | Filter | `CC_LOCAL_FILTER_RESONANCE` | 0 | 4095 | linear |
| 54 | Filter block: EnvVCF to cutoff | Filter | `CC_LOCAL_FILTER_ADSR2_TO_VCF` | 0 | 512 | linear |
| 55 | Filter block: LFO2 to cutoff | Filter | `CC_LOCAL_FILTER_LFO2_TO_VCF` | 0 | 512 | linear |
| 56 | LFO2 to PW | PWM | `PARAM_LFO2_TO_PW` | 0 | 511 | linear |
| 57 | ADSR3 to PWM | PWM | `PARAM_ADSR3_TO_PWM` | 0 | 1023 | linear |
| 58 | PWM pots manual | PWM | `PARAM_PWM_POTS_CONTROL_MANUAL` | 0 | 1 | linear |
| 59 | Pulse width: PW | PWM | `CC_LOCAL_PW_PW` | 0 | 4095 | linear |
| 60 | LFO1 waveform | LFOs | `PARAM_LFO1_WAVEFORM` | 0 | 127 | linear |
| 61 | LFO2 waveform | LFOs | `PARAM_LFO2_WAVEFORM` | 0 | 127 | linear |
| 62 | LFO1 speed | LFOs | `PARAM_LFO1_SPEED` | 0 | 4095 | linear |
| 63 | LFO2 speed | LFOs | `PARAM_LFO2_SPEED` | 0 | 4095 | linear |
| 65 | LFO1 to DCO | LFOs | `PARAM_LFO1_TO_DCO` | 0 | 511 | linear |
| 66 | LFO1 to VCA | LFOs | `PARAM_LFO1_TO_VCA` | 0 | 1023 | linear |
| 67 | LFO2 to OSC2 detune | LFOs | `PARAM_LFO2_TO_DETUNE2` | 0 | 255 | linear |
| 68 | LFO2 to OSC3 detune | LFOs | `PARAM_LFO2_TO_DETUNE3` | 0 | 255 | linear |
| 69 | Voice mode | Voice and Drift | `PARAM_VOICE_MODE` | 0 | 127 | linear |
| 70 | Unison detune | Voice and Drift | `PARAM_UNISON_DETUNE` | 0 | 127 | linear |
| 71 | Portamento time | Voice and Drift | `PARAM_PORTAMENTO_TIME` | 0 | 255 | linear |
| 72 | Portamento mode | Voice and Drift | `PARAM_PORTAMENTO_MODE` | 0 | 127 | linear |
| 73 | Analog drift amount | Voice and Drift | `PARAM_ANALOG_DRIFT_AMOUNT` | 0 | 127 | linear |
| 74 | Analog drift speed | Voice and Drift | `PARAM_ANALOG_DRIFT_SPEED` | 1 | 255 | linear |
| 75 | Analog drift spread | Voice and Drift | `PARAM_ANALOG_DRIFT_SPREAD` | 1 | 127 | linear |
| 76 | VCA level | Voice and Drift | `PARAM_VCA_LEVEL` | 0 | 128 | linear |
| 77 | Velocity to VCA | Voice and Drift | `PARAM_VELOCITY_TO_VCA` | 0 | 20 | linear |
| 78 | Manual calibration mode | Calibration | `PARAM_MANUAL_CALIBRATION_FLAG` | 0 | 1 | linear |
| 79 | Manual cal stage (osc) | Calibration | `PARAM_MANUAL_CALIBRATION_STAGE` | 0 | 2 | linear |
| 80 | Manual cal offset | Calibration | `PARAM_MANUAL_CALIBRATION_OFFSET` | -20 | 20 | linear |

## Menu values

These parameters take discrete values; the CC number to send is the value itself.

- **CC 2, OSC1 interval (semitones)**: +0 = 0, +12 = 12, +24 = 24, +36 = 36, +48 = 48, +60 = 60, +72 = 72
- **CC 20, Hard sync topology**: 0 - all free running = 0, 1 - OSC2 masters OSC1 = 1, 2 - OSC1 masters OSC2 = 2
- **CC 22, Sub-oscillator divide**: Off = 0, Divide by 2 = 2, Divide by 4 = 4
- **CC 23, Osc sync / phase align OSC2**: Off - free running (no note-on sync) = 0, Sync at note-on (0 deg) = 1, Sync + 30 deg = 15, Sync + 45 deg = 2, Sync + 60 deg = 30, Sync + 90 deg = 3, Sync + 120 deg = 60, Sync + 135 deg = 4, Sync + 150 deg = 75, Sync + 180 deg = 5, Sync + 210 deg = 105, Sync + 225 deg = 6, Sync + 240 deg = 120, Sync + 270 deg = 7, Sync + 315 deg = 8
  - out of 7-bit reach, use the serial bench app instead: Sync + 300 deg (150), Sync + 330 deg (165)
- **CC 25, ADSR3 to osc select**: 0 - OSC1 = 0, 1 - OSC2 = 1, 2 - OSC1+2 = 2, 3 - OSC3 = 3, 4 - all = 4
- **CC 60, LFO1 waveform**: 0 - off = 0, 1 - saw = 1, 2 - triangle = 2, 3 - sine = 3, 4 - square = 4
- **CC 61, LFO2 waveform**: 0 - off = 0, 1 - saw = 1, 2 - triangle = 2, 3 - sine = 3, 4 - square = 4
- **CC 69, Voice mode**: 0 - mono = 0, 1 - poly = 1, 2 - stack = 2
- **CC 72, Portamento mode**: 0 - fixed time = 0, 1 - slew rate = 1

## Deliberately not mapped

- **Run autotune** (parameter 150)
- **Store manual cal offsets** (parameter 156)

Autotune takes the board over for about a minute and the store writes the filesystem, so neither should be one stray controller away. Both are still available from the serial bench app in `tools/dco_control`.

Reserved controllers left untouched: 0, 1, 6, 7, 10, 11, 32, 38, 42, 64, 98, 99, 100, 101, 120, 121, 122, 123, 124, 125, 126, 127. CC 42 keeps its historical meaning here, pitch-bend range in semitones. 98-101 stay free so a later NRPN upgrade needs no reshuffling.
