#include "include_all.h"

#if __has_include("bench.h")
#include "bench.h"
#elif __has_include("../bench.h")
#include "../bench.h"
#endif

// =============================================================================
// 1. Oscillator & Voice Configuration
// =============================================================================

static void apply_param_osc1_interval(int16_t v)  { octave_shift = (int8_t)v; }
static void apply_param_osc2_interval(int16_t v)  { OSC2_interval = (int8_t)v; }
static void apply_param_osc3_interval(int16_t v)  { OSC3_interval = (int8_t)v; }
static void apply_param_osc2_detune(int16_t v)    { OSC2_detune = (uint16_t)v; }
static void apply_param_osc3_detune(int16_t /*v*/) { /* DCO3 monosynth only */ }
static void apply_param_unison_detune(int16_t v)  { unisonDetune = v; }

static void apply_param_portamento_time(int16_t v){ portamento_time = (uint16_t)v; }
static void apply_param_portamento_mode(int16_t v){ portamento_mode = (uint8_t)v; }

static void apply_param_voice_mode(int16_t v) { 
  voiceMode = (uint8_t)constrain(v, 0, 2);
  setVoiceMode(); 
}

static void apply_param_voice_alloc_mode(int16_t v){ 
  voiceAlloc.setMode((uint8_t)constrain(v, 0, 5)); 
}

static void apply_param_sync_mode(int16_t v) { 
  syncMode = (uint8_t)v;
  setSyncMode(); 
}

static void apply_param_soft_sync(int16_t v)      { softSyncChunks = (uint8_t)v; }
static void apply_param_subosc_divide(int16_t v)  { subOscDivide = (uint8_t)v; }

// =============================================================================
// 2. LFO Speeds, Waveforms & Pitch Depths
// =============================================================================

static void apply_param_lfo1_waveform(int16_t v) {
  LFO1Waveform = (uint8_t)v;
  LFO1_class.setWaveForm(LFO1Waveform);
}

static void apply_param_lfo2_waveform(int16_t v) {
  LFO2Waveform = (uint8_t)v;
  LFO2_class.setWaveForm(LFO2Waveform);
}

static void apply_param_lfo1_speed(int16_t v) {
  LFO1SpeedVal = (uint16_t)v;
  LFO1Speed = expConverterFloat(LFO1SpeedVal, 5000);
  LFO1_class.setMode0Freq((float)LFO1Speed, micros());
}

static void apply_param_lfo2_speed(int16_t v) {
  LFO2SpeedVal = (uint16_t)v;
  LFO2Speed = expConverterFloat(LFO2SpeedVal, 5000);
  LFO2_class.setMode0Freq((float)LFO2Speed, micros());
}

