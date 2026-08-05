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
| 2 | Octave shift (semitones) | Oscillators | `PARAM_OSC1_INTERVAL` | 0 | 127 | linear |
| 3 | OSC2 interval (semitones) | Oscillators | `PARAM_OSC2_INTERVAL` | 0 | 60 | linear |
| 4 | OSC3 interval (semitones) | Oscillators | `PARAM_OSC3_INTERVAL` | 0 | 60 | linear |
| 5 | OSC2 detune | Oscillators | `PARAM_OSC2_DETUNE_VAL` | 0 | 512 | linear |
| 8 | OSC3 detune | Oscillators | `PARAM_OSC3_DETUNE_VAL` | 0 | 512 | linear |
| 9 | OSC1 level | Oscillators | `PARAM_OSC1_LEVEL` | 0 | 127 | linear |
| 12 | OSC2 level | Oscillators | `PARAM_OSC2_LEVEL` | 0 | 127 | linear |
| 83 | OSC3 level | Oscillators | `PARAM_OSC3_LEVEL` | 0 | 127 | linear |
| 13 | Sub level | Oscillators | `PARAM_SUB_LEVEL` | 0 | 127 | linear |
| 16 | OSC1 Saw enable | Oscillators | `PARAM_OSC1_SAW_ENABLE` | 0 | 1 | linear |
| 17 | OSC1 Pulse enable | Oscillators | `PARAM_OSC1_PULSE_ENABLE` | 0 | 1 | linear |
| 18 | OSC1 Tri enable | Oscillators | `PARAM_OSC1_TRI_ENABLE` | 0 | 1 | linear |
| 112 | OSC2 Saw enable | Oscillators | `PARAM_OSC2_SAW_ENABLE` | 0 | 1 | linear |
| 113 | OSC2 Pulse enable | Oscillators | `PARAM_OSC2_PULSE_ENABLE` | 0 | 1 | linear |
| 114 | OSC2 Tri enable | Oscillators | `PARAM_OSC2_TRI_ENABLE` | 0 | 1 | linear |
| 115 | OSC3 Saw enable | Oscillators | `PARAM_OSC3_SAW_ENABLE` | 0 | 1 | linear |
| 116 | OSC3 Pulse enable | Oscillators | `PARAM_OSC3_PULSE_ENABLE` | 0 | 1 | linear |
| 117 | OSC3 Tri enable | Oscillators | `PARAM_OSC3_TRI_ENABLE` | 0 | 1 | linear |
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
| 81 | Distortion drive | Filter | `PARAM_DIST_DRIVE` | 0 | 4095 | linear |
| 82 | Distortion mix | Filter | `PARAM_DIST_MIX` | 0 | 4095 | linear |
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
| 84 | Mod slot 0 source | Mod matrix | `PARAM_MOD_SLOT0_SOURCE` | 0 | 127 | linear |
| 85 | Mod slot 0 dest | Mod matrix | `PARAM_MOD_SLOT0_DEST` | 0 | 127 | linear |
| 86 | Mod slot 0 depth | Mod matrix | `PARAM_MOD_SLOT0_DEPTH` | -4095 | 4095 | linear |
| 87 | Mod slot 1 source | Mod matrix | `PARAM_MOD_SLOT1_SOURCE` | 0 | 127 | linear |
| 88 | Mod slot 1 dest | Mod matrix | `PARAM_MOD_SLOT1_DEST` | 0 | 127 | linear |
| 89 | Mod slot 1 depth | Mod matrix | `PARAM_MOD_SLOT1_DEPTH` | -4095 | 4095 | linear |
| 90 | Mod slot 2 source | Mod matrix | `PARAM_MOD_SLOT2_SOURCE` | 0 | 127 | linear |
| 91 | Mod slot 2 dest | Mod matrix | `PARAM_MOD_SLOT2_DEST` | 0 | 127 | linear |
| 92 | Mod slot 2 depth | Mod matrix | `PARAM_MOD_SLOT2_DEPTH` | -4095 | 4095 | linear |
| 93 | Mod slot 3 source | Mod matrix | `PARAM_MOD_SLOT3_SOURCE` | 0 | 127 | linear |
| 94 | Mod slot 3 dest | Mod matrix | `PARAM_MOD_SLOT3_DEST` | 0 | 127 | linear |
| 95 | Mod slot 3 depth | Mod matrix | `PARAM_MOD_SLOT3_DEPTH` | -4095 | 4095 | linear |
| 96 | Mod slot 4 source | Mod matrix | `PARAM_MOD_SLOT4_SOURCE` | 0 | 127 | linear |
| 97 | Mod slot 4 dest | Mod matrix | `PARAM_MOD_SLOT4_DEST` | 0 | 127 | linear |
| 102 | Mod slot 4 depth | Mod matrix | `PARAM_MOD_SLOT4_DEPTH` | -4095 | 4095 | linear |
| 103 | Mod slot 5 source | Mod matrix | `PARAM_MOD_SLOT5_SOURCE` | 0 | 127 | linear |
| 104 | Mod slot 5 dest | Mod matrix | `PARAM_MOD_SLOT5_DEST` | 0 | 127 | linear |
| 105 | Mod slot 5 depth | Mod matrix | `PARAM_MOD_SLOT5_DEPTH` | -4095 | 4095 | linear |
| 106 | Mod slot 6 source | Mod matrix | `PARAM_MOD_SLOT6_SOURCE` | 0 | 127 | linear |
| 107 | Mod slot 6 dest | Mod matrix | `PARAM_MOD_SLOT6_DEST` | 0 | 127 | linear |
| 108 | Mod slot 6 depth | Mod matrix | `PARAM_MOD_SLOT6_DEPTH` | -4095 | 4095 | linear |
| 109 | Mod slot 7 source | Mod matrix | `PARAM_MOD_SLOT7_SOURCE` | 0 | 127 | linear |
| 110 | Mod slot 7 dest | Mod matrix | `PARAM_MOD_SLOT7_DEST` | 0 | 127 | linear |
| 111 | Mod slot 7 depth | Mod matrix | `PARAM_MOD_SLOT7_DEPTH` | -4095 | 4095 | linear |
| 78 | Manual calibration mode | Calibration | `PARAM_MANUAL_CALIBRATION_FLAG` | 0 | 1 | linear |
| 79 | Manual cal stage (osc) | Calibration | `PARAM_MANUAL_CALIBRATION_STAGE` | 0 | 2 | linear |
| 80 | Manual cal offset | Calibration | `PARAM_MANUAL_CALIBRATION_OFFSET` | -20 | 20 | linear |

