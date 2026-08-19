#include "include_all.h"

#if __has_include("bench.h")
#include "bench.h"
#elif __has_include("../bench.h")
#include "../bench.h"
#endif

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"

//============================================================================================
// Apply Preset From RAM Shadow Copy
//============================================================================================
void __not_in_flash_func(dco_apply_preset_shadow)() {
  // =========================================================================
  // 1. Oscillator & Voice Configuration
  // =========================================================================
  pulseWaveOn       = (presetParamShadow[PARAM_OSC1_PULSE_ENABLE] != 0);
  octave_shift      = (int8_t)presetParamShadow[PARAM_OSC1_INTERVAL];
  OSC2_interval     = (int8_t)presetParamShadow[PARAM_OSC2_INTERVAL];
  OSC3_interval     = (int8_t)presetParamShadow[PARAM_OSC3_INTERVAL];
  OSC2_detune       = (uint16_t)presetParamShadow[PARAM_OSC2_DETUNE_VAL];
  unisonDetune      = presetParamShadow[PARAM_UNISON_DETUNE];

  portamento_time   = (uint16_t)presetParamShadow[PARAM_PORTAMENTO_TIME];
  portamento_mode   = (uint8_t)presetParamShadow[PARAM_PORTAMENTO_MODE];

  voiceMode         = (uint8_t)constrain(presetParamShadow[PARAM_VOICE_MODE], 0, 2);
  setVoiceMode();
  voiceAlloc.setMode((uint8_t)constrain(presetParamShadow[PARAM_VOICE_ALLOC_MODE], 0, 5));

  syncMode          = (uint8_t)presetParamShadow[PARAM_SYNC_MODE];
  setSyncMode();
  softSyncChunks    = (uint8_t)presetParamShadow[PARAM_SOFT_SYNC];
  subOscDivide      = (uint8_t)presetParamShadow[PARAM_SUBOSC_DIVIDE];

  // =========================================================================
  // 2. LFO Speeds, Waveforms & Frequencies
  // =========================================================================
  LFO1Waveform      = (uint8_t)presetParamShadow[PARAM_LFO1_WAVEFORM];
  LFO1_class.setWaveForm(LFO1Waveform);

  LFO2Waveform      = (uint8_t)presetParamShadow[PARAM_LFO2_WAVEFORM];
  LFO2_class.setWaveForm(LFO2Waveform);

  const uint32_t now_us = micros();
  LFO1SpeedVal      = (uint16_t)presetParamShadow[PARAM_LFO1_SPEED];
  LFO1Speed         = fast_exp_speed_5000(LFO1SpeedVal);
  LFO1_class.setMode0Freq(LFO1Speed, now_us);

  LFO2SpeedVal      = (uint16_t)presetParamShadow[PARAM_LFO2_SPEED];
  LFO2Speed         = fast_exp_speed_5000(LFO2SpeedVal);
  LFO2_class.setMode0Freq(LFO2Speed, now_us);

  // =========================================================================
  // 3. LFO Pitch Depths (Q24 Math)
  // =========================================================================
  LFO1toDCOVal      = (uint16_t)presetParamShadow[PARAM_LFO1_TO_DCO];
  LFO1toDCO_q24     = lfo_pitch_depth_q24(fast_lfo_depth_amt(LFO1toDCOVal), LFO1_PITCH_DEPTH_SCALE);
  LFO1toOSC1_q24    = lfo_pitch_depth_q24(fast_lfo_depth_amt(constrain(presetParamShadow[PARAM_LFO1_TO_OSC1], 0, 255)), LFO1_PITCH_DEPTH_SCALE);
  LFO1toOSC2_q24    = lfo_pitch_depth_q24(fast_lfo_depth_amt(constrain(presetParamShadow[PARAM_LFO1_TO_OSC2], 0, 255)), LFO1_PITCH_DEPTH_SCALE);
  LFO1toOSC3_q24    = lfo_pitch_depth_q24(fast_lfo_depth_amt(constrain(presetParamShadow[PARAM_LFO1_TO_OSC3], 0, 255)), LFO1_PITCH_DEPTH_SCALE);

  LFO2toOSC2_q24    = lfo_pitch_depth_q24(fast_lfo_depth_amt(presetParamShadow[PARAM_LFO2_TO_OSC2]), LFO2_PITCH_DEPTH_SCALE);
  LFO2toOSC3_q24    = lfo_pitch_depth_q24(fast_lfo_depth_amt(presetParamShadow[PARAM_LFO2_TO_OSC3]), LFO2_PITCH_DEPTH_SCALE);
  LFO2toOSC2_coarse_q24 = lfo_pitch_depth_q24(fast_lfo_depth_amt(constrain(presetParamShadow[PARAM_LFO2_TO_OSC2_COARSE], 0, 511)), LFO1_PITCH_DEPTH_SCALE);
  LFO2toOSC3_coarse_q24 = lfo_pitch_depth_q24(fast_lfo_depth_amt(constrain(presetParamShadow[PARAM_LFO2_TO_OSC3_COARSE], 0, 511)), LFO1_PITCH_DEPTH_SCALE);

  LFO2toPW          = (uint16_t)presetParamShadow[PARAM_LFO2_TO_PW];

  // =========================================================================
  // 4. Analog Drift, Pulse Width & Character
  // =========================================================================
  analogDrift       = (int8_t)presetParamShadow[PARAM_ANALOG_DRIFT_AMOUNT];
  analogDriftSpeed  = presetParamShadow[PARAM_ANALOG_DRIFT_SPEED];
  analogDriftSpread = (int8_t)presetParamShadow[PARAM_ANALOG_DRIFT_SPREAD];
  init_DRIFT_LFOs(); // Initialized ONCE

  PW[0]             = (uint16_t)(presetParamShadow[PARAM_PW_VALUE] >> 2);

  // =========================================================================
  // 5. Envelope Modulations
  // =========================================================================
  ADSR3ToOscSelect         = (int8_t)presetParamShadow[PARAM_ADSR3_TO_OSC_SELECT];
  ADSR1toPWM               = (int16_t)presetParamShadow[PARAM_ADSR3_TO_PWM] - 512;
  ADSR1toPWM_scale         = ADSR1toPWM;
  ADSR1toDETUNE1           = presetParamShadow[PARAM_ADSR3_TO_DETUNE1];
  ADSR1toDETUNE1_scale_q24 = (int32_t)presetParamShadow[PARAM_ADSR3_TO_DETUNE1] * 4097;
  env_dco_pitch_centered   = (presetParamShadow[PARAM_ADSR3_PITCH_MODE] != 0) ? 1 : 0;
}

