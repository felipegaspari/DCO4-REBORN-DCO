#ifndef PARAMS_DEF_H
#define PARAMS_DEF_H

#include <stdint.h>

// Central definition of all parameter IDs used across MCUs.
// This DCO header mirrors the mainboard's params_def.h numeric values
// so that parameters sent over Serial are interpreted consistently.
//
// IMPORTANT:
//   - Do not change numeric values of existing IDs.
//   - New parameters should get new, unused numbers.
//   - The meaning of each ID (name + number) should be stable across MCUs.

enum ParamId : uint16_t {
  // --- Oscillator wave enable (mainboard-local, not used on DCO) ------
  PARAM_SAW_STATUS               = 1,
  PARAM_SAW2_STATUS              = 2,
  PARAM_TRI_STATUS               = 3,
  PARAM_SINE_STATUS              = 4,

  // Shared with DCO: 5, 10.. etc
  PARAM_SQR1_STATUS              = 5,
  PARAM_SQR2_STATUS              = 6,   // mainboard-local

  PARAM_RESONANCE_COMPENSATION   = 7,   // mainboard-local
  PARAM_VCA_ADSR_RESTART         = 8,   // mainboard-local
  PARAM_VCF_ADSR_RESTART         = 9,   // mainboard-local

  // --- Shared routing / oscillator parameters -------------------------
  PARAM_ADSR3_TO_OSC_SELECT      = 10,

  PARAM_LFO1_WAVEFORM            = 11,
  PARAM_LFO2_WAVEFORM            = 12,

  PARAM_OSC1_INTERVAL            = 13,
  PARAM_OSC2_INTERVAL            = 14,

  PARAM_OSC2_DETUNE_VAL          = 15,
  PARAM_LFO2_TO_DETUNE2          = 16,

  PARAM_OSC_SYNC_MODE            = 17,

  PARAM_PORTAMENTO_TIME          = 18,

  // --- Mainboard-local filter/velocity routing ------------------------
  PARAM_VCF_KEYTRACK             = 19,
  PARAM_VELOCITY_TO_VCF          = 20,
  PARAM_VELOCITY_TO_VCA          = 21,
  PARAM_SQR1_LEVEL               = 22,
  PARAM_SQR2_LEVEL               = 23,
  PARAM_SUB_LEVEL                = 24,

  // --- Shared calibration / voice mode --------------------------------
  PARAM_CALIBRATION_VALUE        = 25,

  PARAM_VOICE_MODE               = 26,
  PARAM_UNISON_DETUNE            = 27,

  PARAM_ANALOG_DRIFT_AMOUNT      = 28,
  PARAM_ANALOG_DRIFT_SPEED       = 29,
  PARAM_ANALOG_DRIFT_SPREAD      = 30,

  PARAM_SYNC_MODE                = 31,

  // 32: DCO-only portamento mode selector (currently local to DCO)
  PARAM_PORTAMENTO_MODE          = 32,

  // DCO3 monosynth OSC3 (new IDs — wire on Mainboard/Input/Screen later)
  PARAM_OSC3_INTERVAL            = 33,
  PARAM_OSC3_DETUNE_VAL          = 34,
  PARAM_LFO2_TO_DETUNE3          = 35,

  // 36: sync flavour. 0 = hard sync (master sidesets onto the slave's reset pin),
  // 1 = soft sync (slave polls the master and ignores edges early in its own cycle).
  PARAM_SOFT_SYNC                = 36,

  // 37: sub-oscillator divide. 0 = off, 2 = one octave down, 4 = two octaves.
  PARAM_SUBOSC_DIVIDE            = 37,

  // --- LFO routing (shared) -------------------------------------------
  PARAM_LFO1_TO_DCO              = 40,
  PARAM_LFO1_SPEED               = 41,
  PARAM_LFO2_SPEED               = 42,

  // --- Mainboard-only VCA routing ------------------------------------
  PARAM_VCA_LEVEL                = 43,
  PARAM_LFO1_TO_VCA              = 44,