static void apply_param_lfo1_to_dco(int16_t v) {
  LFO1toDCOVal = (uint16_t)v;
  float amt = (float)expConverterFloat(LFO1toDCOVal, 500) / 275000.0f;
  LFO1toDCO_q24 = lfo_pitch_depth_q24(amt, LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo1_to_osc1(int16_t v) {
  float amt = (float)expConverterFloat((uint8_t)constrain(v, 0, 255), 500) / 275000.0f;
  LFO1toOSC1_q24 = lfo_pitch_depth_q24(amt, LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo1_to_osc2(int16_t v) {
  float amt = (float)expConverterFloat((uint8_t)constrain(v, 0, 255), 500) / 275000.0f;
  LFO1toOSC2_q24 = lfo_pitch_depth_q24(amt, LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo1_to_osc3(int16_t v) {
  float amt = (float)expConverterFloat((uint8_t)constrain(v, 0, 255), 500) / 275000.0f;
  LFO1toOSC3_q24 = lfo_pitch_depth_q24(amt, LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo2_to_osc2(int16_t v) {
  float amt = (float)expConverterFloat((uint16_t)v, 500) / 275000.0f;
  LFO2toOSC2_q24 = lfo_pitch_depth_q24(amt, LFO2_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo2_to_osc3(int16_t v) {
  float amt = (float)expConverterFloat((uint16_t)v, 500) / 275000.0f;
  LFO2toOSC3_q24 = lfo_pitch_depth_q24(amt, LFO2_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo2_to_osc2_coarse(int16_t v) {
  float amt = (float)expConverterFloat((uint16_t)constrain(v, 0, 511), 500) / 275000.0f;
  LFO2toOSC2_coarse_q24 = lfo_pitch_depth_q24(amt, LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo2_to_osc3_coarse(int16_t v) {
  float amt = (float)expConverterFloat((uint16_t)constrain(v, 0, 511), 500) / 275000.0f;
  LFO2toOSC3_coarse_q24 = lfo_pitch_depth_q24(amt, LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo2_to_pw(int16_t v) {
  LFO2toPW = (uint16_t)v;
}

// =============================================================================
// 3. Analog Drift, Character & Pulse Width
// =============================================================================

static void apply_param_analog_drift_amount(int16_t v) {
  analogDrift = (int8_t)v;
}

static void apply_param_analog_drift_speed(int16_t v) {
  analogDriftSpeed = v;
  init_DRIFT_LFOs();
}

static void apply_param_analog_drift_spread(int16_t v) {
  analogDriftSpread = (int8_t)v;
  init_DRIFT_LFOs();
}

static void apply_param_character(int16_t /*v*/) {
  // Master scale hook for character/jitter
}

static void apply_param_pw_value(int16_t v) {
  PW[0] = (uint16_t)(v >> 2);
}

// =============================================================================
// 4. Envelope Modulations
// =============================================================================

static void apply_param_adsr3_to_osc_select(int16_t v) {
  ADSR3ToOscSelect = (int8_t)v;
}

static void apply_param_adsr3_to_pwm(int16_t v) {
  ADSR1toPWM = (int16_t)v - 512;
}

static void apply_param_adsr3_to_detune1(int16_t v) {
  ADSR1toDETUNE1 = v;
  ADSR1toDETUNE1_scale_q24 = ((int64_t)ADSR1toDETUNE1 * (1 << 24)) / 4095;
}

static void apply_param_adsr3_pitch_mode(int16_t v) {
  env_dco_pitch_centered = (v != 0) ? 1 : 0;
}

// =============================================================================
// 5. Calibration Controls & Storage Trims
// =============================================================================

static void apply_param_calibration_flag(int16_t v) {
  calibrationFlag = (v != 0);
}

static void apply_param_manual_calibration_flag(int16_t v) {
  manualCalibrationFlag = (v != 0);
  calibrationFlag = (v != 0);
}

static void apply_param_manual_calibration_stage(int16_t v) {
  manualCalibrationStage = (uint8_t)v;
}

static void apply_param_manual_calibration_offset(int16_t v) {
  uint8_t osc = cal_stage_to_osc_n(manualCalibrationStage, NUM_OSCILLATORS);
  if (osc < NUM_OSCILLATORS) {
    manualCalibrationOffset[osc] = (int8_t)v;
  }
}

static void apply_param_manual_calibration_step(int16_t v) {
  manualCalibrationStep = (uint8_t)v;
}

static void apply_param_amp_comp_440(int16_t v) {
  uint8_t osc = cal_stage_to_osc_n(manualCalibrationStage, NUM_OSCILLATORS);
  if (osc < NUM_OSCILLATORS) {
    ampComp440[osc] = (uint16_t)constrain(v, AMP_COMP_440_MIN, AMP_COMP_440_MAX);
  }
}

static void apply_param_cal_pw_center(int16_t v) {
  uint8_t osc = cal_stage_to_osc_n(manualCalibrationStage, NUM_OSCILLATORS);
  uint8_t ch = osc / 2;
  if (ch < 4) {
    PW_CENTER[ch] = (uint16_t)constrain(v, 0, CAL_PW_CENTER_MAX);
  }
}

static void apply_param_amp_comp_duty_offset(int16_t v) {
  uint8_t osc = cal_stage_to_osc_n(manualCalibrationStage, NUM_OSCILLATORS);
  if (osc < NUM_OSCILLATORS) {
    ampCompDutyOffset[osc] = (int16_t)constrain(v, -500, 500);
  }
}

static void apply_param_manual_calibration_store(int16_t /*v*/) {
  for (uint8_t i = 0; i < NUM_OSCILLATORS; i++) {
    update_FS_ManualCalibrationOffset(i, manualCalibrationOffset[i]);
    update_FS_AmpComp440(i, ampComp440[i]);
    update_FS_AmpCompDutyOffset(i, ampCompDutyOffset[i]);
  }
  for (uint8_t ch = 0; ch < 4; ch++) {
    update_FS_PWCenter(ch, PW_CENTER[ch]);
  }
}

// =============================================================================
// 6. Diagnostic & Debug Commands (PARAM_DEBUG_COMMAND 160)
// =============================================================================

static void apply_param_debug_command(int16_t v) {
  // Packed Character setters (0xC8xx, 0xCAxx, 0xCBxx)
  uint8_t hi = (uint8_t)((uint16_t)v >> 8);
  uint8_t lo = (uint8_t)((uint16_t)v & 0xFF);
  if (hi == 0xC8) { ampCompJitter = lo; return; }
  if (hi == 0xCA) { pitchJitter = lo; return; }
  if (hi == 0xCB) { pulsewidthJitter = lo; return; }

  // Set pioPulseLength (200..50000)
  if ((uint16_t)v >= 200 && (uint16_t)v <= 50000) {
    pioPulseLength = (uint16_t)v;
    pio_defer_request_reset_pulse_all();
    return;
  }

  switch (v) {
    case 1:
      pio_topology_report();
      break;

    case 2:
      pio_period_probe(0, 100);
      break;

    case 3:
      pio_period_probe(0, 50000);
      break;

    case 10:
#ifdef RUNNING_AVERAGE
      bench_dump_request = true;
#endif
      break;

    case 11:
#ifdef RUNNING_AVERAGE
      bench_reset_all();
#endif
      break;

    case 12:
#ifdef RUNNING_AVERAGE
      bench_periodic = !bench_periodic;
#endif
      break;

    case 13:
#ifdef ENABLE_MEM_DIAG
      mem_diag_request();
#endif
      break;

    case 14:
#ifdef ENABLE_MEM_DIAG
      mem_diag_runtime_enabled = false;
#endif
      break;

    case 15:
#ifdef ENABLE_MEM_DIAG
      mem_diag_runtime_enabled = true;
#endif
      break;

    case 30:
      seed_fake_calibration_tables(true);
      break;

    // Forward Mainboard-specific debug commands over Serial2
    case 42:
    case 43:
    case 44:
    case 45:
      serialSendParam16(PARAM_DEBUG_COMMAND, v);
      break;

    default:
      break;
  }
}

// =============================================================================
// 7. Preset Store & Recall
// =============================================================================

static void apply_param_preset_save(int16_t v)       { preset_store_save((uint8_t)v); }
static void apply_param_preset_load(int16_t v)       { preset_store_load((uint8_t)v); }
static void apply_param_preset_dump(int16_t v)       { preset_store_dump((int16_t)v); }
static void apply_param_ui_preset_scroll(int16_t v) { 
  const uint8_t slot = (uint8_t)v;

  // 1. Send 'q' to update the Screen's name and slot number
  serial_send_preset_scroll_to_mb(slot); 

  // 2. Send 'L' to tell the Input Controller to turn off manual controls and sync the slot!
  serial_send_preset_loaded_to_mb(slot); 
}

// =============================================================================
// 8. Router Table & Jump Setup
// =============================================================================

static const ParamDescriptorT<int16_t> paramTable[] = {
  { PARAM_OSC1_INTERVAL,            apply_param_osc1_interval },
  { PARAM_OSC2_INTERVAL,            apply_param_osc2_interval },
  { PARAM_OSC3_INTERVAL,            apply_param_osc3_interval },
  { PARAM_OSC2_DETUNE_VAL,          apply_param_osc2_detune },
  { PARAM_OSC3_DETUNE_VAL,          apply_param_osc3_detune },
  { PARAM_UNISON_DETUNE,            apply_param_unison_detune },
  { PARAM_PORTAMENTO_TIME,          apply_param_portamento_time },
  { PARAM_PORTAMENTO_MODE,          apply_param_portamento_mode },
  { PARAM_VOICE_MODE,               apply_param_voice_mode },
  { PARAM_VOICE_ALLOC_MODE,         apply_param_voice_alloc_mode },
  { PARAM_SYNC_MODE,                apply_param_sync_mode },
  { PARAM_SOFT_SYNC,                apply_param_soft_sync },
  { PARAM_SUBOSC_DIVIDE,            apply_param_subosc_divide },

  { PARAM_LFO1_WAVEFORM,            apply_param_lfo1_waveform },
  { PARAM_LFO2_WAVEFORM,            apply_param_lfo2_waveform },
  { PARAM_LFO1_SPEED,               apply_param_lfo1_speed },
  { PARAM_LFO2_SPEED,               apply_param_lfo2_speed },
  { PARAM_LFO1_TO_DCO,              apply_param_lfo1_to_dco },
  { PARAM_LFO1_TO_OSC1,             apply_param_lfo1_to_osc1 },
  { PARAM_LFO1_TO_OSC2,             apply_param_lfo1_to_osc2 },
  { PARAM_LFO1_TO_OSC3,             apply_param_lfo1_to_osc3 },
  { PARAM_LFO2_TO_OSC2,             apply_param_lfo2_to_osc2 },
  { PARAM_LFO2_TO_OSC3,             apply_param_lfo2_to_osc3 },
  { PARAM_LFO2_TO_OSC2_COARSE,      apply_param_lfo2_to_osc2_coarse },
  { PARAM_LFO2_TO_OSC3_COARSE,      apply_param_lfo2_to_osc3_coarse },
  { PARAM_LFO2_TO_PW,               apply_param_lfo2_to_pw },

  { PARAM_ANALOG_DRIFT_AMOUNT,      apply_param_analog_drift_amount },
  { PARAM_ANALOG_DRIFT_SPEED,       apply_param_analog_drift_speed },
  { PARAM_ANALOG_DRIFT_SPREAD,      apply_param_analog_drift_spread },
  { PARAM_CHARACTER,                apply_param_character },
  { PARAM_PW_VALUE,                 apply_param_pw_value },

  { PARAM_ADSR3_TO_OSC_SELECT,      apply_param_adsr3_to_osc_select },
  { PARAM_ADSR3_TO_PWM,             apply_param_adsr3_to_pwm },
  { PARAM_ADSR3_TO_DETUNE1,         apply_param_adsr3_to_detune1 },
  { PARAM_ADSR3_PITCH_MODE,         apply_param_adsr3_pitch_mode },

  { PARAM_CALIBRATION_FLAG,         apply_param_calibration_flag },
  { PARAM_MANUAL_CALIBRATION_FLAG,  apply_param_manual_calibration_flag },
  { PARAM_MANUAL_CALIBRATION_STAGE, apply_param_manual_calibration_stage },
  { PARAM_MANUAL_CALIBRATION_OFFSET,apply_param_manual_calibration_offset },
  { PARAM_MANUAL_CALIBRATION_STEP,  apply_param_manual_calibration_step },
  { PARAM_AMP_COMP_440,             apply_param_amp_comp_440 },
  { PARAM_CAL_PW_CENTER,            apply_param_cal_pw_center },
  { PARAM_AMP_COMP_DUTY_OFFSET,     apply_param_amp_comp_duty_offset },
  { PARAM_MANUAL_CALIBRATION_STORE, apply_param_manual_calibration_store },

  { PARAM_PRESET_SAVE,              apply_param_preset_save },
  { PARAM_PRESET_LOAD,              apply_param_preset_load },
  { PARAM_PRESET_DUMP,              apply_param_preset_dump },
  { PARAM_UI_PRESET_SCROLL,         apply_param_ui_preset_scroll },
  { PARAM_DEBUG_COMMAND,            apply_param_debug_command },
};

static void (*dcoParamJump[PARAM_ROUTER_JUMP_SIZE])(int16_t) = { nullptr };

void init_param_router() {
  param_router_build_jump(
    dcoParamJump,
    paramTable,
    sizeof(paramTable) / sizeof(paramTable[0])
  );
}

void update_parameters(uint8_t id, int16_t value) {
  param_router_apply(dcoParamJump, id, value);
  preset_shadow_capture(id, value);
}