## Menu values

These parameters take discrete values; the CC number to send is the value itself.

- **CC 2, Octave shift (semitones)** (`PARAM_OSC1_INTERVAL` / `octave_shift`): wire = midi bias; `table_index = midi - 36 + value` (36 ⇒ unison). Labels as octaves: −3 = 0, −2 = 12, −1 = 24 (firmware default), 0 = 36, +1 = 48, +2 = 60, +3 = 72. OSC2/OSC3 intervals use the same bias (36 ⇒ unison with OSC1 in mono); firmware/UI defaults are 36.
- **CC 20, Hard sync topology**: 0 - all free running = 0, 1 - OSC2 masters OSC1 = 1, 2 - OSC1 masters OSC2 = 2
- **CC 22, Sub-oscillator divide**: Off = 0, Divide by 2 = 2, Divide by 4 = 4
- **CC 23, Osc sync / phase align OSC2**: Off - free running (no note-on sync) = 0, Sync at note-on (0 deg) = 1, Sync + 30 deg = 15, Sync + 45 deg = 2, Sync + 60 deg = 30, Sync + 90 deg = 3, Sync + 120 deg = 60, Sync + 135 deg = 4, Sync + 150 deg = 75, Sync + 180 deg = 5, Sync + 210 deg = 105, Sync + 225 deg = 6, Sync + 240 deg = 120, Sync + 270 deg = 7, Sync + 315 deg = 8
  - out of 7-bit reach, use the serial bench app instead: Sync + 300 deg (150), Sync + 330 deg (165)