// =============================================================================
// 1. Oscillator & Voice Configuration
// =============================================================================

static void apply_param_osc1_pulse_enable(int16_t v) { pulseWaveOn = (v != 0); }
static void apply_param_osc1_interval(int16_t v) { octave_shift = (int8_t)v; }
static void apply_param_osc2_interval(int16_t v) { OSC2_interval = (int8_t)v; }
static void apply_param_osc3_interval(int16_t v) { OSC3_interval = (int8_t)v; }
static void apply_param_osc2_detune(int16_t v) { OSC2_detune = (uint16_t)v; }
static void apply_param_osc3_detune(int16_t /*v*/) { /* DCO3 monosynth only */ }
static void apply_param_unison_detune(int16_t v) { unisonDetune = v; }

static void apply_param_portamento_time(int16_t v) {
  portamento_time = (uint16_t)v;
}
static void apply_param_portamento_mode(int16_t v) {
  portamento_mode = (uint8_t)v;
}

static void apply_param_voice_mode(int16_t v) {
  voiceMode = (uint8_t)constrain(v, 0, 2);
  setVoiceMode();
}

static void apply_param_voice_alloc_mode(int16_t v) {
  voiceAlloc.setMode((uint8_t)constrain(v, 0, 5));
}

static void apply_param_sync_mode(int16_t v) {
  syncMode = (uint8_t)v;
  setSyncMode();
}

static void apply_param_soft_sync(int16_t v) { softSyncChunks = (uint8_t)v; }
static void apply_param_subosc_divide(int16_t v) { subOscDivide = (uint8_t)v; }

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

// --- LFO Speeds ---
static void apply_param_lfo1_speed(int16_t v) {
  LFO1SpeedVal = (uint16_t)v;
  LFO1Speed = fast_exp_speed_5000(LFO1SpeedVal);
  LFO1_class.setMode0Freq(LFO1Speed, micros());
}