  // --- PWM / ADSR to PWM / detune (shared with DCO) -------------------
  PARAM_LFO2_TO_PW               = 45,
  PARAM_ADSR3_TO_PWM             = 46,
  PARAM_ADSR3_TO_DETUNE1         = 47,

  // ADSR curve shaping (mainboard-local only)
  PARAM_ADSR1_ATTACK_CURVE       = 48,
  PARAM_ADSR1_DECAY_CURVE        = 49,
  PARAM_ADSR2_ATTACK_CURVE       = 50,
  PARAM_ADSR2_DECAY_CURVE        = 51,

  // Post-LP distortion CVs (Drive VCA + dry/wet Mix). See docs/DISTORTION.md.
  PARAM_DIST_DRIVE               = 52,
  PARAM_DIST_MIX                 = 53,

  // AS3320 multimode select (0..N). Dual-MCU: RP2040 aux; solo-B: DCO. See docs/FILTER_ROUTING.md.
  PARAM_FILTER_MODE              = 54,

  // FX placeholders (RP2040 aux in dual-MCU builds). IDs reserved; not wired yet.
  // PARAM_FX_PROGRAM             = 55,
  // PARAM_FX_MIX                 = 56,

  // --- Misc / control / UI flags ------------------------------------
  // Calibration mode selector (screen/UI only for now)
  PARAM_CALIBRATION_MODE         = 101,

  // Global/manual control flags (input+screen; DCO may ignore)
  PARAM_FADERS_CONTROL_MANUAL    = 120,
  PARAM_FADER_ROW1_CONTROL_MANUAL= 121,
  PARAM_FADER_ROW2_CONTROL_MANUAL= 122,
  PARAM_VCF_POTS_CONTROL_MANUAL  = 123,
  PARAM_PWM_POTS_CONTROL_MANUAL  = 124,
  PARAM_ALL_CONTROLS_MANUAL      = 125,

  PARAM_ADSR3_ENABLED            = 126,
  PARAM_FUNCTION_KEY             = 127,

  PARAM_VCA_POTS_CONTROL_MANUAL  = 128,
  PARAM_POTS_CONTROL_MANUAL      = 129,

  // UI navigation / calibration helper parameters (screen-focused)
  PARAM_UI_MENU_POSITION         = 190,
  PARAM_UI_CALIBRATION_DISMISS   = 199,
  PARAM_UI_CALIBRATION_MENU_MODE = 200,

  // Reserved / screen-only extras (future expansion)
  PARAM_PW_VALUE                 = 210,
  PARAM_LFO3_SPEED               = 211,
  PARAM_LFO3_WAVEFORM            = 212,
  PARAM_ADSR3_RESTART            = 214,
  PARAM_VCA_LEVEL_ALT            = 215,

  // --- Calibration flags (shared) ------------------------------------
  PARAM_CALIBRATION_FLAG         = 150,
  PARAM_MANUAL_CALIBRATION_FLAG  = 151,
  PARAM_MANUAL_CALIBRATION_STAGE = 152,
  PARAM_MANUAL_CALIBRATION_OFFSET= 153,

  // 154: gap from DCO — TX to Input on Serial2; Input relays it to the Screen
  PARAM_GAP_FROM_DCO             = 154,

  // 155: manual calibration offsets reported from DCO back to Input.
  PARAM_MANUAL_CALIBRATION_OFFSET_FROM_DCO = 155,

  // 156: explicit "store manual calibration offsets" command.
  PARAM_MANUAL_CALIBRATION_STORE = 156,

  // 160: bench diagnostic trigger (DCO-local, no panel UI). Prints to USB serial.
  // 1 = PIO topology report, 2 = period probe at a low divider,
  // 3 = period probe at a high divider. See DCO/docs/PIO_OSCILLATORS.md section 12.
  // 10 = dump profiler once, 11 = reset profiler, 12 = toggle ~1 Hz dump
  // (RUNNING_AVERAGE builds only). See DCO/docs/BENCHMARKING.md.
  PARAM_DEBUG_COMMAND            = 160
};

#endif  // PARAMS_DEF_H