- **CC 25, ADSR3 to osc select**: 0 - OSC1 = 0, 1 - OSC2 = 1, 2 - OSC1+2 = 2, 3 - OSC3 = 3, 4 - all = 4
- **CC 60, LFO1 waveform**: 0 - off = 0, 1 - saw = 1, 2 - triangle = 2, 3 - sine = 3, 4 - square = 4
- **CC 61, LFO2 waveform**: 0 - off = 0, 1 - saw = 1, 2 - triangle = 2, 3 - sine = 3, 4 - square = 4
- **CC 69, Voice mode**: 0 - mono = 0, 1 - poly = 1, 2 - stack = 2
- **CC 72, Portamento mode**: 0 - fixed time = 0, 1 - slew rate = 1
- **CC 84, Mod slot 0 source**: 0 ADSR3 (EnvDCO) = 0, 1 ADSR4 (stub) = 1, 2 LFO3 (stub) = 2, 3 LFO4 (stub) = 3, 4 Velocity = 4, 5 Keytrack = 5, 6 Random = 6, 7 Aftertouch = 7
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 85, Mod slot 0 dest**: 0 OSC1 level = 0, 1 OSC2 level = 1, 2 OSC3 level = 2, 3 Sub level = 3, 4 VCF1 reso = 4, 5 VCF2 reso = 5, 6 Dist Drive = 6
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 87, Mod slot 1 source**: 0 ADSR3 (EnvDCO) = 0, 1 ADSR4 (stub) = 1, 2 LFO3 (stub) = 2, 3 LFO4 (stub) = 3, 4 Velocity = 4, 5 Keytrack = 5, 6 Random = 6, 7 Aftertouch = 7
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 88, Mod slot 1 dest**: 0 OSC1 level = 0, 1 OSC2 level = 1, 2 OSC3 level = 2, 3 Sub level = 3, 4 VCF1 reso = 4, 5 VCF2 reso = 5, 6 Dist Drive = 6
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 90, Mod slot 2 source**: 0 ADSR3 (EnvDCO) = 0, 1 ADSR4 (stub) = 1, 2 LFO3 (stub) = 2, 3 LFO4 (stub) = 3, 4 Velocity = 4, 5 Keytrack = 5, 6 Random = 6, 7 Aftertouch = 7
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 91, Mod slot 2 dest**: 0 OSC1 level = 0, 1 OSC2 level = 1, 2 OSC3 level = 2, 3 Sub level = 3, 4 VCF1 reso = 4, 5 VCF2 reso = 5, 6 Dist Drive = 6
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 93, Mod slot 3 source**: 0 ADSR3 (EnvDCO) = 0, 1 ADSR4 (stub) = 1, 2 LFO3 (stub) = 2, 3 LFO4 (stub) = 3, 4 Velocity = 4, 5 Keytrack = 5, 6 Random = 6, 7 Aftertouch = 7
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 94, Mod slot 3 dest**: 0 OSC1 level = 0, 1 OSC2 level = 1, 2 OSC3 level = 2, 3 Sub level = 3, 4 VCF1 reso = 4, 5 VCF2 reso = 5, 6 Dist Drive = 6
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 96, Mod slot 4 source**: 0 ADSR3 (EnvDCO) = 0, 1 ADSR4 (stub) = 1, 2 LFO3 (stub) = 2, 3 LFO4 (stub) = 3, 4 Velocity = 4, 5 Keytrack = 5, 6 Random = 6, 7 Aftertouch = 7
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 97, Mod slot 4 dest**: 0 OSC1 level = 0, 1 OSC2 level = 1, 2 OSC3 level = 2, 3 Sub level = 3, 4 VCF1 reso = 4, 5 VCF2 reso = 5, 6 Dist Drive = 6
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 103, Mod slot 5 source**: 0 ADSR3 (EnvDCO) = 0, 1 ADSR4 (stub) = 1, 2 LFO3 (stub) = 2, 3 LFO4 (stub) = 3, 4 Velocity = 4, 5 Keytrack = 5, 6 Random = 6, 7 Aftertouch = 7
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 104, Mod slot 5 dest**: 0 OSC1 level = 0, 1 OSC2 level = 1, 2 OSC3 level = 2, 3 Sub level = 3, 4 VCF1 reso = 4, 5 VCF2 reso = 5, 6 Dist Drive = 6
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 106, Mod slot 6 source**: 0 ADSR3 (EnvDCO) = 0, 1 ADSR4 (stub) = 1, 2 LFO3 (stub) = 2, 3 LFO4 (stub) = 3, 4 Velocity = 4, 5 Keytrack = 5, 6 Random = 6, 7 Aftertouch = 7
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 107, Mod slot 6 dest**: 0 OSC1 level = 0, 1 OSC2 level = 1, 2 OSC3 level = 2, 3 Sub level = 3, 4 VCF1 reso = 4, 5 VCF2 reso = 5, 6 Dist Drive = 6
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 109, Mod slot 7 source**: 0 ADSR3 (EnvDCO) = 0, 1 ADSR4 (stub) = 1, 2 LFO3 (stub) = 2, 3 LFO4 (stub) = 3, 4 Velocity = 4, 5 Keytrack = 5, 6 Random = 6, 7 Aftertouch = 7
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)
- **CC 110, Mod slot 7 dest**: 0 OSC1 level = 0, 1 OSC2 level = 1, 2 OSC3 level = 2, 3 Sub level = 3, 4 VCF1 reso = 4, 5 VCF2 reso = 5, 6 Dist Drive = 6
  - out of 7-bit reach, use the serial bench app instead: Off / empty (255)

## Deliberately not mapped

- **Run autotune** (parameter 150)
- **Store manual cal offsets** (parameter 156)

Autotune takes the board over for about a minute and the store writes the filesystem, so neither should be one stray controller away. Both are still available from the serial bench app in `tools/dco_control`.

Reserved controllers left untouched: 0, 1, 6, 7, 10, 11, 32, 38, 42, 64, 98, 99, 100, 101, 120, 121, 122, 123, 124, 125, 126, 127. CC 42 keeps its historical meaning here, pitch-bend range in semitones. 98-101 stay free so a later NRPN upgrade needs no reshuffling.