static void apply_param_lfo2_speed(int16_t v) {
  LFO2SpeedVal = (uint16_t)v;
  LFO2Speed = fast_exp_speed_5000(LFO2SpeedVal);
  LFO2_class.setMode0Freq(LFO2Speed, micros());
}

// --- LFO Pitch Depths ---
static void apply_param_lfo1_to_dco(int16_t v) {
  LFO1toDCOVal = (uint16_t)v;
  LFO1toDCO_q24 = lfo_pitch_depth_q24(fast_lfo_depth_amt(LFO1toDCOVal), LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo1_to_osc1(int16_t v) {
  LFO1toOSC1_q24 = lfo_pitch_depth_q24(fast_lfo_depth_amt(constrain(v, 0, 255)), LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo1_to_osc2(int16_t v) {
  LFO1toOSC2_q24 = lfo_pitch_depth_q24(fast_lfo_depth_amt(constrain(v, 0, 255)), LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo1_to_osc3(int16_t v) {
  LFO1toOSC3_q24 = lfo_pitch_depth_q24(fast_lfo_depth_amt(constrain(v, 0, 255)), LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo2_to_osc2(int16_t v) {
  LFO2toOSC2_q24 = lfo_pitch_depth_q24(fast_lfo_depth_amt(v), LFO2_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo2_to_osc3(int16_t v) {
  LFO2toOSC3_q24 = lfo_pitch_depth_q24(fast_lfo_depth_amt(v), LFO2_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo2_to_osc2_coarse(int16_t v) {
  LFO2toOSC2_coarse_q24 = lfo_pitch_depth_q24(fast_lfo_depth_amt(constrain(v, 0, 511)), LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo2_to_osc3_coarse(int16_t v) {
  LFO2toOSC3_coarse_q24 = lfo_pitch_depth_q24(fast_lfo_depth_amt(constrain(v, 0, 511)), LFO1_PITCH_DEPTH_SCALE);
}

static void apply_param_lfo2_to_pw(int16_t v) { LFO2toPW = (uint16_t)v; }

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
static void apply_param_character(int16_t /*v*/) { /* Character scale hook */ }
static void apply_param_pw_value(int16_t v) { PW[0] = (uint16_t)(v >> 2); }

// =============================================================================
// 4. Envelope Modulations
// =============================================================================

static void apply_param_adsr3_to_osc_select(int16_t v) {
  ADSR3ToOscSelect = (int8_t)v;
}

static void apply_param_adsr3_to_pwm(int16_t v) {
  ADSR1toPWM = (int16_t)v - 512;
  ADSR1toPWM_scale = ADSR1toPWM;
}

static void apply_param_adsr3_to_detune1(int16_t v) {
  ADSR1toDETUNE1 = v;
  // (1 << 24) / 4095 is practically 4097. 
  // Eliminates 64-bit cast & expensive division!
  ADSR1toDETUNE1_scale_q24 = (int32_t)v * 4097; 
}

static void apply_param_adsr3_pitch_mode(int16_t v) {
  env_dco_pitch_centered = (v != 0) ? 1 : 0;
}

// =============================================================================
// 5. Calibration Controls & Storage Trims
// =============================================================================
static void dco_send_all_calibration_data() {
  // 1. Send all Manual Offsets (Param 155, 32-bit: [oscIndex:8 | offset:8])
  for (uint8_t i = 0; i < NUM_OSCILLATORS; i++) {
    uint32_t packed = ((uint32_t)i << 8) | (uint8_t)manualCalibrationOffset[i];
    serialSendParam32(PARAM_MANUAL_CALIBRATION_OFFSET_FROM_DCO, packed);
  }

  // 2. Send all 440 Hz Amp Comps (Param 159, 32-bit: [oscIndex:8 | amp440:16])
  for (uint8_t i = 0; i < NUM_OSCILLATORS; i++) {
    uint32_t packed = ((uint32_t)i << 16) | (uint16_t)ampComp440[i];
    serialSendParam32(PARAM_AMP_COMP_440, packed);
  }

  // 3. Send all PW Centers (Param 162, 32-bit: [voiceChannel:8 | pwCenter:16])
  for (uint8_t ch = 0; ch < 4; ch++) {
    uint32_t packed = ((uint32_t)ch << 16) | (uint16_t)PW_CENTER[ch];
    serialSendParam32(PARAM_CAL_PW_CENTER, packed);
  }

  // 4. Send all Scope 50% Duty Trims (Param 161, 32-bit: [oscIndex:8 |
  // dutyOffset:16])
  for (uint8_t i = 0; i < NUM_OSCILLATORS; i++) {
    uint32_t packed = ((uint32_t)i << 16) | (uint16_t)ampCompDutyOffset[i];
    serialSendParam32(PARAM_AMP_COMP_DUTY_OFFSET, packed);
  }
}

static void apply_param_manual_calibration_flag(int16_t v) {
  manualCalibrationFlag = (v != 0);
  calibrationFlag = (v != 0);
  if (v != 0) {
    // Burst entire calibration bank to Input Controller
    dco_send_all_calibration_data();
  }
}

static void apply_param_calibration_flag(int16_t v) {
  // 0 = Cancel running calibration
  if (v == 0) {
    calibrationCancelRequested = true;
    calibrationFlag = false;
    return;
  }

  // 1. Decode Precision (+4 = Fine, +8 = Fast)
  uint8_t scopeVal = (uint8_t)v;
  if (scopeVal >= 8) {
    calibrationPrecision = CAL_PRECISION_FAST;
    scopeVal -= 8;
  } else if (scopeVal >= 4) {
    calibrationPrecision = CAL_PRECISION_FINE;
    scopeVal -= 4;
  } else {
    calibrationPrecision = CAL_PRECISION_NORMAL;
  }

  // 2. Decode Scope (1 = Amp only, 2 = PW only, 3 = Full)
  if (scopeVal == 1) {
    calibrationScope = CAL_SCOPE_AMP;
  } else if (scopeVal == 2) {
    calibrationScope = CAL_SCOPE_PW;
  } else {
    calibrationScope = CAL_SCOPE_FULL;
  }

  // 3. Clear cancel flag and arm auto-calibration
  calibrationCancelRequested = false;
  calibrationFlag = true;
}

static void apply_param_manual_calibration_stage(int16_t v) {
  manualCalibrationStage = (uint8_t)v;
  manualCalibrationStep =
      cal_stage_is_440_n(manualCalibrationStage, NUM_OSCILLATORS) ? 1 : 0;
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
    ampComp440[osc] =
        (uint16_t)constrain(v, AMP_COMP_440_MIN, AMP_COMP_440_MAX);
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
// 6. Diagnostic & Debug Commands (PARAM_DEBUG_COMMAND = 160)
// =============================================================================

static void apply_param_debug_command(int16_t v) {
  // Packed Character setters (0xC8xx, 0xCAxx, 0xCBxx)
  uint8_t hi = (uint8_t)((uint16_t)v >> 8);
  uint8_t lo = (uint8_t)((uint16_t)v & 0xFF);
  if (hi == 0xC8) {
    ampCompJitter = lo;
    return;
  }
  if (hi == 0xCA) {
    pitchJitter = lo;
    return;
  }
  if (hi == 0xCB) {
    pulsewidthJitter = lo;
    return;
  }

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

#ifdef RUNNING_AVERAGE
  case 10:
    bench_dump_request = true;
    break;
  case 11:
    bench_reset_all();
    break;
  case 12:
    bench_periodic = !bench_periodic;
    break;
#endif

#ifdef ENABLE_MEM_DIAG
  case 13:
    mem_diag_request();
    break;
  case 14:
    mem_diag_runtime_enabled = false;
    break;
  case 15:
    mem_diag_runtime_enabled = true;
    break;
#endif

  case 30:
    seed_fake_calibration_tables(true);
    break;
  case 34:
    autotuneAmpMethod = 0;
    break;
  case 35:
    autotuneAmpMethod = 1;
    break;
  case 36:
    calibrationVerifyRequested = true;
    break;
  case 37:
    autotuneSearchMode = 0;
    break;
  case 38:
    autotuneSearchMode = 1;
    break;
  case 39:
    autotuneSearchMode = 2;
    break;
  case 40:
    autotuneAmp0Mode = 0;
    break;
  case 41:
    autotuneAmp0Mode = 1;
    break;
  case 46:
    pwCvProbeRequested = true;
    break;

  // Forward Mainboard-specific debug commands over Serial2
  case 42:
  case 43:
  case 44:
  case 45:
    serialSendParam16(PARAM_DEBUG_COMMAND, v);
    break;

    // --- RESET & BOOTLOADER COMMANDS (RP2040 / RP2350) ---
  case 90: // Software Reset / Reboot
    Serial.println("[mcu] Rebooting system...");
    Serial.flush();
    delay(30);

// Stop Core 1 if running audio/PIO tasks to prevent memory faults during reset
#if defined(PICO_MULTICORE) || defined(ARDUINO_ARCH_RP2040)
    multicore_reset_core1();
#endif

// Trigger hardware reboot
#if defined(ARDUINO_ARCH_RP2040)
    rp2040.reboot();
#else
    watchdog_reboot(0, 0, 0);
#endif
    break;

  case 91: // Reboot into BOOTSEL mode (UF2 upload)
    Serial.println("[mcu] Entering BOOTSEL mode...");
    Serial.flush();
    delay(30);

// Stop Core 1 before handing over to ROM bootloader
#if defined(PICO_MULTICORE) || defined(ARDUINO_ARCH_RP2040)
    multicore_reset_core1();
#endif

// Jump to ROM USB Bootloader
#if defined(ARDUINO_ARCH_RP2040)
    rp2040.rebootToBootloader();
#else
    reset_usb_boot(0, 0);
#endif
    break;
  default:
    break;
  }
}

// =============================================================================
// 7. Preset Store, Calibration Dump & Recall
// =============================================================================

static void apply_param_preset_save(int16_t v) {
  preset_store_save((uint8_t)v);
}
static void apply_param_preset_load(int16_t v) {
  preset_store_load((uint8_t)v);
}
static void apply_param_preset_dump(int16_t v) {
  preset_store_dump((int16_t)v);
}
static void apply_param_cal_dump(int16_t v) { preset_store_cal_dump(v); }
static void apply_param_ui_preset_scroll(int16_t v) {
  const uint8_t slot = (uint8_t)v;
  serial_send_preset_scroll_to_mb(slot);
  serial_send_preset_loaded_to_mb(slot);
}

// =============================================================================
// 8. Router Table & Jump Setup
// =============================================================================

static const ParamDescriptorT<int16_t> paramTable[] = {
    {PARAM_OSC1_PULSE_ENABLE, apply_param_osc1_pulse_enable},
    {PARAM_OSC1_INTERVAL, apply_param_osc1_interval},
    {PARAM_OSC2_INTERVAL, apply_param_osc2_interval},
    {PARAM_OSC3_INTERVAL, apply_param_osc3_interval},
    {PARAM_OSC2_DETUNE_VAL, apply_param_osc2_detune},
    {PARAM_OSC3_DETUNE_VAL, apply_param_osc3_detune},
    {PARAM_UNISON_DETUNE, apply_param_unison_detune},
    {PARAM_PORTAMENTO_TIME, apply_param_portamento_time},
    {PARAM_PORTAMENTO_MODE, apply_param_portamento_mode},
    {PARAM_VOICE_MODE, apply_param_voice_mode},
    {PARAM_VOICE_ALLOC_MODE, apply_param_voice_alloc_mode},
    {PARAM_SYNC_MODE, apply_param_sync_mode},
    {PARAM_SOFT_SYNC, apply_param_soft_sync},
    {PARAM_SUBOSC_DIVIDE, apply_param_subosc_divide},

    {PARAM_LFO1_WAVEFORM, apply_param_lfo1_waveform},
    {PARAM_LFO2_WAVEFORM, apply_param_lfo2_waveform},
    {PARAM_LFO1_SPEED, apply_param_lfo1_speed},
    {PARAM_LFO2_SPEED, apply_param_lfo2_speed},
    {PARAM_LFO1_TO_DCO, apply_param_lfo1_to_dco},
    {PARAM_LFO1_TO_OSC1, apply_param_lfo1_to_osc1},
    {PARAM_LFO1_TO_OSC2, apply_param_lfo1_to_osc2},
    {PARAM_LFO1_TO_OSC3, apply_param_lfo1_to_osc3},
    {PARAM_LFO2_TO_OSC2, apply_param_lfo2_to_osc2},
    {PARAM_LFO2_TO_OSC3, apply_param_lfo2_to_osc3},
    {PARAM_LFO2_TO_OSC2_COARSE, apply_param_lfo2_to_osc2_coarse},
    {PARAM_LFO2_TO_OSC3_COARSE, apply_param_lfo2_to_osc3_coarse},
    {PARAM_LFO2_TO_PW, apply_param_lfo2_to_pw},

    {PARAM_ANALOG_DRIFT_AMOUNT, apply_param_analog_drift_amount},
    {PARAM_ANALOG_DRIFT_SPEED, apply_param_analog_drift_speed},
    {PARAM_ANALOG_DRIFT_SPREAD, apply_param_analog_drift_spread},
    {PARAM_CHARACTER, apply_param_character},
    {PARAM_PW_VALUE, apply_param_pw_value},

    {PARAM_ADSR3_TO_OSC_SELECT, apply_param_adsr3_to_osc_select},
    {PARAM_ADSR3_TO_PWM, apply_param_adsr3_to_pwm},
    {PARAM_ADSR3_TO_DETUNE1, apply_param_adsr3_to_detune1},
    {PARAM_ADSR3_PITCH_MODE, apply_param_adsr3_pitch_mode},

    {PARAM_CALIBRATION_FLAG, apply_param_calibration_flag},
    {PARAM_MANUAL_CALIBRATION_FLAG, apply_param_manual_calibration_flag},
    {PARAM_MANUAL_CALIBRATION_STAGE, apply_param_manual_calibration_stage},
    {PARAM_MANUAL_CALIBRATION_OFFSET, apply_param_manual_calibration_offset},
    {PARAM_MANUAL_CALIBRATION_STEP, apply_param_manual_calibration_step},
    {PARAM_AMP_COMP_440, apply_param_amp_comp_440},
    {PARAM_CAL_PW_CENTER, apply_param_cal_pw_center},
    {PARAM_AMP_COMP_DUTY_OFFSET, apply_param_amp_comp_duty_offset},
    {PARAM_MANUAL_CALIBRATION_STORE, apply_param_manual_calibration_store},

    {PARAM_PRESET_SAVE, apply_param_preset_save},
    {PARAM_PRESET_LOAD, apply_param_preset_load},
    {PARAM_PRESET_DUMP, apply_param_preset_dump},
    {PARAM_CAL_DUMP, apply_param_cal_dump},
    {PARAM_UI_PRESET_SCROLL, apply_param_ui_preset_scroll},
    {PARAM_DEBUG_COMMAND, apply_param_debug_command},
};

// =============================================================================
// 8. Calibration Parameter Whitelist & Router Entry Point
// =============================================================================

// Checks if an incoming parameter ID is exclusively meant for calibration
static inline bool is_calibration_parameter(uint8_t id) {
  switch (id) {
    case PARAM_CALIBRATION_FLAG:
    case PARAM_MANUAL_CALIBRATION_FLAG:
    case PARAM_MANUAL_CALIBRATION_STAGE:
    case PARAM_MANUAL_CALIBRATION_OFFSET:
    case PARAM_MANUAL_CALIBRATION_STORE:
    case PARAM_MANUAL_CALIBRATION_STEP:
    case PARAM_AMP_COMP_440:
    case PARAM_AMP_COMP_DUTY_OFFSET:
    case PARAM_CAL_PW_CENTER:
    case PARAM_DEBUG_COMMAND:
      return true;
    default:
      return false;
  }
}

static void (*dcoParamJump[PARAM_ROUTER_JUMP_SIZE])(int16_t) = {nullptr};

void init_param_router() {
  param_router_build_jump(dcoParamJump, paramTable,
                          sizeof(paramTable) / sizeof(paramTable[0]));
}

void __not_in_flash_func(update_parameters)(uint8_t id, int16_t value) {
  // =========================================================================
  // CALIBRATION PARAMETER GATING (Prevents pot noise, LFO, and preset leaks)
  // =========================================================================
  if (calibrationFlag) {
    // 1. Auto-Calibration running: ONLY allow cancel or debug triggers
    if (!manualCalibrationFlag) {
      if (id == PARAM_CALIBRATION_FLAG || id == PARAM_DEBUG_COMMAND) {
        param_router_apply(dcoParamJump, id, value);
      }
      return; // DROP all pot moves, preset loads, and synth controls
    }

    // 2. Manual Calibration UI active: ONLY allow calibration UI parameters
    if (!is_calibration_parameter(id)) {
      return; // DROP normal synth pots (PW knob, LFO rates/depths, Cutoff, etc.)
    }
  }

  // Normal parameter execution
  param_router_apply(dcoParamJump, id, value);
  preset_shadow_capture(id, value);
}