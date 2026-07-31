#ifndef __MIDI_CC_MAP_H__
#define __MIDI_CC_MAP_H__

// GENERATED FILE - do not edit.
// Emitted by tools/dco_control/gen_midi_map.py from tools/dco_control/params.py.
// Re-run that script after changing params.py; see docs/MIDI_CC_MAP.md for the chart.
//
// cc, target, lo, hi, curve. CC 0 lands on lo and CC 127 on hi; targets at or above
// CC_LOCAL_FIRST are block values that midi_cc_apply() writes directly.

#include <stddef.h>
#include "params_def.h"
#include "midi_cc.h"

static const MidiCcEntry midiCcMap[] = {
  // --- Oscillators ---
  {   2, PARAM_OSC1_INTERVAL,                 0,   127, MIDI_CC_LINEAR },
  {   3, PARAM_OSC2_INTERVAL,                 0,    60, MIDI_CC_LINEAR },
  {   4, PARAM_OSC3_INTERVAL,                 0,    60, MIDI_CC_LINEAR },
  {   5, PARAM_OSC2_DETUNE_VAL,               0,   512, MIDI_CC_LINEAR },
  {   8, PARAM_OSC3_DETUNE_VAL,               0,   512, MIDI_CC_LINEAR },
  {   9, PARAM_SQR1_LEVEL,                    0,   127, MIDI_CC_LINEAR },
  {  12, PARAM_SQR2_LEVEL,                    0,   127, MIDI_CC_LINEAR },
  {  13, PARAM_SUB_LEVEL,                     0,   127, MIDI_CC_LINEAR },
  {  14, PARAM_SQR1_STATUS,                   0,     1, MIDI_CC_LINEAR },
  {  15, PARAM_SQR2_STATUS,                   0,     1, MIDI_CC_LINEAR },
  {  16, PARAM_SAW_STATUS,                    0,     1, MIDI_CC_LINEAR },
  {  17, PARAM_SAW2_STATUS,                   0,     1, MIDI_CC_LINEAR },
  {  18, PARAM_TRI_STATUS,                    0,     1, MIDI_CC_LINEAR },
  {  19, PARAM_SINE_STATUS,                   0,     1, MIDI_CC_LINEAR },
  // --- Sync and PIO ---
  {  20, PARAM_SYNC_MODE,                     0,   127, MIDI_CC_LINEAR },
  {  21, PARAM_SOFT_SYNC,                     0,     1, MIDI_CC_LINEAR },
  {  22, PARAM_SUBOSC_DIVIDE,                 0,   127, MIDI_CC_LINEAR },
  {  23, PARAM_OSC_SYNC_MODE,                 0,   127, MIDI_CC_LINEAR },
  // --- Envelopes ---
  {  24, PARAM_ADSR3_ENABLED,                 0,     1, MIDI_CC_LINEAR },
  {  25, PARAM_ADSR3_TO_OSC_SELECT,           0,   127, MIDI_CC_LINEAR },
  {  26, PARAM_ADSR3_TO_DETUNE1,           -511,   511, MIDI_CC_LINEAR },
  {  27, PARAM_ADSR1_ATTACK_CURVE,            0,     7, MIDI_CC_LINEAR },
  {  28, PARAM_ADSR1_DECAY_CURVE,             0,     7, MIDI_CC_LINEAR },
  {  29, PARAM_ADSR2_ATTACK_CURVE,            0,     7, MIDI_CC_LINEAR },
  {  30, PARAM_ADSR2_DECAY_CURVE,             0,     7, MIDI_CC_LINEAR },
  {  31, PARAM_VCA_ADSR_RESTART,              0,     1, MIDI_CC_LINEAR },
  {  33, PARAM_VCF_ADSR_RESTART,              0,     1, MIDI_CC_LINEAR },
  {  34, CC_LOCAL_ADSR_VCA_ATTACK,            0,  4095, MIDI_CC_EXP_TIME },
  {  35, CC_LOCAL_ADSR_VCA_DECAY,             0,  4095, MIDI_CC_EXP_TIME },
  {  36, CC_LOCAL_ADSR_VCA_SUSTAIN,           0,  4095, MIDI_CC_LINEAR },
  {  37, CC_LOCAL_ADSR_VCA_RELEASE,           0,  4095, MIDI_CC_EXP_TIME },
  {  39, CC_LOCAL_ADSR_VCF_ATTACK,            0,  4095, MIDI_CC_EXP_TIME },
  {  40, CC_LOCAL_ADSR_VCF_DECAY,             0,  4095, MIDI_CC_EXP_TIME },
  {  41, CC_LOCAL_ADSR_VCF_SUSTAIN,           0,  4095, MIDI_CC_LINEAR },
  {  43, CC_LOCAL_ADSR_VCF_RELEASE,           0,  4095, MIDI_CC_EXP_TIME },
  {  44, CC_LOCAL_ADSR_DCO_ATTACK,            0,  4095, MIDI_CC_EXP_TIME },
  {  45, CC_LOCAL_ADSR_DCO_DECAY,             0,  4095, MIDI_CC_EXP_TIME },
  {  46, CC_LOCAL_ADSR_DCO_SUSTAIN,           0,  4095, MIDI_CC_LINEAR },
  {  47, CC_LOCAL_ADSR_DCO_RELEASE,           0,  4095, MIDI_CC_EXP_TIME },
  {  48, CC_LOCAL_ADSR1_TO_VCA_AMOUNT,        0,   512, MIDI_CC_LINEAR },
  // --- Filter ---
  {  49, PARAM_VCF_KEYTRACK,               -256,   255, MIDI_CC_LINEAR },
  {  50, PARAM_VELOCITY_TO_VCF,               0,    20, MIDI_CC_LINEAR },
  {  51, PARAM_RESONANCE_COMPENSATION,        0,     1, MIDI_CC_LINEAR },
  {  52, CC_LOCAL_FILTER_CUTOFF,              0,  4095, MIDI_CC_LINEAR },
  {  53, CC_LOCAL_FILTER_RESONANCE,           0,  4095, MIDI_CC_LINEAR },
  {  54, CC_LOCAL_FILTER_ADSR2_TO_VCF,        0,   512, MIDI_CC_LINEAR },
  {  55, CC_LOCAL_FILTER_LFO2_TO_VCF,         0,   512, MIDI_CC_LINEAR },
  // --- PWM ---
  {  56, PARAM_LFO2_TO_PW,                    0,   511, MIDI_CC_LINEAR },
  {  57, PARAM_ADSR3_TO_PWM,                  0,  1023, MIDI_CC_LINEAR },
  {  58, PARAM_PWM_POTS_CONTROL_MANUAL,       0,     1, MIDI_CC_LINEAR },
  {  59, CC_LOCAL_PW_PW,                      0,  4095, MIDI_CC_LINEAR },
  // --- LFOs ---
  {  60, PARAM_LFO1_WAVEFORM,                 0,   127, MIDI_CC_LINEAR },
  {  61, PARAM_LFO2_WAVEFORM,                 0,   127, MIDI_CC_LINEAR },
  {  62, PARAM_LFO1_SPEED,                    0,  4095, MIDI_CC_LINEAR },
  {  63, PARAM_LFO2_SPEED,                    0,  4095, MIDI_CC_LINEAR },
  {  65, PARAM_LFO1_TO_DCO,                   0,   511, MIDI_CC_LINEAR },
  {  66, PARAM_LFO1_TO_VCA,                   0,  1023, MIDI_CC_LINEAR },
  {  67, PARAM_LFO2_TO_DETUNE2,               0,   255, MIDI_CC_LINEAR },
  {  68, PARAM_LFO2_TO_DETUNE3,               0,   255, MIDI_CC_LINEAR },
  // --- Voice and Drift ---
  {  69, PARAM_VOICE_MODE,                    0,   127, MIDI_CC_LINEAR },
  {  70, PARAM_UNISON_DETUNE,                 0,   127, MIDI_CC_LINEAR },
  {  71, PARAM_PORTAMENTO_TIME,               0,   255, MIDI_CC_LINEAR },
  {  72, PARAM_PORTAMENTO_MODE,               0,   127, MIDI_CC_LINEAR },
  {  73, PARAM_ANALOG_DRIFT_AMOUNT,           0,   127, MIDI_CC_LINEAR },
  {  74, PARAM_ANALOG_DRIFT_SPEED,            1,   255, MIDI_CC_LINEAR },
  {  75, PARAM_ANALOG_DRIFT_SPREAD,           1,   127, MIDI_CC_LINEAR },
  {  76, PARAM_VCA_LEVEL,                     0,   128, MIDI_CC_LINEAR },
  {  77, PARAM_VELOCITY_TO_VCA,               0,    20, MIDI_CC_LINEAR },
  // --- Calibration ---
  {  78, PARAM_MANUAL_CALIBRATION_FLAG,       0,     1, MIDI_CC_LINEAR },
  {  79, PARAM_MANUAL_CALIBRATION_STAGE,      0,     2, MIDI_CC_LINEAR },
  {  80, PARAM_MANUAL_CALIBRATION_OFFSET,   -20,    20, MIDI_CC_LINEAR },
};

static const size_t midiCcMapSize = sizeof(midiCcMap) / sizeof(midiCcMap[0]);

#endif
