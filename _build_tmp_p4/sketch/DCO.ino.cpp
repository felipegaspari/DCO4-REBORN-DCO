#include <Arduino.h>
#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/DCO.ino"
// #include <stdint.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <math.h>

/*  *** TO DO ***
- Fix PULSE PWN not received or updated when loading patches.
- Ask the AI to optimize and clean the autotune code. 
*/

// #define RUNNING_AVERAGE

#ifdef RUNNING_AVERAGE
#include "RunningAverage.h"
// Note: RunningAverage objects for voice_task timing are defined in voices.ino
#endif

// ---------------------------------------------------------------------------
// Voice engine build options
// ---------------------------------------------------------------------------
// High-level engine selection:
// - For RP2040 (no FPU): comment this out to use the fixed-point engine.
// - For RP2350 (with FPU): leave defined to use the float-based engine.
#define USE_FLOAT_ENGINE

// Phase 1 hub: uncomment to make Serial2 (GP20/21) speak Input protocol instead
// of Mainboard. MIDI stays on Serial1 GP0/1. See docs/MAINBOARD_ABSORPTION.md.
// #define ENABLE_INPUT_UART

// Phase 4: Screen UART (SerialPIO GP8 TX / GP9 RX @ 2.5M). Gap 'x' goes direct
// to Screen; pair with ENABLE_INPUT_UART for full hub (cal offsets → Input).
// #define ENABLE_SCREEN_UART

// Phase 3 CV hardware (provisional pins in globals.h / docs/PINOUT.md).
// Leave commented on benches without filter/VCA/mux/DAC attached.
// #define ENABLE_CV_OUTS
// #define ENABLE_WAVE_MUX
// #define ENABLE_MCP4728

// Derived switches for the different subsystems:
#ifdef USE_FLOAT_ENGINE
  // Use float-based voice task (pitch path, modifiers, clock-divider, etc.)
  #define USE_FLOAT_VOICE_TASK
  // Use float-based amplitude compensation (pure Hz domain).
  #define USE_FLOAT_AMP_COMP
#endif


// ---------------------------------------------------------------------------
// RP2040 or fixed point engine specific settings
  // Pitch interpolation mode:
  #define PITCH_USE_RATIO_Q16 1 // Uncomment this to use Q16 for pitch interpolation. dEFAULT mode.

  // IF PITCH_USE_RATIO_Q16 IS NOT DEFINED, THEN:
    // Use Q12 for pitch interpolation. Q12 is a good compromise between accuracy and speed.
    // This mostly affects the multiplier table interpolation (pitch bend, detune, unison, ADSR, drift etc.) applied to frequency.
    // Higher precision means smaller stepping when modulating frequency.
    //
    // #ifdef PITCH_INTERP_USE_Q8_ 32-bit friendly path: slope in Q8, delta in Q8; total 16 frac bits
    // #ifdef PITCH_INTERP_USE_Q12: enables medium-precision path: slope in Q12, delta in Q12; total 24 frac bits
    // else: enables high-precision path: slope in Q20, delta in Q16
  #define PITCH_INTERP_USE_Q12  // Uncomment this to use Q12 for pitch interpolation WHEN PITCH_USE_RATIO_Q16 IS NOT DEFINED.
  //#define PITCH_INTERP_USE_Q8 // Uncomment this to use Q8 for pitch interpolation WHEN PITCH_USE_RATIO_Q16 IS NOT DEFINED.
  
  
  
  // Select clock-divider precision mode for the fixed-point path:
  // 0 = fast 32-bit fixed-point, 1 = high-precision 64bit integer division
  // High precision is preferred for better accuracy at low frequencies, but it is much slower than fixed point. 
  // High precision is the default method, at 4uS per voice. Fixed-point takes 1uS per voice.
  // The fixed-point method is there in case I want to try some crazy fast modulation, or to move the project to a much slower processor.
#define HIGH_PRECISION_CLKDIV 1

// Uncomment to benchmark float vs double clock-divider calculations in voice_task_float:
// #define CLKDIV_BENCHMARK

#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
//#include "tusb_config.h"

#include "pico/stdlib.h"
// #include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico-dco.pio.h"
#include "hardware/pwm.h"
// #include "hardware/spi.h"

#include "LittleFS.h"
// #include <SingleFileDrive.h>
// #include <EEPROM.h>

#include <stdint.h>
#include "params_def.h"
#include "param_router.h"

#include "globals.h"
#include "cv_state.h"
#include "cv_out.h"

#include "FS.h"

#include "noteList.h"
#include "amp_comp.h"

#include "Serial.h"
#include "midi.h"
#include "voices.h"
#include "state_machines.h"
#include "PWM.h"
#include "utils.h"
#include "Timer_millis.h"

#include "LFO.h"
#include "adsr.h"
#include "wave_mux.h"
#include "mcp4728_dco.h"

#include "PID.h"
#include "autotune.h"


#ifdef RUNNING_AVERAGE
RunningAverage ra_loop1_ADSR_and_detune(2000);
RunningAverage ra_loop0_LFOs(2000);
RunningAverage ra_loop0_DRIFT_LFOs(2000);
RunningAverage ra_loop0_MIDI_and_serial(2000);
RunningAverage ra_loop0_memcpy(2000);

#endif

// ****************************************************************************************** //

// Core 0 boot: serial, MIDI, LFOs, board fix pins, USB descriptors, calibration input pin.
#line 134 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/DCO.ino"
void setup();
#line 165 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/DCO.ino"
void setup1();
#line 196 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/DCO.ino"
void loop();
#line 246 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/DCO.ino"
void loop1();
#line 4 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/FS.ino"
void init_FS();
#line 148 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/FS.ino"
void update_FS_voice(byte voiceN);
#line 171 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/FS.ino"
void update_FS_PWCenter(byte voiceN, uint16_t value);
#line 184 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/FS.ino"
void update_FS_PW_High_Limit(byte voiceN, uint16_t value);
#line 197 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/FS.ino"
void update_FS_PW_Low_Limit(byte voiceN, uint16_t value);
#line 211 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/FS.ino"
void update_FS_ManualCalibrationOffset(byte oscIndex, int8_t value);
#line 2 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/LFO.ino"
void init_LFOs();
#line 8 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/LFO.ino"
void init_DRIFT_LFOs();
#line 15 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/LFO.ino"
void init_DRIFT_LFO(lfo &LFO, int CC, byte LFONumber);
#line 26 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/LFO.ino"
void init_LFO1();
#line 36 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/LFO.ino"
void init_LFO2();
#line 45 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/LFO.ino"
void LFO1();
#line 55 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/LFO.ino"
void LFO2();
#line 67 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/LFO.ino"
void DRIFT_LFOs();
#line 5 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
static double compute_gap_tolerance_for_freq(double freqHz, double dutyErrorFraction);
#line 18 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
static bool did_sign_change(float previous, float current);
#line 31 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
static float measure_gap_for_amp(uint16_t ampPwm);
#line 53 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
static void update_best_from_neighbours( int rangeSamples, const float* lowerMeasurements, const uint16_t* lowerVoltages, const float* higherMeasurements, const uint16_t* higherVoltages, float avgValue, float& closestToZero, uint16_t& bestAmpComp, uint16_t currentAmpCompCalibrationVal );
#line 86 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
static void step_amp_from_error(float avgValue, double tolerance, uint16_t& currentAmpCompCalibrationVal);
#line 108 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
static uint16_t compute_initial_amp_for_note( const DCOCalibrationContext& ctx, int j );
#line 137 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
static void store_note_result( DCOCalibrationContext& ctx, int j, uint16_t bestAmpComp, float closestToZero );
#line 156 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
void init_PID();
#line 169 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
float find_highest_freq();
#line 227 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
float find_lowest_freq();
#line 292 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
void calibrate_DCO(DCOCalibrationContext& ctx, double dutyErrorFraction);
#line 458 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
float quadraticInterpolation(float x0, float y0, float x1, float y1, float x2, float y2, float x);
#line 471 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
uint16_t logarithmicInterpolation(float x0, float y0, float x1, float y1, float x);
#line 492 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
float linearInterpolation(float x0, float y0, float x1, float y1, float x);
#line 511 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
double expInterpolationSolveY(double x, double x0, double x1, double y0, double y1);
#line 2 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PWM.ino"
void init_pwm();
#line 28 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PWM.ino"
void init_cv_pwm();
#line 55 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PWM.ino"
void write_cv_pwm();
#line 9 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
void init_serial();
#line 132 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
static void input_handle_adsr1(char, const uint8_t* payload, uint8_t len);
#line 141 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
static void input_handle_adsr2(char, const uint8_t* payload, uint8_t len);
#line 150 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
static void input_handle_adsr3(char, const uint8_t* payload, uint8_t len);
#line 158 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
static void input_handle_filter_block(char, const uint8_t* payload, uint8_t len);
#line 167 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
static void input_handle_adsr1_to_vca(char, const uint8_t* payload, uint8_t len);
#line 173 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
static void input_handle_pw(char, const uint8_t* payload, uint8_t len);
#line 179 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
static void input_handle_param16(char, const uint8_t* payload, uint8_t len);
#line 186 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
static void input_handle_param8(char, const uint8_t* payload, uint8_t len);
#line 193 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
static void input_handle_preset_name(char, const uint8_t* payload, uint8_t len);
#line 216 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
void serial_STM32_task();
#line 237 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
void serial_send_note_on(uint8_t, uint8_t, uint8_t);
#line 238 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
void serial_send_note_off(uint8_t);
#line 242 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
static void serial_write_param32_frame(Stream& port, byte paramNumber, uint32_t paramValue);
#line 250 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
void serialSendParam32ToScreen(byte paramNumber, uint32_t paramValue);
#line 256 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
void serialSendParam32(byte paramNumber, uint32_t paramValue);
#line 2 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Timer_millis.ino"
void millisTimer();
#line 2 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/adsr.ino"
void init_ADSR();
#line 31 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/adsr.ino"
void ADSR_update();
#line 68 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/adsr.ino"
void ADSR_set_parameters();
#line 115 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/adsr.ino"
void ADSR1_set_restart();
#line 121 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/adsr.ino"
void ADSR_VCA_set_restart();
#line 127 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/adsr.ino"
void ADSR_VCF_set_restart();
#line 133 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/adsr.ino"
void ADSR1_change_curves();
#line 16 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"
static void disable_all_oscillators_and_range_pwm();
#line 47 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"
static void reset_pw_to_DIV_COUNTER_PW();
#line 58 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"
void init_DCO_calibration();
#line 107 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"
void DCO_calibration();
#line 171 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"
void restart_DCO_calibration();
#line 224 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"
static uint16_t find_PW_for_target_duty(double targetDutyFraction, uint16_t targetGap, uint16_t pwMin, uint16_t pwMax);
#line 768 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"
void find_PW_center(uint8_t mode);
#line 836 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"
PWLimitSearchResult search_PW_limit_from_center( uint8_t voiceIdx, uint16_t centerPW, PWLimitDir dir, double periodUs, double targetDuty );
#line 1043 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"
void find_PW_limit_v2(PWLimitDir dir);
#line 1145 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"
float find_gap(byte specialMode);
#line 1324 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"
void DCO_calibration_debug();
#line 4 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/cv_out.ino"
static uint16_t cv_lerp_u16(uint16_t value, uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1);
#line 10 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/cv_out.ino"
void init_cv_out();
#line 16 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/cv_out.ino"
void cv_update_mod_formulas();
#line 23 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/cv_out.ino"
void update_CV_outs();
#line 13 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/mcp4728_dco.ino"
static void mcp4728_fastWrite(uint8_t addr, uint16_t a, uint16_t b, uint16_t c, uint16_t d);
#line 27 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/mcp4728_dco.ino"
void init_MCP4728();
#line 40 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/mcp4728_dco.ino"
void mcpUpdate();
#line 3 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/midi.ino"
void init_midi();
#line 22 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/midi.ino"
void handleNoteOn(byte channel, byte pitch, byte velocity);
#line 26 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/midi.ino"
void handleNoteOff(byte channel, byte pitch, byte velocity);
#line 31 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/midi.ino"
void handleControlChange(byte channel, byte number, byte value);
#line 42 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/midi.ino"
void handleProgramChange(byte channel, byte program);
#line 46 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/midi.ino"
void handlePitchBend(byte channel, int pitchBend);
#line 51 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/midi.ino"
void note_on(uint8_t note, uint8_t velocity);
#line 139 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/midi.ino"
void note_off(uint8_t note);
#line 43 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_saw_status(int16_t v);
#line 48 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_saw2_status(int16_t v);
#line 53 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_tri_status(int16_t v);
#line 58 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_sine_status(int16_t v);
#line 64 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_sqr1_status(int16_t v);
#line 68 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_sqr2_status(int16_t v);
#line 73 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_resonance_comp(int16_t v);
#line 77 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_vca_adsr_restart(int16_t v);
#line 82 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_vcf_adsr_restart(int16_t v);
#line 88 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_adsr3_to_osc_select(int16_t v);
#line 93 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_lfo1_waveform(int16_t v);
#line 100 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_lfo2_waveform(int16_t v);
#line 107 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_osc1_interval(int16_t v);
#line 112 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_osc2_interval(int16_t v);
#line 117 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_osc3_interval(int16_t v);
#line 122 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_osc2_detune_val(int16_t v);
#line 127 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_osc3_detune_val(int16_t v);
#line 132 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_lfo2_to_detune2(int16_t v);
#line 138 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_lfo2_to_detune3(int16_t v);
#line 144 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_osc_sync_mode(int16_t v);
#line 189 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_portamento_time(int16_t v);
#line 200 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_vcf_keytrack(int16_t v);
#line 209 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_velocity_to_vcf(int16_t v);
#line 214 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_velocity_to_vca(int16_t v);
#line 219 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_sqr1_level(int16_t v);
#line 227 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_sqr2_level(int16_t v);
#line 235 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_sub_level(int16_t v);
#line 242 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_portamento_mode(int16_t v);
#line 253 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_voice_mode(int16_t v);
#line 259 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_unison_detune(int16_t v);
#line 264 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_analog_drift_amount(int16_t v);
#line 269 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_analog_drift_speed(int16_t v);
#line 281 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_analog_drift_spread(int16_t v);
#line 293 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_sync_mode(int16_t v);
#line 299 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_lfo1_to_dco(int16_t v);
#line 309 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_lfo1_speed(int16_t v);
#line 316 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_lfo2_speed(int16_t v);
#line 322 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_vca_level(int16_t v);
#line 326 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_lfo1_to_vca(int16_t v);
#line 332 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_lfo2_to_pw(int16_t v);
#line 337 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_adsr1_to_pwm(int16_t v);
#line 342 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_adsr1_to_detune1(int16_t v);
#line 365 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_adsr1_attack_curve(int16_t v);
#line 369 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_adsr1_decay_curve(int16_t v);
#line 373 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_adsr2_attack_curve(int16_t v);
#line 377 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_adsr2_decay_curve(int16_t v);
#line 382 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_pwm_pots_manual(int16_t v);
#line 386 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_adsr3_enabled(int16_t v);
#line 399 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_calibration_flag(int16_t v);
#line 404 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_manual_calibration_flag(int16_t v);
#line 422 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_manual_calibration_stage(int16_t v);
#line 430 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
static void apply_param_manual_calibration_offset(int16_t v);
#line 510 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
void update_parameters(byte paramNumber, int16_t paramValue);
#line 2 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/state_machines.ino"
void init_pio();
#line 14 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/state_machines.ino"
void start_voice_sms();
#line 57 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/state_machines.ino"
void init_sm_sync(PIO pio, uint sm, uint offset, uint pin, uint pin2);
#line 3 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/utils.ino"
uint16_t linearToLogarithmic(uint16_t linearValue, float base, uint16_t maxValue);
#line 16 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/utils.ino"
float expConverterFloat(uint16_t readingValue, uint16_t curve);
#line 26 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/utils.ino"
uint16_t expConverter(uint16_t readingValue, uint16_t curve);
#line 30 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
void init_voices();
#line 43 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
static int64_t noteQ16_to_freqQ24(int32_t note_q16);
#line 75 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
static int64_t float_to_q24(float f);
#line 81 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
static float noteIndex_to_freqFloat(float noteIndex);
#line 873 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
void voice_task_main();
#line 883 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
void voice_task_float();
#line 1549 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
uint8_t get_free_voice_sequential();
#line 1591 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
uint8_t get_free_voice();
#line 1615 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
void setVoiceMode();
#line 1634 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
void setSyncMode();
#line 1771 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
uint16_t get_chan_level_float(float freqHz, uint8_t voiceN);
#line 1813 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
uint16_t get_PW_level_interpolated(uint16_t PWval, uint8_t voiceN);
#line 1848 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
void voice_task_autotune(uint8_t taskAutotuneVoiceMode, uint16_t calibrationValue);
#line 1935 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
int32_t interpolatePitchMultiplierIntQ16_cached(int32_t xQ16, int dcoIndex);
#line 1997 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
int32_t interpolateRatioQ16_cached(int32_t xQ16, int dcoIndex);
#line 2053 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
float interpolateRatioFloat_cached(float x, int dcoIndex);
#line 2113 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
void initMultiplierTables();
#line 14 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/wave_mux.ino"
static void waveMuxWritePin(uint8_t pin, bool high);
#line 23 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/wave_mux.ino"
static void waveMuxShiftOut();
#line 36 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/wave_mux.ino"
void init_waveSelector();
#line 48 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/wave_mux.ino"
void update_waveSelector(byte wave);
#line 134 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/DCO.ino"
void setup() {
  //set_sys_clock_khz(sysClock, true);
  // EEPROM.begin(512);
  init_serial();
  init_midi();

  init_LFOs();
  init_DRIFT_LFOs();

  
  // init_tuner();
  // init_tuning_tables();

  pinMode(23, OUTPUT);
  digitalWrite(23, HIGH);

  pinMode(24, OUTPUT);  // Fix pin on DCO BOARD
  digitalWrite(24, HIGH);

  USBDevice.setManufacturerDescriptor("FELA         ");
  USBDevice.setProductDescriptor("DCO3-MONO   ");

  pinMode(DCO_calibration_pin, INPUT_PULLUP);

  // gpio_init(11);
  // gpio_set_dir(11, GPIO_IN);
  // gpio_pull_down(11);
}

// Core 1 boot: PID, LittleFS cal load, ADSR, amp-comp precompute, PWM/PIO, voices.
// Clears calibrationFlag so the init_DCO_calibration block below is currently unreachable.
void setup1() {

  //set_sys_clock_khz(sysClock, true);

  init_PID();

  init_FS();

  init_ADSR();
  init_cv_out();
  init_waveSelector();
  init_MCP4728();

  // Select amplitude-compensation precompute based on engine type.
  precompute_amp_comp_for_engine();

  calibrationFlag = false;
  manualCalibrationFlag = false;
  firstTuneFlag = true;

  init_pwm();
  init_pio();
  init_voices();

  if (calibrationFlag == true) {
    init_DCO_calibration();
    voice_task_autotune(0, ampCompCalibrationVal);
  }
}

// Core 0 forever loop: MIDI read, Serial2 parser, LFO1; ~100 µs LFO2 + drift + FIFO push of detune.
void loop() {
  // unsigned long loop0_start_time = micros();
  // unsigned long loop0_total_time;
  loop0_micros = micros();

  MIDI_USB.read();
  MIDI_SERIAL.read();
  serial_STM32_task();

  LFO1();

#ifdef RUNNING_AVERAGE
ra_loop0_MIDI_and_serial.addValue((float)(micros() - loop0_micros));
#endif

  if ((loop0_micros - loop0_microsLast) > 100) {
#ifdef RUNNING_AVERAGE
    unsigned long t_loop0_LFOs = micros();
#endif

    LFO2();

    #ifdef RUNNING_AVERAGE
    ra_loop0_LFOs.addValue((float)(micros() - t_loop0_LFOs));
    unsigned long t_loop0_DRIFT_LFOs = micros();
#endif

    DRIFT_LFOs();
    #ifdef RUNNING_AVERAGE
    ra_loop0_DRIFT_LFOs.addValue((float)(micros() - t_loop0_DRIFT_LFOs));
    unsigned long t_loop0_memcpy = micros();
#endif

    // Transfer LFO1 detune modulation as a raw Q24 fixed-point integer via FIFO.
    rp2040.fifo.push_nb((uint32_t)DETUNE_INTERNAL_q24);

#ifdef RUNNING_AVERAGE
    ra_loop0_memcpy.addValue((float)(micros() - t_loop0_memcpy));
#endif

    loop0_microsLast = loop0_micros;
    // Serial.println((String)"a" + (micros() - a));
  }
  // loop0_total_time = micros() - loop0_start_time;
  // if (loop0_total_time > 10) {
  //// Serial.println(loop0_total_time);
  // }
}

// Core 1 forever loop: soft timers; auto/manual calibration OR ADSR + FIFO pop + voice_task_main.
void loop1() {
  // unsigned long loop1_start_time = micros();
  // unsigned long loop1_total_time;
  millisTimer();

  if (calibrationFlag == true) {
    if (manualCalibrationFlag == true) {
      VOICE_NOTES[0]               = manual_DCO_calibration_start_note;
      DCO_calibration_current_note = manual_DCO_calibration_start_note;
      ampCompCalibrationVal = initManualAmpCompCalibrationValPreset + manualCalibrationOffset[manualCalibrationStage];
      voice_task_autotune(0, ampCompCalibrationVal);
      // In manual calibration mode, continuously measure and report the duty
      // difference so the screen can display live feedback for the user.
      DCO_calibration_debug();
      //Serial.println((String) "PW value: " + (PW[0] / 4));

    } else {
      DCO_calibration();
    }
  } else {

    loop1_micros = micros();

    if ((loop1_micros - loop1_microsLast) > 100) {
#ifdef RUNNING_AVERAGE
      unsigned long t_loop1_ADSR_and_detune = micros();
#endif
      ADSR_update();
      update_CV_outs();

#ifdef RUNNING_AVERAGE
      ra_loop1_ADSR_and_detune.addValue((float)(micros() - t_loop1_ADSR_and_detune));
#endif
      loop1_microsLast = loop1_micros;
    }
    
          // Receive Q24 detune value from core 0; reinterpret raw bits back to signed.
          rp2040.fifo.pop_nb(detune_fifo_variable);
          DETUNE_INTERNAL_FIFO_q24 = (int32_t)DETUNE_INTERNAL_FIFO;

    // loop speed
    //  loop1_start_time = micros();
    // Serial.println("pre voice task");
    voice_task_main();
    // Serial.println("post voice task");
    // loop speed
    // loop1_total_time = micros() - loop1_start_time;
    //  if (loop1_total_time > 50) {
    // Serial.println(loop1_total_time);
    //  }
    // Serial.println("loop1");
  }

  #ifdef RUNNING_AVERAGE
  if (timer1000msFlag) {
    print_running_averages();
  }
  #endif
}

#ifdef RUNNING_AVERAGE
// Debug: print Core0/Core1 timing averages (~1 Hz when timer1000msFlag). Calls print_voice_task_timings().
void print_running_averages() {
  Serial.println("--------------------------------");
  Serial.println("RUNNING AVERAGES");
  Serial.println("--------------------------------");
  Serial.println("Loop0");
  Serial.println((String) "Loop0 MIDI and Serial: " + ra_loop0_MIDI_and_serial.getFastAverage());
  Serial.println((String) "Loop0 LFOs: " + ra_loop0_LFOs.getFastAverage());
  Serial.println((String) "Loop0 DRIFT LFOs: " + ra_loop0_DRIFT_LFOs.getFastAverage());
  Serial.println((String) "Loop0 memcpy: " + ra_loop0_memcpy.getFastAverage());
  Serial.println("Loop1");
  Serial.println((String) "Loop1 ADSR and Detune: " + ra_loop1_ADSR_and_detune.getFastAverage());



  print_voice_task_timings();
}
#endif
#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/FS.ino"
#include "include_all.h"
// Mount LittleFS and load amp-comp / PW / offset calibration into runtime arrays (float or Q8).
// Called from setup1() and again at end of DCO_calibration().
void init_FS() {
  LittleFS.begin();

  if (!LittleFS.exists("voiceTables")) {
    fileVoiceTablesFS = LittleFS.open("voiceTables", "w+");
  } else {
    fileVoiceTablesFS = LittleFS.open("voiceTables", "r");
  }

#ifdef ENABLE_FS_CALIBRATION

  fileVoiceTablesFS.read(voiceTablesBankBuffer, FSBankSize);
  fileVoiceTablesFS.close();


    for (int i = 0; i < (chanLevelVoiceDataSize * NUM_OSCILLATORS); i++) {
     freq_to_amp_comp_array[i] = (int32_t(voiceTablesBankBuffer[i * 4 + 3]) << 24) |
                    (int32_t(voiceTablesBankBuffer[i * 4 + 2]) << 16) |
                    (int32_t(voiceTablesBankBuffer[i * 4 + 1]) << 8) |
                    int32_t(voiceTablesBankBuffer[i * 4 ]);
  }

    for (int datasetIndex = 0; datasetIndex < NUM_OSCILLATORS; ++datasetIndex) {
        for (int pairIndex = 0; pairIndex < chanLevelVoiceDataSize / 2; ++pairIndex) {
            int rawIndex = datasetIndex * chanLevelVoiceDataSize + pairIndex * 2;

        // Stored frequencies are in Hz*100.
            int32_t freq_x100 = freq_to_amp_comp_array[rawIndex];

#ifdef USE_FLOAT_AMP_COMP
        // Float engine: convert directly to Hz for the pure-float amp-comp path.
        float freqHz = (float)freq_x100 / 100.0f;
        ampCompFrequencyHz[datasetIndex][pairIndex] = freqHz;
        // Level is shared between fixed and float paths.
        ampCompArray[datasetIndex][pairIndex] = freq_to_amp_comp_array[rawIndex + 1];
#else
        // Fixed-point engine: convert to fixed-point Hz (Hz * 2^FREQ_FRAC_BITS).
        int64_t scaled = (int64_t)freq_x100 * (1LL << FREQ_FRAC_BITS);  // use 64-bit to avoid overflow
            int32_t freq_fx = (scaled >= 0)
                            ? (int32_t)((scaled + 50LL) / 100LL)        // round to nearest
                                : (int32_t)(-((( -scaled) + 50LL) / 100LL));
            ampCompFrequencyArray[datasetIndex][pairIndex] = freq_fx;
        ampCompArray[datasetIndex][pairIndex]          = freq_to_amp_comp_array[rawIndex + 1];
#endif
        }
    }


  uint8_t highestNoteFound = 255;
  // for (int i = 0; i < NUM_OSCILLATORS; i++) {
  //   highestOSCNote[i] =
  //     if (highestOSCNote[i] < highestNoteFound) {
  //     highestNoteFound = highestOSCNote[i];
  //   }
  // }

  // PW CALIBRATION VALUES VALUES FROM FS
  // PW_CENTER
  if (!LittleFS.exists("PWCenter")) {
    filePWCenterFS = LittleFS.open("PWCenter", "w+");
  } else {
    filePWCenterFS = LittleFS.open("PWCenter", "r");
  }

  filePWCenterFS.read(PWCenterBankBuffer, FSPWBankSize);
  filePWCenterFS.close();

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    uint16_t uint16Data;
    for (int j = 0; j < FSPWDataSize; j++) {
      ((uint8_t *)&uint16Data)[j] = PWCenterBankBuffer[i * 2 + j];
    }

    PW_CENTER[i] = (uint16_t)uint16Data;

    // delay(1000);
    // Serial.println((String) "PW_CENTER " + i + (String) ": " + uint16Data);
  }
  // PW_HIGH_LIMIT
    if (!LittleFS.exists("PWHighLimit")) {
    filePWHighLimitFS = LittleFS.open("PWHighLimit", "w+");
  } else {
    filePWHighLimitFS = LittleFS.open("PWHighLimit", "r");
  }

  filePWHighLimitFS.read(PWHighLimitBankBuffer, FSPWBankSize);
  filePWHighLimitFS.close();

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    uint16_t uint16Data;
    for (int j = 0; j < FSPWDataSize; j++) {
      ((uint8_t *)&uint16Data)[j] = PWHighLimitBankBuffer[i * 2 + j];
    }
    PW_HIGH_LIMIT[i] = (uint16_t)uint16Data;

    // // Debug:
    // // Serial.println((String) "PW_HIGH_LIMIT " + i + (String) ": " + uint16Data);
  } 
  // PW_LOW_LIMIT
  if (!LittleFS.exists("PWLowLimit")) {
    filePWLowLimitFS = LittleFS.open("PWLowLimit", "w+");
  } else {
    filePWLowLimitFS = LittleFS.open("PWLowLimit", "r");
  }

  filePWLowLimitFS.read(PWLowLimitBankBuffer, FSPWBankSize);
  filePWLowLimitFS.close();

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    uint16_t uint16Data;
    for (int j = 0; j < FSPWDataSize; j++) {
      ((uint8_t *)&uint16Data)[j] = PWLowLimitBankBuffer[i * 2 + j];
    }
    PW_LOW_LIMIT[i] = (uint16_t)uint16Data;

    // delay(1000);
    // Serial.println((String) "PW_LOW_LIMIT " + i + (String) ": " + uint16Data);
  }

  // Manual calibration offsets (one signed byte per oscillator).
  if (!LittleFS.exists("ManualOffset")) {
    fileManualOffsetFS = LittleFS.open("ManualOffset", "w+");
    // Initialise FS with zeros so future reads are defined.
    for (int i = 0; i < FSManualOffsetBankSize; ++i) {
      ManualOffsetBankBuffer[i] = 0;
    }
    fileManualOffsetFS.write(ManualOffsetBankBuffer, FSManualOffsetBankSize);
  } else {
    fileManualOffsetFS = LittleFS.open("ManualOffset", "r");
    fileManualOffsetFS.read(ManualOffsetBankBuffer, FSManualOffsetBankSize);
  }
  fileManualOffsetFS.close();

  // Copy stored offsets into the runtime array.
  for (int osc = 0; osc < NUM_OSCILLATORS; ++osc) {
    manualCalibrationOffset[osc] = (int8_t)ManualOffsetBankBuffer[osc];
  }

#endif

  //singleFileDrive.begin("voiceTables", "voicetables.txt");
}

// Persist one oscillator's calibrationData slice into voiceTables. Called from DCO_calibration().
void update_FS_voice(byte voiceN) {
  byte calibrationDataBytes[FSVoiceDataSize];

  // Serialize calibrationData (uint32_t pairs: [freq_x100, pwm]) for this voice
  // into a contiguous byte buffer. Each entry is written little-endian.

  for (int i = 0; i < chanLevelVoiceDataSize; i++) {
    // freq_to_amp_comp_array[i + (voiceN * chanLevelVoiceDataSize)] = calibrationData[i]; // can be used for in-RAM updates if desired
    byte *b = (byte *)&calibrationData[i];
    for (int j = 0; j < 4; j++) {
      calibrationDataBytes[i * 4 + j] = b[j];
    }
  }
  uint16_t startByteN = voiceN * FSVoiceDataSize;

  fileVoiceTablesFS = LittleFS.open("voiceTables", "r+");
  fileVoiceTablesFS.seek(startByteN);
  fileVoiceTablesFS.write(calibrationDataBytes, FSVoiceDataSize);
  fileVoiceTablesFS.close();
}


// Persist PW center for a voice. Called from find_PW_center().
void update_FS_PWCenter(byte voiceN, uint16_t value) {
  byte calibrationDataBytes[FSPWDataSize];
  byte *b = (byte *)&value;

  uint16_t startByteN = voiceN * FSPWDataSize;

  filePWCenterFS = LittleFS.open("PWCenter", "r+");
  filePWCenterFS.seek(startByteN);
  filePWCenterFS.write(b, FSPWDataSize);
  filePWCenterFS.close();
}

// Persist PW high limit for a voice. Called from find_PW_limit_v2().
void update_FS_PW_High_Limit(byte voiceN, uint16_t value) {
  byte calibrationDataBytes[FSPWDataSize];
  byte *b = (byte *)&value;

  uint16_t startByteN = voiceN * FSPWDataSize;

  filePWHighLimitFS = LittleFS.open("PWHighLimit", "r+");
  filePWHighLimitFS.seek(startByteN);
  filePWHighLimitFS.write(b, FSPWDataSize);
  filePWHighLimitFS.close();
}

// Persist PW low limit for a voice. Called from find_PW_limit_v2().
void update_FS_PW_Low_Limit(byte voiceN, uint16_t value) {
  byte calibrationDataBytes[FSPWDataSize];
  byte *b = (byte *)&value;

  uint16_t startByteN = voiceN * FSPWDataSize;

  filePWLowLimitFS = LittleFS.open("PWLowLimit", "r+");
  filePWLowLimitFS.seek(startByteN);
  filePWLowLimitFS.write(b, FSPWDataSize);
  filePWLowLimitFS.close();
}

// Persist a single manualCalibrationOffset entry for the given oscillator index.
// Persist one oscillator's manual calibration offset. Called from apply_param_manual_calibration_store().
void update_FS_ManualCalibrationOffset(byte oscIndex, int8_t value) {
  if (oscIndex >= NUM_OSCILLATORS) {
    return;
  }

  uint8_t b = (uint8_t)value;  // store raw signed byte
  uint16_t startByteN = oscIndex * FSManualOffsetDataSize;

  fileManualOffsetFS = LittleFS.open("ManualOffset", "r+");
  fileManualOffsetFS.seek(startByteN);
  fileManualOffsetFS.write(&b, FSManualOffsetDataSize);
  fileManualOffsetFS.close();
}

#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/LFO.ino"
// Init LFO1 and LFO2 instances. Called from setup() (Core 0).
void init_LFOs() {
  init_LFO1();
  init_LFO2();
}

// Init all per-oscillator drift LFOs. Called from setup() (Core 0).
void init_DRIFT_LFOs() {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    init_DRIFT_LFO(LFO_DRIFT_CLASS[i], LFO_DRIFT_CC, i);
  }
}

// Configure one drift LFO (waveform, amplitude, speed offset from spread/speed params).
void init_DRIFT_LFO(lfo &LFO, int CC, byte LFONumber) {
  LFO_DRIFT_SPEED_OFFSET[LFONumber] = (float)(1.00f - (float)((float)analogDriftSpread * 0.005) + (float)((float)analogDriftSpread * 0.00125f * (float)LFONumber)) * (float)expConverterFloat((float)analogDriftSpeed, 5000);
  LFO.setWaveForm(LFO_DRIFT_WAVEFORM);                  // inicializar forma de onda
  LFO.setAmpl(CC);                                      // establecer amplitud máxima
  LFO.setAmplOffset(0);                                 // sin offset a la amplitud
  LFO.setMode(0);                                       // establecer modo de sincronización a modo0 -> sin sincronización a BPM
  //LFO.setMode0Freq(LFO_DRIFT_SPEED_OFFSET[LFONumber]);  // establecer LFO a 0.5 Hz
  LFO.setMode0Freq(LFO_DRIFT_SPEED_OFFSET[LFONumber], micros());
}

// Configure main detune LFO (LFO1). Called from init_LFOs().
void init_LFO1() {
  LFO1_class.setWaveForm(LFO1Waveform);  // initialize waveform
  LFO1_class.setAmpl(LFO1_CC);           // set amplitude to maximum
  LFO1_class.setAmplOffset(0);           // no offset to the amplitude
  LFO1_class.setMode(0);                 // set sync mode to mode0 -> no sync to BPM
  LFO1_class.setMode0Freq(0.5);          // set LFO to 30 Hz
}


// Configure secondary LFO (LFO2: PW / OSC2 detune, etc.). Called from init_LFOs().
void init_LFO2() {
  LFO2_class.setWaveForm(2);    // initialize waveform
  LFO2_class.setAmpl(LFO2_CC);  // set amplitude to maximum
  LFO2_class.setAmplOffset(0);  // no offset to the amplitude
  LFO2_class.setMode(0);        // set sync mode to mode0 -> no sync to BPM
  LFO2_class.setMode0Freq(5);   // set LFO to 30 Hz
}

// Update LFO1 and write DETUNE_INTERNAL_q24 for Core1 FIFO. Called every loop() iter.
inline void LFO1() {
  //tLFO1 = micros();                                     // take timestamp
  //LFO1Level = LFO1_CC_HALF - LFO1_class.getWave(micros());
  LFO1Level = LFO1_class.getWave(micros()) - LFO1_CC_HALF;
  // Produce detune modulation directly in Q24 fixed-point:
  // detune_q24 = (LFO1Level * LFO1toDCO) * 2^24
  DETUNE_INTERNAL_q24 = (int32_t)LFO1Level * LFO1toDCO_q24;
}

// Update LFO2 levels and DETUNE_INTERNAL2_q24. Called from loop() ~every 100 µs.
inline void LFO2() {
  //tLFO1 = micros();                                     // take timestamp
  //LFO1Level = LFO1_CC_HALF - LFO1_class.getWave(micros());
  LFO2Level = LFO2_class.getWave(micros()) - LFO2_CC_HALF;
  //PW_MOD = (float)((float)LFO2Level * LFO2toPW);
  // Produce OSC2/OSC3 detune modulation in Q24 fixed-point:
  // detuneN_q24 = (LFO2Level * LFO2toDETUNEN) * 2^24
  DETUNE_INTERNAL2_q24 = (int32_t)LFO2Level * LFO2toDETUNE2_q24;
  DETUNE_INTERNAL3_q24 = (int32_t)LFO2Level * LFO2toDETUNE3_q24;
}

// Update per-oscillator drift LFO levels into LFO_DRIFT_LEVEL[]. Called with LFO2 ~every 100 µs.
inline void DRIFT_LFOs() {
  unsigned long currentMicros = micros();
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    LFO_DRIFT_LEVEL[i] = LFO_DRIFT_CC_HALF - LFO_DRIFT_CLASS[i].getWave(currentMicros);
  }
}
#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PID.ino"
#include "include_all.h"

// Compute allowed |gap| (in microseconds) for a given frequency (Hz) and
// duty-cycle error fraction (e.g. 0.005 = 0.5% duty error).
static double compute_gap_tolerance_for_freq(double freqHz, double dutyErrorFraction) {
  if (freqHz <= 0.0) {
    return 1e6;  // Very loose tolerance if frequency is invalid.
  }
  double periodUs = 1e6 / freqHz;                         // Wave period in microseconds.
  double toleranceUs = 2.0 * dutyErrorFraction * periodUs;  // From |gap| <= 2 * ε * T.
  return toleranceUs;
}

// Return true if the two values have opposite signs (simple sign change test).
// Used by calibrate_DCO() to detect when the duty-cycle error has crossed
// through zero between successive measurements (indicating we've passed the
// ideal PWM point and should probe neighbours more carefully).
static bool did_sign_change(float previous, float current) {
  return (previous > 0.0f && current < 0.0f) ||
         (previous < 0.0f && current > 0.0f);
}

// Helper: set the current DCO amplitude, wait for the waveform to settle,
// and return the measured duty-cycle gap (or timeout sentinel value).
// This centralizes the "write PWM, delay, measure gap" pattern so that
// calibrate_DCO() stays focused on the search logic instead of timing details.
// IMPORTANT: We normalize the sign here so that a *positive* value means
// "amplitude too low" and a *negative* value means "amplitude too high",
// matching the legacy behaviour of the old find_gap() implementation even
// after the hardware polarity handling was refactored.
static float measure_gap_for_amp(uint16_t ampPwm) {
  voice_task_autotune(0, ampPwm);
  delay(10);
  GapMeasurement gm = measure_gap(0);

  // Preserve the timeout sentinel exactly so downstream code can reliably
  // detect "no signal" vs a real small error.
  if (gm.timedOut) {
    return kGapTimeoutSentinel;
  }

  // For valid measurements, flip the sign so calibrate_DCO() continues to
  // move the PWM in the correct direction regardless of the edge polarity
  // used inside find_gap().
  return -gm.value;
}

// Helper: evaluate neighbour measurements (lower/higher) around the current
// voltage and update closestToZero / bestAmpComp if any of them are better.
// The caller passes in the measurements taken one step below and above the
// current PWM value; this routine picks the best candidate among those and
// the current PWM, based purely on closeness of the duty error to zero.
static void update_best_from_neighbours(
  int rangeSamples,
  const float* lowerMeasurements,
  const uint16_t* lowerVoltages,
  const float* higherMeasurements,
  const uint16_t* higherVoltages,
  float avgValue,
  float& closestToZero,
  uint16_t& bestAmpComp,
  uint16_t currentAmpCompCalibrationVal
) {
  // Evaluate stored measurements including the current voltage
  for (int i = 0; i < rangeSamples; i++) {
    if (abs(lowerMeasurements[i]) < abs(closestToZero)) {
      closestToZero = lowerMeasurements[i];
      bestAmpComp = lowerVoltages[i];
    }
    if (abs(higherMeasurements[i]) < abs(closestToZero)) {
      closestToZero = higherMeasurements[i];
      bestAmpComp = higherVoltages[i];
    }
  }

  // Check the current voltage again
  if (abs(avgValue) < abs(closestToZero)) {
    closestToZero = avgValue;
    bestAmpComp = currentAmpCompCalibrationVal;
  }
}

// Helper: update the calibration PWM based on the current error and tolerance.
// For large errors we step the PWM in units of 2; once we get close to the
// target (within tolerance * 20) we only step by 1 to avoid overshooting.
static void step_amp_from_error(float avgValue, double tolerance, uint16_t& currentAmpCompCalibrationVal) {
  // Adjust the voltage based on the measurement
  if (abs(avgValue) < tolerance * 20) {
    if (avgValue > 0) {
      currentAmpCompCalibrationVal += 1;
    } else {
      currentAmpCompCalibrationVal -= 1;
    }
  } else {
    if (avgValue > 0) {
      currentAmpCompCalibrationVal += 2;
    } else {
      currentAmpCompCalibrationVal -= 2;
    }
  }
}

// Helper: compute the initial amplitude (range PWM) guess for a given table
// index j and note, using the same interpolation strategy as the original code:
//  - j == 4: manual preset scaled by 1.35,
//  - j == 6: logarithmic interpolation between the first two entries,
//  - else : quadratic interpolation based on the previous three calibration points.
static uint16_t compute_initial_amp_for_note(
  const DCOCalibrationContext& ctx,
  int j
) {
  if (j == 4) {
    return (ctx.initManualAmpByOsc[ctx.dcoIndex] + ctx.manualOffsetByOsc[ctx.dcoIndex]) * 1.35;
  } else if (j == 6) {
    return logarithmicInterpolation(
      ctx.calibrationData[2],
      ctx.calibrationData[3],
      ctx.calibrationData[4],
      ctx.calibrationData[5],
      sNotePitches[ctx.currentNote - 12] * 100
    );
  } else {
    return quadraticInterpolation(
      ctx.calibrationData[j - 6],
      ctx.calibrationData[j - 5],
      ctx.calibrationData[j - 4],
      ctx.calibrationData[j - 3],
      ctx.calibrationData[j - 2],
      ctx.calibrationData[j - 1],
      sNotePitches[ctx.currentNote - 12] * 100
    );
  }
}

// Helper: store the final calibration pair for the current note into the
// calibration table and print a short summary to Serial.
static void store_note_result(
  DCOCalibrationContext& ctx,
  int j,
  uint16_t bestAmpComp,
  float closestToZero
) {
  ctx.calibrationData[j]     = sNotePitches[ctx.currentNote - 12] * 100;
  ctx.calibrationData[j + 1] = bestAmpComp;

  Serial.print("DCO_calibration_current_note ");
  Serial.println(ctx.currentNote);
  Serial.print("Best calibration voltage: ");
  Serial.println(bestAmpComp);
  Serial.print("Closest measurement to zero: ");
  Serial.println(closestToZero);
}

// Initialize the global PID controller used by some legacy calibration
// routines. Newer code relies more on explicit search than on PID_v1.
void init_PID() {
  //initialize the variables we're linked to
  PIDInput = -2000;
  PIDSetpoint = 0;

  myPID.SetMode(AUTOMATIC);
}

// LEGACY: Note-by-note DCO calibration loop driven by PID_v1.
// Superseded by calibrate_DCO() and currently not used in the main flow.
// Left here for reference and potential future experiments.

// Search highest usable DCO frequency (returns Hz*100). Called from calibrate_DCO().
float find_highest_freq() {

  ampCompCalibrationVal = DIV_COUNTER;
  PIDInput = 100;
  myPID.SetOutputLimits(sNotePitches[DCO_calibration_current_note - 12 - calibration_note_interval], sNotePitches[DCO_calibration_current_note - 12 + calibration_note_interval]);
  myPID.SetTunings(0.01, 1.2, 0.002);
  myPID.SetSampleTime(5);

  // Initialize to a non-zero value so the loop enters at least once.
  DCO_calibration_difference = 1000.0f;

  while (abs(DCO_calibration_difference) > 0.5) {
    voice_task_autotune(4, DIV_COUNTER);
    delay(4);

    // Use the same structured gap measurement and sign normalization that the
    // main calibrate_DCO() path uses, so this function is not sensitive to
    // hardware polarity changes inside find_gap().
    GapMeasurement gm = measure_gap(0);
    if (gm.timedOut) {
      DCO_calibration_difference = kGapTimeoutSentinel;
    } else {
      // Flip sign so that a positive value still means "too low" and negative
      // means "too high", matching the legacy behaviour.
      DCO_calibration_difference = -gm.value;
    }

    PIDInput = 0 - (double)DCO_calibration_difference;

    myPID.Compute();

    if (autotuneDebug >= 1) {
      Serial.println((String) "Pid output: " + PIDOutput + (String) " Pid gap: " + DCO_calibration_difference);
    }
  }
  Serial.println((String) "Highest freq found: " + PIDOutput);

  //find highest note
  for (int i = 0; i < sizeof(sNotePitches); i++) {
    if (PIDOutput > sNotePitches[i] && PIDOutput < sNotePitches[i + 1]) {
      highestNoteOSC[currentDCO] = i;
      Serial.println((String) "Highest note found: " + i + (String) " - Note freq: " + sNotePitches[i]);
      break;
    }
  }

  return PIDOutput * 100;
}

// Estimate the lowest reachable frequency for the current DCO using the
// latest [freq -> PWM] calibration data and a polynomial fit, assuming
// an amp compensation (range PWM) of 0. This is conceptually symmetric
// to find_highest_freq(), but instead of running a full PID loop we
// derive the starting point from the same interpolation strategy used
// in calibrate_DCO().
//
// Return value: estimated lowest frequency * 100 (same units as
// calibrationData[] entries and find_highest_freq()).
float find_lowest_freq() {
  // Use amp compensation (range PWM) = 0 as requested.
  ampCompCalibrationVal = 0;

  // We require at least three calibration points (six entries) to build
  // a quadratic fit in the [PWM -> freq] direction. The layout of
  // calibrationData is:
  //   [0]  reserved / lowestFreq placeholder
  //   [1]  reserved
  //   [2]  freq0 * 100
  //   [3]  pwm0
  //   [4]  freq1 * 100
  //   [5]  pwm1
  //   [6]  freq2 * 100
  //   [7]  pwm2
  //   ...
  //
  // If we don't have enough data, just return 0.
  if (chanLevelVoiceDataSize < 8) {
    return 0.0f;
  }

  float f0 = (float)calibrationData[2];  // already freq * 100
  float p0 = (float)calibrationData[3];
  float f1 = (float)calibrationData[4];
  float p1 = (float)calibrationData[5];
  float f2 = (float)calibrationData[6];
  float p2 = (float)calibrationData[7];

  // Guard against degenerate cases where the PWMs are identical.
  if (p0 == p1 || p1 == p2 || p0 == p2) {
    // Fall back to a simple linear extrapolation using the first segment.
    float y = linearInterpolation(p0, f0, p1, f1, 0.0f);
    return y;
  }

  // Fit a quadratic in the space PWM -> (freq * 100) and evaluate it at
  // PWM = 0 to estimate the lowest reachable frequency at amp=0.
  float estFreqTimes100 = quadraticInterpolation(
    p0, f0,
    p1, f1,
    p2, f2,
    0.0f
  );

  // Clamp to a sensible minimum to avoid negative or zero frequencies
  // from extreme extrapolation.
  if (estFreqTimes100 < 0.0f) {
    estFreqTimes100 = 0.0f;
  }

  Serial.println((String)"[LOWEST_FREQ_EST] DCO=" + currentDCO +
                 (String)" estFreq*100=" + estFreqTimes100 +
                 (String)" using PWM points {" + p0 + "," + p1 + "," + p2 + "}");

  return estFreqTimes100;
}

// Build the [frequency -> amplitude PWM] calibration table for the DCO in ctx.
// For each calibration note it:
//  - Picks an initial PWM guess (via interpolation),
//  - Searches locally for the PWM that makes the duty error closest to zero,
//  - Stores the best PWM together with the note frequency in ctx.calibrationData.
// dutyErrorFraction controls how much duty-cycle error (e.g. 0.005 = 0.5%)
// is tolerated before the search stops for each note.
void calibrate_DCO(DCOCalibrationContext& ctx, double dutyErrorFraction) {

  double tolerance;      // Allowed absolute duty error (in microseconds) for a given note.
  uint16_t minAmpComp;   // Lower bound for the PWM search around the initial guess.
  uint16_t maxAmpComp;   // Upper bound for the PWM search around the initial guess.
  int rangeSamples = 2;  // Number of neighbour voltages to probe around a sign change.
  const int numPresetVoltages = chanLevelVoiceDataSize;  // Size of the [freq, pwm] table.

  for (int j = 4; j < numPresetVoltages; j += 2) {  // Start from the 3rd preset voltage
    uint16_t currentAmpCompCalibrationVal;

    ctx.currentNote = DCO_calibration_start_note + (calibration_note_interval * (j - 4) / 2);
    VOICE_NOTES[0] = ctx.currentNote;
    currentAmpCompCalibrationVal = compute_initial_amp_for_note(ctx, j);

    if (currentAmpCompCalibrationVal > DIV_COUNTER * 0.98) {
      // When we hit the top of the usable PWM range, stop the table here.
      // Record the highest reachable frequency at the current PWM, and also
      // estimate the lowest reachable frequency at PWM=0 so that the first
      // table entry remains a true "lowest note" anchor.
      float highestFreqFound = find_highest_freq();  // Hz * 100
      float lowestFreqCalc   = find_lowest_freq();   // Hz * 100, at PWM=0

      // Store the highest reachable point at this index.
      ctx.calibrationData[j]     = (uint32_t)highestFreqFound;
      ctx.calibrationData[j + 1] = DIV_COUNTER;

      // Ensure entry 0 continues to represent the lowest frequency at PWM=0.
      ctx.calibrationData[0] = (uint32_t)lowestFreqCalc;
      ctx.calibrationData[1] = 0;

      for (int i = j + 2; i < numPresetVoltages; i += 2) {
        ctx.calibrationData[i] = 20000000;
        ctx.calibrationData[i + 1] = DIV_COUNTER;
      }
      break;
    }

    uint16_t minAmpComp = currentAmpCompCalibrationVal * 0.8;  // Lower Limit for this note.
    uint16_t maxAmpComp = currentAmpCompCalibrationVal * 1.3;  // Upper Limit for this note.

    double freqHz = sNotePitches[VOICE_NOTES[0] - 12];
    tolerance = compute_gap_tolerance_for_freq(freqHz, dutyErrorFraction);

    // For debugging, report the effective duty-cycle tolerance in percent.
    double periodUs = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;
    double toleranceDutyPercent = 0.0;
    if (periodUs > 0.0) {
      double tolDutyFrac = tolerance / (2.0 * periodUs);
      toleranceDutyPercent = tolDutyFrac * 100.0;
    }

    Serial.println((String) "Current DCO: " + ctx.dcoIndex);
    Serial.println((String) "Calibration note: " + VOICE_NOTES[0]);
    Serial.println((String) "Calibration note freq: " + sNotePitches[VOICE_NOTES[0] - 12]);
    Serial.println((String) "Calibration note amplitude: " + currentAmpCompCalibrationVal);
    Serial.println((String) "Tolerance (us): " + tolerance);
    Serial.println((String) "Tolerance duty approx (%): " + toleranceDutyPercent);
    Serial.println((String) "MinAmpComp: " + minAmpComp);
    Serial.println((String) "MaxAmpComp: " + maxAmpComp);

    voice_task_autotune(0, currentAmpCompCalibrationVal);  // Send the preset voltage
    delay(10);

    uint16_t bestAmpComp = currentAmpCompCalibrationVal;  // Best PWM found so far for this note.
    float closestToZero = 50000;  // Smallest absolute duty error seen so far.
    float previousAvgValue = 0.0; // Duty error from the previous iteration (for sign-change detection).

    float lowerMeasurements[rangeSamples];   // Duty errors measured at lower neighbour PWMs.
    float higherMeasurements[rangeSamples];  // Duty errors measured at higher neighbour PWMs.
    uint16_t lowerVoltages[rangeSamples];    // PWM values used for lowerMeasurements[].
    uint16_t higherVoltages[rangeSamples];   // PWM values used for higherMeasurements[].

    int flipCounter = 0;  // Count of successive sign changes; used to relax tolerance if the search oscillates.

    while (true) {
      float avgValue = measure_gap_for_amp(currentAmpCompCalibrationVal);

      // Optional debug: report current duty and tolerance when enabled.
      // Treat timeout sentinel specially so we don't fake a 50% duty reading.
      if (autotuneDebug >= 2 && periodUs > 0.0) {
        if (avgValue == kGapTimeoutSentinel) {
          Serial.println((String)"[DCO_AMP_SCAN] note=" + ctx.currentNote +
                         (String)" DCO=" + ctx.dcoIndex +
                         (String)" AMP=" + currentAmpCompCalibrationVal +
                         (String)" gap=TIMEOUT" +
                         (String)" duty=NA target=50% tol≈" + toleranceDutyPercent + "%");
        } else {
          // avgValue is the same DCO_calibration_difference used elsewhere:
          // positive => low segment longer (duty < 50%), negative => high longer.
          double dutyErrorFrac = (double)avgValue / (2.0 * periodUs);
          double dutyPercent   = (0.5 + dutyErrorFrac) * 100.0;
        Serial.println((String)"[DCO_AMP_SCAN] note=" + ctx.currentNote +
                       (String)" DCO=" + ctx.dcoIndex +
                       (String)" AMP=" + currentAmpCompCalibrationVal +
                       (String)" gap=" + avgValue +
                       (String)"us duty=" + dutyPercent +
                       (String)"% target=50% tol≈" + toleranceDutyPercent + "%");
        }
      }

      // Update best candidate only if the measurement is valid (not a timeout)
      // and closer to zero than what we've seen before.
      if (avgValue != kGapTimeoutSentinel && abs(avgValue) < abs(closestToZero)) {
        closestToZero = avgValue;
        bestAmpComp = currentAmpCompCalibrationVal;
      } else {
        avgValue == 0;
      }

      // Detect sign change
      if (did_sign_change(previousAvgValue, avgValue)) {
        // Store measurements around the current voltage
        for (int i = 0; i < rangeSamples; i++) {
          float lowerVoltage = currentAmpCompCalibrationVal - (i + 1);
          float higherVoltage = currentAmpCompCalibrationVal + (i + 1);

          lowerMeasurements[i] = measure_gap_for_amp(lowerVoltage);
          lowerVoltages[i] = lowerVoltage;

          higherMeasurements[i] = measure_gap_for_amp(higherVoltage);
          higherVoltages[i] = higherVoltage;
        }

        update_best_from_neighbours(
          rangeSamples,
          lowerMeasurements,
          lowerVoltages,
          higherMeasurements,
          higherVoltages,
          avgValue,
          closestToZero,
          bestAmpComp,
          currentAmpCompCalibrationVal
        );

        // Break the loop if the closest value is within tolerance
        if (abs(closestToZero) <= tolerance) {
          break;
        } else {
          tolerance = tolerance * 1.2;
        }
        flipCounter++;
        if (flipCounter >= 3 && abs(closestToZero) <= tolerance * 2) {
          break;
        } else {
          tolerance = tolerance * 1.5;
        }
      }

      step_amp_from_error(avgValue, tolerance, currentAmpCompCalibrationVal);

      // Ensure the voltage stays within the allowed range
      if (currentAmpCompCalibrationVal < minAmpComp || currentAmpCompCalibrationVal > maxAmpComp) {
        Serial.println((String) "Calibration voltage out of range: " + currentAmpCompCalibrationVal);
      }

      previousAvgValue = avgValue;
    }

    store_note_result(ctx, j, bestAmpComp, closestToZero);
  }
}


// 3-point quadratic interpolate y at x. Used by calibrate_DCO helpers / find_lowest_freq.
float quadraticInterpolation(float x0, float y0, float x1, float y1, float x2, float y2, float x) {
  // Calculate the coefficients of the quadratic polynomial
  float a = ((y2 - (x2 * (y1 - y0) + x1 * y0 - x0 * y1) / (x1 - x0)) / (x2 * (x2 - x0 - x1) + x0 * x1));
  float b = ((y1 - y0) / (x1 - x0) - a * (x0 + x1));
  float c = y0 - x0 * (b + a * x0);

  // Use the polynomial to estimate the next value
  return a * x * x + b * x + c;
}

// Exponential interpolate between (x0,y0)-(x1,y1). Currently unused.

// Log interpolate between two points → uint16. Used by compute_initial_amp_for_note().
uint16_t logarithmicInterpolation(float x0, float y0, float x1, float y1, float x) {
  // Ensure x0 and x1 are not zero or negative to avoid log(0) or log of negative number
  if (x0 <= 0 || x1 <= 0) {
    return 0;  // or handle the error as needed
  }

  // Calculate the constants a and b
  float a = (y1 - y0) / (log(x1) - log(x0));
  float b = y0 - a * log(x0);

  // Calculate the y value at the given x
  float y = a * log(x) + b;

  return (uint16_t)round(y);
}

// Log interpolate (float). Currently unused.

// Log interpolate (double). Currently unused.

// Linear interpolate between two points. Used by find_lowest_freq().
float linearInterpolation(float x0, float y0, float x1, float y1, float x) {
  // Ensure x0 and x1 are not the same to avoid division by zero
  if (x0 == x1) {
    return 0;  // or handle the error as needed
  }

  // Calculate the slope (m) of the line
  float m = (y1 - y0) / (x1 - x0);

  // Calculate the y-intercept (b) of the line
  float b = y0 - m * x0;

  // Calculate the y value at the given x
  float y = m * x + b;

  return y;
}

// Solve exponential interpolation for y at x (log-space lerp). Used by initMultiplierTables().
double expInterpolationSolveY(double x, double x0, double x1, double y0, double y1) {
    if (x0 <= 0 || x1 <= 0) {
        // Handle error: x0 and x1 must be greater than 0 for exponential interpolation
        return NAN;
    }

    double log_y0 = log(y0);
    double log_y1 = log(y1);

    double log_y = log_y0 + (log_y1 - log_y0) * (x - x0) / (x1 - x0);

    return exp(log_y);
}
#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/PWM.ino"
// Configure range PWM (per DCO, DIV_COUNTER) and PW PWM (per voice, DIV_COUNTER_PW). Called from setup1().
void init_pwm()
{
  for (int i = 0; i < NUM_OSCILLATORS; i++)
  {
    gpio_set_function(RANGE_PINS[i], GPIO_FUNC_PWM);
    RANGE_PWM_SLICES[i] = pwm_gpio_to_slice_num(RANGE_PINS[i]);
    pwm_set_wrap(RANGE_PWM_SLICES[i], DIV_COUNTER);
    pwm_set_enabled(RANGE_PWM_SLICES[i], true);
  }

  for (int i = 0; i < NUM_VOICES_TOTAL; i++)
  {
    gpio_set_function(PW_PINS[i], GPIO_FUNC_PWM);
    PW_PWM_SLICES[i] = pwm_gpio_to_slice_num(PW_PINS[i]);
    pwm_set_wrap(PW_PWM_SLICES[i], DIV_COUNTER_PW);
    pwm_set_enabled(PW_PWM_SLICES[i], true);
  }

#ifdef ENABLE_CV_OUTS
  init_cv_pwm();
#endif
}

#ifdef ENABLE_CV_OUTS

// Init cutoff / resonance / VCA PWM (12-bit CV domain). Reso1 shares slice with RANGE OSC2.
void init_cv_pwm() {
  for (int i = 0; i < NUM_FILTERS; i++) {
    gpio_set_function(CUTOFF_PINS[i], GPIO_FUNC_PWM);
    CUTOFF_PWM_SLICES[i] = pwm_gpio_to_slice_num(CUTOFF_PINS[i]);
    CUTOFF_PWM_CHANS[i] = pwm_gpio_to_channel(CUTOFF_PINS[i]);
    // Cut1+Reso0 share slice 2 — both CV, wrap 4095.
    pwm_set_wrap(CUTOFF_PWM_SLICES[i], DIV_COUNTER_CV);
    pwm_set_enabled(CUTOFF_PWM_SLICES[i], true);

    gpio_set_function(RESO_PINS[i], GPIO_FUNC_PWM);
    RESO_PWM_SLICES[i] = pwm_gpio_to_slice_num(RESO_PINS[i]);
    RESO_PWM_CHANS[i] = pwm_gpio_to_channel(RESO_PINS[i]);
    // Reso1 (GP7) shares slice 3 with RANGE OSC2 (GP22): keep DIV_COUNTER wrap from init_pwm.
    if (RESO_PWM_SLICES[i] != RANGE_PWM_SLICES[1]) {
      pwm_set_wrap(RESO_PWM_SLICES[i], DIV_COUNTER_CV);
    }
    pwm_set_enabled(RESO_PWM_SLICES[i], true);
  }

  gpio_set_function(VCA_PIN, GPIO_FUNC_PWM);
  VCA_PWM_SLICE = pwm_gpio_to_slice_num(VCA_PIN);
  VCA_PWM_CHAN = pwm_gpio_to_channel(VCA_PIN);
  pwm_set_wrap(VCA_PWM_SLICE, DIV_COUNTER_CV);
  pwm_set_enabled(VCA_PWM_SLICE, true);
}

// Push soft VCF/VCA/reso levels to Pico PWM compares.
void write_cv_pwm() {
  const uint16_t vcf = VCF_PWM[0];
  for (int i = 0; i < NUM_FILTERS; i++) {
    pwm_set_chan_level(CUTOFF_PWM_SLICES[i], CUTOFF_PWM_CHANS[i], vcf);

    uint16_t reso_level = RESONANCE_PWM;
    if (RESO_PWM_SLICES[i] == RANGE_PWM_SLICES[1]) {
      // Shared wrap DIV_COUNTER with RANGE OSC2 — scale 0..4095 → 0..DIV_COUNTER.
      reso_level = (uint16_t)(((uint32_t)RESONANCE_PWM * DIV_COUNTER) / DIV_COUNTER_CV);
    }
    pwm_set_chan_level(RESO_PWM_SLICES[i], RESO_PWM_CHANS[i], reso_level);
  }
  pwm_set_chan_level(VCA_PWM_SLICE, VCA_PWM_CHAN, VCA_PWM[0]);
}

#endif  // ENABLE_CV_OUTS

#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.ino"
// Configure Serial1 (MIDI DIN), Serial2 (Mainboard or Input @ 2.5M), optional Screen PIO UART.
#ifdef ENABLE_SCREEN_UART
#include <SerialPIO.h>
// Interim: Screen on SerialPIO so MIDI keeps HW UART0 @ GP0/1 (see docs/PINOUT.md).
// Claims a free PIO SM (not OSC freq SM0 on pio0/1/2). Move to HW UART1 when MIDI is PIO.
SerialPIO SerialScreen(8, 9, 512);
#endif

void init_serial() {
  Serial1.setFIFOSize(256);
  Serial1.setPollingMode(true);
  Serial1.setRX(1);
  Serial1.setTX(0);
  Serial1.begin(31250);

  Serial2.setFIFOSize(512);
  Serial2.setPollingMode(false);
  Serial2.setRX(21);
  Serial2.setTX(20);
  Serial2.begin(2500000);

#ifdef ENABLE_SCREEN_UART
  SerialScreen.begin(2500000);
#endif

  Serial.begin(2000000);
}

/// -------------------------------
// Serial2: legacy Mainboard protocol (default)
// -------------------------------

#ifndef ENABLE_INPUT_UART

static void dco_handle_pw_update(char, const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_PAYLOAD_LEN_PW_UPDATE) {
    return;
  }
  uint16_t pwRaw = (uint16_t)payload[0] | (uint16_t(payload[1]) << 8);
  PW[0] = pwRaw / 4;
}

static void dco_handle_adsr_block(char, const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_PAYLOAD_LEN_ADSR_BLOCK) {
    return;
  }
  ADSR1_attack  = (uint16_t(payload[0]) << 8) | uint16_t(payload[1]);
  ADSR1_decay   = (uint16_t(payload[2]) << 8) | uint16_t(payload[3]);
  ADSR1_sustain = (uint16_t(payload[4]) << 8) | uint16_t(payload[5]);
  ADSR1_release = (uint16_t(payload[6]) << 8) | uint16_t(payload[7]);
}

static void dco_handle_param16(char, const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_PAYLOAD_LEN_PARAM_16) {
    return;
  }
  ParamFrame frame;
  decode_param_p(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static void dco_handle_param8(char, const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_PAYLOAD_LEN_PARAM_8) {
    return;
  }
  ParamFrame frame;
  decode_param_w(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static void dco_handle_param32(char, const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_PAYLOAD_LEN_PARAM_32) {
    return;
  }
  ParamFrame frame;
  decode_param_x(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static const SerialCommandDef dcoSerial2Commands[] = {
  { SERIAL_CMD_PW_UPDATE,  SERIAL_PAYLOAD_LEN_PW_UPDATE,  dco_handle_pw_update  },
  { SERIAL_CMD_ADSR_BLOCK, SERIAL_PAYLOAD_LEN_ADSR_BLOCK, dco_handle_adsr_block },
  { SERIAL_CMD_PARAM_16,   SERIAL_PAYLOAD_LEN_PARAM_16,   dco_handle_param16    },
  { SERIAL_CMD_PARAM_8,    SERIAL_PAYLOAD_LEN_PARAM_8,    dco_handle_param8     },
  { SERIAL_CMD_PARAM_32,   SERIAL_PAYLOAD_LEN_PARAM_32,   dco_handle_param32    },
};

static SerialParserContext dcoSerial2Parser = {
  SERIAL_WAIT_FOR_CMD, 0, nullptr, {0}, 0, 0, 0
};

void serial_STM32_task() {
  if (dcoSerial2Parser.state == SERIAL_READ_PAYLOAD) {
    uint32_t now = micros();
    serial_parser_check_timeout(dcoSerial2Parser, now);
  }
  if (Serial2.available() > 0) {
    uint32_t now = micros();
    while (Serial2.available() > 0) {
      uint8_t b = Serial2.read();
      serial_parser_process_byte(
        dcoSerial2Parser,
        dcoSerial2Commands,
        sizeof(dcoSerial2Commands) / sizeof(dcoSerial2Commands[0]),
        b,
        now
      );
    }
  }
}

// Legacy Mainboard envelope peer (EnvVCA/EnvVCF lived on STM32).
inline void serial_send_note_on(uint8_t voice_n, uint8_t note_velo, uint8_t note) {
  byte sendArray[4];
  sendArray[0] = (uint8_t)'n';
  sendArray[1] = voice_n;
  sendArray[2] = note_velo;
  sendArray[3] = note;
  while (Serial2.availableForWrite() < 1) {}
  Serial2.write(sendArray, 4);
}

inline void serial_send_note_off(uint8_t voice_n) {
  byte sendArray[2] = { (uint8_t)'o', voice_n };
  while (Serial2.availableForWrite() < 1) {}
  Serial2.write(sendArray, 2);
}

#else  // ENABLE_INPUT_UART — Serial2 is Input hub

// EnvVCA times ('a')
static void input_handle_adsr1(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_ADSR_BLOCK) return;
  ADSR_VCA_attack  = word(payload[0], payload[1]);
  ADSR_VCA_decay   = word(payload[2], payload[3]);
  ADSR_VCA_sustain = word(payload[4], payload[5]);
  ADSR_VCA_release = word(payload[6], payload[7]);
}

// EnvVCF times ('b')
static void input_handle_adsr2(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_ADSR_BLOCK) return;
  ADSR_VCF_attack  = word(payload[0], payload[1]);
  ADSR_VCF_decay   = word(payload[2], payload[3]);
  ADSR_VCF_sustain = word(payload[4], payload[5]);
  ADSR_VCF_release = word(payload[6], payload[7]);
}

// EnvDCO times ('c') → existing ADSR1_* engine (pitch/PW)
static void input_handle_adsr3(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_ADSR_BLOCK) return;
  ADSR1_attack  = word(payload[0], payload[1]);
  ADSR1_decay   = word(payload[2], payload[3]);
  ADSR1_sustain = word(payload[4], payload[5]);
  ADSR1_release = word(payload[6], payload[7]);
}

static void input_handle_filter_block(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_FILTER_BLOCK) return;
  CUTOFF     = word(payload[0], payload[1]);
  RESONANCE  = word(payload[2], payload[3]);
  ADSR2toVCF = (int16_t)word(payload[4], payload[5]);
  LFO2toVCF  = word(payload[6], payload[7]);
  cv_update_mod_formulas();
}

static void input_handle_adsr1_to_vca(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_ADSR1_TO_VCA) return;
  ADSR1toVCA = (int16_t)word(payload[0], payload[1]);
}

// Input 'f' is big-endian PW (0..4095-ish); voice engine uses PW[0] at /4 scale
static void input_handle_pw(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PW_VALUE) return;
  uint16_t pwRaw = word(payload[0], payload[1]);
  PW[0] = pwRaw / 4;
}

static void input_handle_param16(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PARAM_16) return;
  ParamFrame frame;
  decode_param_p(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static void input_handle_param8(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PARAM_8) return;
  ParamFrame frame;
  decode_param_w(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static void input_handle_preset_name(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PRESET_NAME) return;
  for (int i = 0; i < 8; ++i) {
    presetName[i] = payload[i];
  }
}

static const SerialCommandDef inputSerialCommands[] = {
  { INPUT_CMD_ADSR1_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr1        },
  { INPUT_CMD_ADSR2_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr2        },
  { INPUT_CMD_ADSR3_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr3        },
  { INPUT_CMD_FILTER_BLOCK,  INPUT_SERIAL_LEN_FILTER_BLOCK, input_handle_filter_block },
  { INPUT_CMD_ADSR1_TO_VCA,  INPUT_SERIAL_LEN_ADSR1_TO_VCA, input_handle_adsr1_to_vca },
  { INPUT_CMD_PW_VALUE,      INPUT_SERIAL_LEN_PW_VALUE,     input_handle_pw           },
  { INPUT_CMD_PARAM_16,      INPUT_SERIAL_LEN_PARAM_16,     input_handle_param16      },
  { INPUT_CMD_PARAM_8,       INPUT_SERIAL_LEN_PARAM_8,      input_handle_param8       },
  { INPUT_CMD_PRESET_NAME,   INPUT_SERIAL_LEN_PRESET_NAME,  input_handle_preset_name  },
};

static SerialParserContext inputSerialParser = {
  SERIAL_WAIT_FOR_CMD, 0, nullptr, {0}, 0, 0, 0
};

void serial_STM32_task() {
  if (inputSerialParser.state == SERIAL_READ_PAYLOAD) {
    uint32_t now = micros();
    serial_parser_check_timeout(inputSerialParser, now);
  }
  if (Serial2.available() > 0) {
    uint32_t now = micros();
    while (Serial2.available() > 0) {
      uint8_t b = Serial2.read();
      serial_parser_process_byte(
        inputSerialParser,
        inputSerialCommands,
        sizeof(inputSerialCommands) / sizeof(inputSerialCommands[0]),
        b,
        now
      );
    }
  }
}

// No Mainboard envelope peer — note edges are local (EnvVCA/EnvVCF on Core1).
inline void serial_send_note_on(uint8_t, uint8_t, uint8_t) {}
inline void serial_send_note_off(uint8_t) {}

#endif  // ENABLE_INPUT_UART

static inline void serial_write_param32_frame(Stream& port, byte paramNumber, uint32_t paramValue) {
  uint8_t *b = (uint8_t *)&paramValue;
  byte bytesArray[7] = { (uint8_t)'x', paramNumber, b[0], b[1], b[2], b[3], 1 };
  while (port.availableForWrite() < 7) {}
  port.write(bytesArray, 7);
}

#ifdef ENABLE_SCREEN_UART
void serialSendParam32ToScreen(byte paramNumber, uint32_t paramValue) {
  serial_write_param32_frame(SerialScreen, paramNumber, paramValue);
}
#endif

// Route 'x' frames: gap → Screen (if enabled); else Input hub or legacy Mainboard.
void serialSendParam32(byte paramNumber, uint32_t paramValue) {
#ifdef ENABLE_SCREEN_UART
  if (paramNumber == (byte)PARAM_GAP_FROM_DCO) {
    serialSendParam32ToScreen(paramNumber, paramValue);
    return;
  }
#endif

#if defined(ENABLE_INPUT_UART)
  // Hub: cal offsets (155) and other 'x' go to Input (replaces Mainboard Serial8 forward).
  serial_write_param32_frame(Serial2, paramNumber, paramValue);
#elif defined(ENABLE_SCREEN_UART)
  // Screen bring-up without Input/Mainboard: non-gap 'x' has no peer — drop.
  (void)paramNumber;
  (void)paramValue;
#else
  // Legacy: Serial2 → Mainboard (forwards gap to Screen / offsets to Input).
  serial_write_param32_frame(Serial2, paramNumber, paramValue);
#endif
}

#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Timer_millis.ino"
// Update soft timer flags (99 µs, ~1 ms, 200 ms, 1000 ms, …). Called every loop1() iteration.
inline void millisTimer()
{

  timer99microsFlag = 0;
  timer223microsFlag = 0;
  timer1msFlag = 0;
  // timer2340microsFlag = 0;
  // timer3543microsFlag = 0;
  // timer5msFlag = 0;
  // timer11msFlag = 0;
  // timer23msFlag = 0;
  // timer31msFlag = 0;
  // timer67msFlag = 0;
  timer200msFlag = 0;
  //  timer500msFlag = 0;
  timer1000msFlag = 0;


  unsigned long currentMillis = millis();
  unsigned long currentMicros = micros();

  if (currentMicros - timer99micros > 99)
  {
    timer99micros = currentMicros;
    timer99microsFlag = 1;
  }

  if (currentMicros - timer223micros > 223)
  {
    timer223micros = currentMicros;
    timer223microsFlag = 1;
  }

  if ( currentMicros - timer1ms > 1001) {
    timer1ms = currentMicros;
    timer1msFlag = 1;
  }

  // if ( currentMicros - timer2340micros > 2340) {
  //   timer2340micros = currentMicros;
  //   timer2340microsFlag = 1;
  // }

  // if ( currentMicros - timer3543micros > 3543) {
  //   timer3543micros = currentMicros;
  //   timer3543microsFlag = 1;
  // }

  // if ( currentMillis - timer5ms > 5) {
  //   timer5ms = currentMillis;
  //   timer5msFlag = 1;
  // }

  // if ( currentMillis - timer11ms > 11) {
  //   timer11ms = currentMillis;
  //   timer11msFlag = 1;
  // }

  // if ( currentMillis - timer23ms > 23) {
  //   timer23ms = currentMillis;
  //   timer23msFlag = 1;
  // }

  // if ( currentMillis - timer31ms > 31) {
  //   timer31ms = currentMillis;
  //   timer31msFlag = 1;
  // }

  // if ( currentMillis - timer67ms > 67) {
  //   timer67ms = currentMillis;
  //   timer67msFlag = 1;
  // }

  if (currentMillis - timer200ms > 200)
  {
    timer200ms = currentMillis;
    timer200msFlag = 1;
  }

  if (currentMillis - timer1000ms > 1000)
  {
    timer1000ms = currentMillis;
    timer1000msFlag = 1;
  }

  //    if ( currentMillis - timer500ms > 500) {
  //    timer500ms = currentMillis;
  //    timer500msFlag = 1;
  //  }
}
#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/adsr.ino"
// Boot: build Bézier/log tables and apply initial A/D/S/R to EnvDCO + EnvVCA + EnvVCF.
void init_ADSR() {
  adsrBezierInitTables(ADSR_1_CC, ARRAY_SIZE, _curve_tables);

  for (int i = 0; i < LIN_TO_EXP_TABLE_SIZE; i++) {
    linToLogLookup[i] = linearToLogarithmic(i, 10, maxADSRControlValue);
  }

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr1_voice.setAttack(ADSR1_attack);
    ADSRVoices[i].adsr1_voice.setDecay(ADSR1_decay);
    ADSRVoices[i].adsr1_voice.setSustain(ADSR1_sustain);
    ADSRVoices[i].adsr1_voice.setRelease(ADSR1_release);
    ADSRVoices[i].adsr1_voice.setResetAttack(ADSRRestart);

    ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
    ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
    ADSRVoices[i].adsr_vca_voice.setSustain(ADSR_VCA_sustain);
    ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
    ADSRVoices[i].adsr_vca_voice.setResetAttack(VCAADSRRestart);

    ADSRVoices[i].adsr_vcf_voice.setAttack(ADSR_VCF_attack);
    ADSRVoices[i].adsr_vcf_voice.setDecay(ADSR_VCF_decay);
    ADSRVoices[i].adsr_vcf_voice.setSustain(ADSR_VCF_sustain);
    ADSRVoices[i].adsr_vcf_voice.setRelease(ADSR_VCF_release);
    ADSRVoices[i].adsr_vcf_voice.setResetAttack(VCFADSRRestart);
  }
}

// ~10 kHz: note edges → EnvDCO + EnvVCA + EnvVCF; sample levels.
inline void ADSR_update() {
  tADSR = millis();
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    if (noteEnd[i] == 1) {
      ADSRVoices[i].adsr1_voice.noteOff();
      ADSRVoices[i].adsr_vca_voice.noteOff();
      ADSRVoices[i].adsr_vcf_voice.noteOff();
      noteEnd[i] = 0;
    } else if (noteStart[i] == 1) {
      ADSRVoices[i].adsr1_voice.noteOff();
      ADSRVoices[i].adsr1_voice.setAttack(ADSR1_attack);
      ADSRVoices[i].adsr1_voice.setDecay(ADSR1_decay);
      ADSRVoices[i].adsr1_voice.setRelease(ADSR1_release);
      ADSRVoices[i].adsr1_voice.noteOn();

      ADSRVoices[i].adsr_vca_voice.noteOff();
      ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
      ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
      ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
      ADSRVoices[i].adsr_vca_voice.noteOn();

      ADSRVoices[i].adsr_vcf_voice.noteOff();
      ADSRVoices[i].adsr_vcf_voice.setAttack(ADSR_VCF_attack);
      ADSRVoices[i].adsr_vcf_voice.setDecay(ADSR_VCF_decay);
      ADSRVoices[i].adsr_vcf_voice.setRelease(ADSR_VCF_release);
      ADSRVoices[i].adsr_vcf_voice.noteOn();

      noteStart[i] = 0;
    }
    ADSR1Level[i] = ADSRVoices[i].adsr1_voice.getWave();
    ADSR_VCA_Level[i] = ADSRVoices[i].adsr_vca_voice.getWave();
    ADSR_VCF_Level[i] = ADSRVoices[i].adsr_vcf_voice.getWave();
  }
  ADSR_set_parameters();
}

// Debounced sustain (and EnvVCA/EnvVCF A/D/R when changed) push.
inline void ADSR_set_parameters() {
  if ((tADSR - tADSR_params) > 5) {
    static uint16_t last_attack = 0xFFFF, last_decay = 0xFFFF, last_sustain = 0xFFFF, last_release = 0xFFFF;
    static uint16_t last_vca_a = 0xFFFF, last_vca_d = 0xFFFF, last_vca_s = 0xFFFF, last_vca_r = 0xFFFF;
    static uint16_t last_vcf_a = 0xFFFF, last_vcf_d = 0xFFFF, last_vcf_s = 0xFFFF, last_vcf_r = 0xFFFF;

    bool dco_a = (ADSR1_attack != last_attack);
    bool dco_d = (ADSR1_decay != last_decay);
    bool dco_s = (ADSR1_sustain != last_sustain);
    bool dco_r = (ADSR1_release != last_release);
    bool vca_a = (ADSR_VCA_attack != last_vca_a);
    bool vca_d = (ADSR_VCA_decay != last_vca_d);
    bool vca_s = (ADSR_VCA_sustain != last_vca_s);
    bool vca_r = (ADSR_VCA_release != last_vca_r);
    bool vcf_a = (ADSR_VCF_attack != last_vcf_a);
    bool vcf_d = (ADSR_VCF_decay != last_vcf_d);
    bool vcf_s = (ADSR_VCF_sustain != last_vcf_s);
    bool vcf_r = (ADSR_VCF_release != last_vcf_r);

    if (dco_a || dco_d || dco_s || dco_r || vca_a || vca_d || vca_s || vca_r || vcf_a || vcf_d || vcf_s || vcf_r) {
      for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
        if (dco_a) ADSRVoices[i].adsr1_voice.setAttack(ADSR1_attack);
        if (dco_d) ADSRVoices[i].adsr1_voice.setDecay(ADSR1_decay);
        if (dco_s) ADSRVoices[i].adsr1_voice.setSustain(ADSR1_sustain);
        if (dco_r) ADSRVoices[i].adsr1_voice.setRelease(ADSR1_release);

        if (vca_a) ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
        if (vca_d) ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
        if (vca_s) ADSRVoices[i].adsr_vca_voice.setSustain(ADSR_VCA_sustain);
        if (vca_r) ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);

        if (vcf_a) ADSRVoices[i].adsr_vcf_voice.setAttack(ADSR_VCF_attack);
        if (vcf_d) ADSRVoices[i].adsr_vcf_voice.setDecay(ADSR_VCF_decay);
        if (vcf_s) ADSRVoices[i].adsr_vcf_voice.setSustain(ADSR_VCF_sustain);
        if (vcf_r) ADSRVoices[i].adsr_vcf_voice.setRelease(ADSR_VCF_release);
      }
      last_attack = ADSR1_attack; last_decay = ADSR1_decay;
      last_sustain = ADSR1_sustain; last_release = ADSR1_release;
      last_vca_a = ADSR_VCA_attack; last_vca_d = ADSR_VCA_decay;
      last_vca_s = ADSR_VCA_sustain; last_vca_r = ADSR_VCA_release;
      last_vcf_a = ADSR_VCF_attack; last_vcf_d = ADSR_VCF_decay;
      last_vcf_s = ADSR_VCF_sustain; last_vcf_r = ADSR_VCF_release;
    }
    tADSR_params = tADSR;
  }
}

void ADSR1_set_restart() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr1_voice.setResetAttack(ADSRRestart);
  }
}

void ADSR_VCA_set_restart() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr_vca_voice.setResetAttack(VCAADSRRestart);
  }
}

void ADSR_VCF_set_restart() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr_vcf_voice.setResetAttack(VCFADSRRestart);
  }
}

void ADSR1_change_curves() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr1_voice.setAttack(ADSR1_attack);
    ADSRVoices[i].adsr1_voice.setDecay(ADSR1_decay);
    ADSRVoices[i].adsr1_voice.setSustain(ADSR1_sustain);
    ADSRVoices[i].adsr1_voice.setRelease(ADSR1_release);
    ADSRVoices[i].adsr1_voice.setResetAttack(ADSRRestart);
  }
}

#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/autotune.ino"

#include "include_all.h"

// For debug logging and duty computation in gap measurement: track the last
// PW raw value we explicitly programmed for the current DCO, the duty
// target/period assumed by the current PW search routine, and the most
// recently measured period from find_gap().
static uint16_t g_lastPWMeasurementRaw = 0;
static double   g_gapLogCurrentPeriodUs = 0.0;
static double   g_gapLogTargetDutyFraction = 0.5;  // default 50%
static double   g_lastGapMeasuredPeriodUs = 0.0;

// Helper: turn off all oscillators and set their RANGE outputs to a known
// state, while charging their timing capacitors using the original
// PIO+GPIO sequence. This preserves the analogue behaviour you rely on.
static void disable_all_oscillators_and_range_pwm() {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    uint8_t pioNumber = VOICE_TO_PIO[i];
    PIO     pioN      = pio[VOICE_TO_PIO[i]];
    uint8_t smN = VOICE_TO_SM[i];

    // Original "park" frequency used to pre-charge the caps.
    uint32_t clk_div1 = 200;

    // Run the DCO SM at a known slow rate while driving the RANGE PWM.
    pio_sm_set_enabled(pioN, smN, true);
    pio_sm_put(pioN, smN, clk_div1);
    pio_sm_exec(pioN, smN, pio_encode_pull(false, false));

    delay(200);

    // Stop the SM and hold the RANGE pin high as a plain GPIO output.
    pio_sm_set_enabled(pioN, smN, false);
    gpio_init(RANGE_PINS[i]);
    gpio_set_dir(RANGE_PINS[i], GPIO_OUT);
    gpio_put(RANGE_PINS[i], 1);
  }

  // After all RANGE caps are charged, park shared PW PWM at max wrap so the
  // centre search can start from a known state. (Matches original behaviour.)
  reset_pw_to_DIV_COUNTER_PW();
}



// Helper: park shared PW PWM at max wrap (DIV_COUNTER_PW). Called from disable_all_oscillators_and_range_pwm().
static void reset_pw_to_DIV_COUNTER_PW() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), DIV_COUNTER_PW);
  }
}


// Initialize legacy PID-based DCO calibration state for oscillator 0.
// Note: the main calibration now uses calibrate_DCO(); this is kept
// for compatibility and reference.
// Currently unreachable at boot: setup1() clears calibrationFlag before the call site.
void init_DCO_calibration() {

  currentDCO = 0;

  VOICE_NOTES[0] = DCO_calibration_start_note;
  DCO_calibration_current_note = DCO_calibration_start_note;

  arrayPos = 0;
  calibrationData[arrayPos] = 0;
  calibrationData[arrayPos + 1] = ampCompLowestFreqVal;
  arrayPos += 2;

  calibrationData[arrayPos] = (uint32_t)(sNotePitches[manual_DCO_calibration_start_note - 12] * 100);
  calibrationData[arrayPos + 1] = initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];

  arrayPos += 2;

  currentNoteCalibrationStart = micros();
  DCOCalibrationStart = millis();
  DCO_calibration_difference = 10000;
  PIDMinGap = 300;

  samplesNumber = 52;

  sampleTime = (1000000 / sNotePitches[DCO_calibration_current_note - 12]) * ((samplesNumber - 1) / 2);

  PIDLimitsFormula = 78;
  PIDOutputLowerLimit = 70;
  PIDOutputHigherLimit = 100;

  // TURN OFF ALL OSCILLATORS and park shared PW voice.
  disable_all_oscillators_and_range_pwm();

  delay(100);

  DCO_calibration_difference = 4000;
  bestGap = 50000;
  bestCandidate = 50000;
  edgeDetectionLastTime = 0;
  PIDMinGapCounter = 0;
  pulseCounter = 0;
}

/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/
// Main DCO amplitude-compensation calibration entry point.
// Monosynth: calibrate shared PW on voice 0 once, then for each oscillator
// run calibrate_DCO() to build a [freq -> range PWM] table and persist via update_FS_voice().
void DCO_calibration() {

  // TURN OFF ALL OSCILLATORS and park shared PW voice.
  disable_all_oscillators_and_range_pwm();

  // PW is per-voice (monosynth: voice 0 only). Calibrate once, then amp-comp per osc.
  currentDCO = 0;
  restart_DCO_calibration();
  DCO_calibration_current_note = manual_DCO_calibration_start_note;
  VOICE_NOTES[0] = DCO_calibration_current_note;
  find_PW_center(0);
  find_PW_limit_v2(PW_LIMIT_LOW);
  find_PW_limit_v2(PW_LIMIT_HIGH);
  pwm_set_chan_level(PW_PWM_SLICES[0], pwm_gpio_to_channel(PW_PINS[0]), PW_CENTER[0]);
  PW[0] = PW_CENTER[0];

  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    currentDCO = i;

    restart_DCO_calibration();

    ampCompCalibrationVal = initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];
    pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], pwm_gpio_to_channel(RANGE_PINS[currentDCO]), ampCompCalibrationVal);

    DCO_calibration_current_note = DCO_calibration_start_note;
    VOICE_NOTES[0] = DCO_calibration_current_note;

    // uint16_t lowestFrequency = find_lowest_freq();
    // calibrationData[0] = lowestFrequency;

    bool oscAmpCompCalibrationComplete = false;

    // Build a small context for this DCO and run the calibration routine.
    DCOCalibrationContext ctx(
      currentDCO,
      DCO_calibration_current_note,
      calibrationData,
      manualCalibrationOffset,
      initManualAmpCompCalibrationVal
    );
    // Desired duty-cycle error tolerance as a fraction (e.g. 0.005 = 0.5%).
    double dutyErrorFraction = 0.001;
    calibrate_DCO(ctx, dutyErrorFraction);

    for (int j = 0; j < chanLevelVoiceDataSize; j++) {
      Serial.println(calibrationData[j]);
    }

    update_FS_voice(currentDCO);

    Serial.println((String) "DCO " + currentDCO + (String) " calibration finished.");
  }
  calibrationFlag = false;
  init_FS();

  // Rebuild amp-comp tables for the active engine.
  precompute_amp_comp_for_engine();
}
/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/

// Reset per-DCO calibration state and header entries in calibrationData.
// This is called before calibrating each DCO (and reused by VCO calibration).
void restart_DCO_calibration() {

  VOICE_NOTES[0] = DCO_calibration_start_note;
  DCO_calibration_current_note = DCO_calibration_start_note;

  arrayPos = 0;
  calibrationData[arrayPos] = 0;
  calibrationData[arrayPos + 1] = ampCompLowestFreqVal;
  arrayPos += 2;

  calibrationData[arrayPos] = (uint32_t)(sNotePitches[DCO_calibration_current_note - calibration_note_interval - 12] * 100);
  calibrationData[arrayPos + 1] = initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];

  arrayPos += 2;

  currentNoteCalibrationStart = micros();
  DCOCalibrationStart = millis();
  DCO_calibration_difference = 10000;
  PIDMinGap = 300;

  // TURN OFF ALL OSCILLATORS for a clean restart, and pre-charge the
  // RANGE capacitors using the legacy helper.
  disable_all_oscillators_and_range_pwm();

  // IMPORTANT: disable_all_oscillators_and_range_pwm() leaves RANGE_PINS[]
  // as plain GPIO outputs driven HIGH. Before starting calibration for the
  // currentDCO we must restore its RANGE pin back to PWM function so that
  // voice_task_autotune() and subsequent RANGE PWM writes actually appear
  // on the physical pin.
  gpio_set_function(RANGE_PINS[currentDCO], GPIO_FUNC_PWM);

  PIO pioN = pio[VOICE_TO_PIO[currentDCO]];
  uint8_t sm1N = VOICE_TO_SM[currentDCO];
  pio_sm_set_enabled(pioN, sm1N, true);

  delay(100);

  DCO_calibration_difference = 4000;
  bestGap = 50000;
  bestCandidate = 50000;
  edgeDetectionLastTime = 0;
  PIDMinGapCounter = 0;
  pulseCounter = 0;
}

/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/

// Shared search routine used by PW calibration functions (center/low/high).
// It searches for the PW value that yields a duty cycle closest to
// targetDutyFraction at the current calibration note. targetGap is the
// allowed absolute gap (in microseconds) from the ideal duty at that note.
static uint16_t find_PW_for_target_duty(double targetDutyFraction,
                                        uint16_t targetGap,
                                        uint16_t pwMin,
                                        uint16_t pwMax) {

  DCO_calibration_difference = 4000;
  bestGap = 50000;
  bestCandidate = 50000;
  edgeDetectionLastTime = 0;
  PIDMinGapCounter = 0;
  pulseCounter = 0;

  // Fixed-size tables for valid and invalid samples.
  // Valid samples: store (PW, gapDiff = gap - gapTarget).
  // Invalid samples: store (PW, distance in PW units to the nearest valid sample).
  const int kMaxSamples = 40;
  uint16_t validPW[kMaxSamples];
  double   validGapDiff[kMaxSamples];
  int      validCount = 0;
  int      inToleranceCount = 0;  // Number of valid samples within target gap

  uint16_t invalidPW[kMaxSamples];
  uint16_t invalidDistToValid[kMaxSamples];
  int      invalidCount = 0;

  // Precompute period and duty-cycle tolerance (in %) for debug reporting.
  double freqHz = (double)sNotePitches[DCO_calibration_current_note - 12];
  double periodUs = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;

  // Update global logging context for gap measurements during this search.
  g_gapLogCurrentPeriodUs     = periodUs;
  g_gapLogTargetDutyFraction  = targetDutyFraction;
  double toleranceDutyPercent = 0.0;
  double gapTarget = 0.0;
  if (periodUs > 0.0) {
    // Ideal gap for target duty: gap = T*(1 - 2p)
    gapTarget = periodUs * (1.0 - 2.0 * targetDutyFraction);
    double tolDutyFrac = (double)targetGap / (2.0 * periodUs);
    toleranceDutyPercent = tolDutyFrac * 100.0;
  }

  // ---- Phase 1: Coarse scan over PW range to find a sign-change bracket ----
  // Use smaller coarse steps for low/high limit searches (target duty far from 50%)
  // and larger steps for center search (targetDutyFraction ~ 0.5).
  uint16_t coarseDiv = (fabs(targetDutyFraction - 0.5) < 0.05) ? 16 : 32;
  uint16_t coarseStep = (pwMax > pwMin) ? ((pwMax - pwMin) / coarseDiv) : 1;
  if (coarseStep == 0) coarseStep = 1;

  bool havePrev = false;
  double prevGap = 0.0;
  uint16_t prevPW = 0;

  bool haveBracket = false;
  uint16_t pwLow = 0, pwHigh = 0;
  double gapLow = 0.0, gapHigh = 0.0;

  for (uint16_t pw = pwMin; pw <= pwMax; pw = (uint16_t)(pw + coarseStep)) {

    if (millis() - DCOCalibrationStart > 60000) {
      Serial.println("PW center coarse scan timeout (60s)");
      break;
    }

    pwm_set_chan_level(PW_PWM_SLICES[0],
                       pwm_gpio_to_channel(PW_PINS[0]),
                       pw);
    // Keep PW[] and debug tracker in sync so GAP logs show the actual PW tested.
    PW[0]        = pw;
    g_lastPWMeasurementRaw    = pw;
    delay(30);

    GapMeasurement gm = measure_gap(2);
    if (gm.timedOut) {
      // No usable signal at this PW; if we have at least one valid sample,
      // track this as an invalid entry near some valid PW for diagnostic use.
      if (validCount > 0 && invalidCount < kMaxSamples) {
        // Compute distance to nearest valid PW.
        uint16_t bestDist = 0xFFFF;
        for (int vi = 0; vi < validCount; ++vi) {
          uint16_t dist = (validPW[vi] > pw) ? (validPW[vi] - pw) : (pw - validPW[vi]);
          if (dist < bestDist) bestDist = dist;
        }
        invalidPW[invalidCount] = pw;
        invalidDistToValid[invalidCount] = bestDist;
        invalidCount++;
      } else if (validCount > 0 && invalidCount >= kMaxSamples) {
        // Table full: only keep invalids that are closer to valids than the current worst.
        uint16_t bestDist = 0xFFFF;
        for (int vi = 0; vi < validCount; ++vi) {
          uint16_t dist = (validPW[vi] > pw) ? (validPW[vi] - pw) : (pw - validPW[vi]);
          if (dist < bestDist) bestDist = dist;
        }
        // Find worst (largest distance) invalid entry.
        int worstIdx = 0;
        uint16_t worstDist = invalidDistToValid[0];
        for (int ii = 1; ii < invalidCount; ++ii) {
          if (invalidDistToValid[ii] > worstDist) {
            worstDist = invalidDistToValid[ii];
            worstIdx = ii;
          }
        }
        if (bestDist < worstDist) {
          invalidPW[worstIdx] = pw;
          invalidDistToValid[worstIdx] = bestDist;
        }
      }
      continue;  // skip invalid sample
    }

    double gap = (double)gm.value;
    double gapDiff = gap - gapTarget;
    double absGapDiff = abs(gapDiff);

    if (autotuneDebug >= 2 && periodUs > 0.0) {
      double dutyErrorFrac = gap / (2.0 * periodUs);
      double dutyPercent = (0.5 + dutyErrorFrac) * 100.0;
      Serial.println((String)"[PW_CENTER_COARSE] note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" PW_raw=" + pw +
                     (String)" gap=" + gap +
                     (String)"us duty=" + dutyPercent +
                     (String)"% target=50% tol≈" + toleranceDutyPercent + "%");
    }

    if (absGapDiff <= (double)targetGap) {
      inToleranceCount++;
    }

    if (absGapDiff < bestGap) {
      bestGap = absGapDiff;
      bestCandidate = pw;
    }

    // Maintain table of best valid samples.
    if (validCount < kMaxSamples) {
      validPW[validCount] = pw;
      validGapDiff[validCount] = gapDiff;
      validCount++;
    } else {
      // Table full: replace the worst entry if this one is closer to the target.
      int worstIdx = 0;
      double worstAbs = fabs(validGapDiff[0]);
      for (int vi = 1; vi < validCount; ++vi) {
        double curAbs = fabs(validGapDiff[vi]);
        if (curAbs > worstAbs) {
          worstAbs = curAbs;
          worstIdx = vi;
        }
      }
      if (absGapDiff < worstAbs) {
        validPW[worstIdx] = pw;
        validGapDiff[worstIdx] = gapDiff;
      }
    }

    if (havePrev) {
      // Check for sign change between prevGap and gap (relative to target duty)
      if ((gapDiff > 0.0 && prevGap < 0.0) || (gapDiff < 0.0 && prevGap > 0.0)) {
        haveBracket = true;
        pwLow = prevPW;
        gapLow = prevGap;
        pwHigh = pw;
        gapHigh = gap;

        // With two valid samples straddling the target, estimate the crossing
        // point via linear interpolation between prevGap and gap.
        double prevGapDiff = prevGap - gapTarget;
        double curGapDiff  = gap - gapTarget;
        double denom = fabs(prevGapDiff) + fabs(curGapDiff);
        if (denom > 0.0) {
          double t = fabs(prevGapDiff) / denom;  // weight towards the closer side
          uint16_t pwEst = (uint16_t)((double)prevPW + ((double)(pw - prevPW) * t));
          if (pwEst >= pwMin && pwEst <= pwMax) {
            pwm_set_chan_level(PW_PWM_SLICES[0],
                               pwm_gpio_to_channel(PW_PINS[0]),
                               pwEst);
            PW[0]     = pwEst;
            g_lastPWMeasurementRaw = pwEst;
            delay(30);
            GapMeasurement gmEst = measure_gap(2);
            if (!gmEst.timedOut) {
              double gapEst = (double)gmEst.value;
              double gapDiffEst = gapEst - gapTarget;
              double absGapDiffEst = fabs(gapDiffEst);

              if (absGapDiffEst <= (double)targetGap) {
                inToleranceCount++;
              }

              if (absGapDiffEst < bestGap) {
                bestGap = absGapDiffEst;
                bestCandidate = pwEst;
              }
              // Insert estimated point into valid table if it's good enough.
              if (validCount < kMaxSamples) {
                validPW[validCount] = pwEst;
                validGapDiff[validCount] = gapDiffEst;
                validCount++;
              }
            }
          }
        }

        break;
      }
    }

    havePrev = true;
    // For the bracket we keep the raw gap value; we subtract gapTarget only
    // when computing gapDiff.
    prevGap = gap;
    prevPW = pw;
  }

  // If we didn't find a bracket, we still want a fine search around the best
  // coarse candidate so that we gather multiple near-target samples before
  // deciding on a final PW.
  if (!haveBracket) {
    if (autotuneDebug >= 1) {
      Serial.println("PW center: no sign-change bracket found, running local fine scan.");
    }
    uint16_t startPW = (bestCandidate >= pwMin && bestCandidate <= pwMax)
                         ? bestCandidate
                         : (uint16_t)((pwMin + pwMax) / 2);
    uint16_t span = (coarseStep > 0) ? coarseStep * 2 : 4;
    uint16_t fineMin = (startPW > span) ? (startPW - span) : pwMin;
    uint16_t fineMax = (startPW + span < pwMax) ? (startPW + span) : pwMax;
    if (fineMax < fineMin) {
      uint16_t tmp = fineMin;
      fineMin = fineMax;
      fineMax = tmp;
    }
    uint16_t fineStep = (fineMax > fineMin) ? ((fineMax - fineMin) / 16) : 1;
    if (fineStep == 0) fineStep = 1;

    for (uint16_t pw = fineMin; pw <= fineMax; pw = (uint16_t)(pw + fineStep)) {
      if (millis() - DCOCalibrationStart > 60000) {
        Serial.println("PW center local fine scan timeout (60s)");
        break;
      }

      pwm_set_chan_level(PW_PWM_SLICES[0],
                         pwm_gpio_to_channel(PW_PINS[0]),
                         pw);
      PW[0]     = pw;
      g_lastPWMeasurementRaw = pw;
      delay(30);

      GapMeasurement gm = measure_gap(2);
      if (gm.timedOut) {
        continue;
      }

      double gap = (double)gm.value;
      double gapDiff = gap - gapTarget;
      double absGapDiff = fabs(gapDiff);

      if (absGapDiff <= (double)targetGap) {
        inToleranceCount++;
      }

      if (absGapDiff < bestGap) {
        bestGap = absGapDiff;
        bestCandidate = pw;
      }

      if (validCount < kMaxSamples) {
        validPW[validCount] = pw;
        validGapDiff[validCount] = gapDiff;
        validCount++;
      }
    }
  } else {
    // ---- Phase 2: Bisection search within the bracket ----
    for (int iter = 0; iter < 14; ++iter) {
      if (millis() - DCOCalibrationStart > 60000) {
        Serial.println("PW center bisection timeout (60s)");
        break;
      }

      uint16_t pwMid = (uint16_t)((pwLow + pwHigh) / 2);
      pwm_set_chan_level(PW_PWM_SLICES[0],
                         pwm_gpio_to_channel(PW_PINS[0]),
                         pwMid);
      PW[0]     = pwMid;
      g_lastPWMeasurementRaw = pwMid;
      delay(30);

      GapMeasurement gm = measure_gap(2);
      if (gm.timedOut) {
        // No valid data at this midpoint; skip this iteration and try again
        // on the next loop. Global time/iteration guards will still ensure
        // we eventually stop if there is no usable region.
        if (autotuneDebug >= 2) {
          Serial.println("PW center: timeout during bisection, skipping midpoint.");
        }
        continue;
      }

      double gapMid = (double)gm.value;
      double gapDiffMid = gapMid - gapTarget;
      double absGapDiffMid = abs(gapDiffMid);
      if (absGapDiffMid <= (double)targetGap) {
        inToleranceCount++;
      }

      if (absGapDiffMid < bestGap) {
        bestGap = absGapDiffMid;
        bestCandidate = pwMid;
      }

      if (autotuneDebug >= 2 && periodUs > 0.0) {
        double dutyErrorFrac = -gapMid / (2.0 * periodUs);
        double dutyPercent = (0.5 + dutyErrorFrac) * 100.0;
        Serial.println((String)"[PW_CENTER_BISECT] note=" + DCO_calibration_current_note +
                       (String)" DCO=" + currentDCO +
                       (String)" PW_raw=" + pwMid +
                       (String)" gap=" + gapMid +
                       (String)"us duty=" + dutyPercent +
                       (String)"% target=50% tol≈" + toleranceDutyPercent + "%");
      }

      // Do not early-exit on first in-tolerance sample; we want at least a
      // couple of near-target measurements before deciding, or until the
      // bracket can no longer be refined.

      // Maintain the sign-change bracket.
      if ((gapDiffMid > 0.0 && (gapLow - gapTarget) > 0.0) ||
          (gapDiffMid < 0.0 && (gapLow - gapTarget) < 0.0)) {
        pwLow = pwMid;
        gapLow = gapMid;
      } else {
        pwHigh = pwMid;
        gapHigh = gapMid;
      }

      if (pwHigh - pwLow <= 1) {
        // Can't refine further in integer PW space.
        break;
      }
    }
  }

  // Choose the best PW from the valid samples table. We now:
  //  1) Use the smallest gap to the target as the primary ranking.
  //  2) For each candidate (best to worse), run a lock-in phase that demands
  //     3 consecutive in-band readings at that PW.
  //  3) If all candidates fail lock-in, keep the previous PW_CENTER.
  if (validCount > 0) {
    // Try candidates from best gap to worse, without keeping an explicit
    // rejected list: after each failed lock-in, we mark that candidate by
    // inflating its gap difference so it won't be chosen again.
    for (int attempt = 0; attempt < validCount; ++attempt) {
      int   bestIdx = -1;
      double bestAbs = 1e12;
      int   inTolForThisPass = 0;

      // Find current best candidate and count in-band samples.
      for (int vi = 0; vi < validCount; ++vi) {
        double curAbs = fabs(validGapDiff[vi]);
        if (curAbs <= (double)targetGap) {
          inTolForThisPass++;
        }
        if (curAbs < bestAbs) {
          bestAbs = curAbs;
          bestIdx = vi;
        }
      }

      if (bestIdx < 0) {
        break;
      }

      // If the best gap is still extremely large compared to the allowed gap
      // (e.g. > 10x), abort early and keep the previous PW center. We no
      // longer require a minimum number of in-band coarse samples here,
      // because the lock-in phase will enforce stability.
      if (bestAbs > (double)targetGap * 10.0) {
        if (autotuneDebug >= 1) {
          Serial.println((String)"[PW_CENTER_ABORT] note=" + DCO_calibration_current_note +
                         (String)" DCO=" + currentDCO +
                         (String)" bestGap=" + bestAbs +
                         (String)"us (> " + (double)targetGap * 10.0 +
                         (String)"us); keeping PW_center=" + PW_CENTER[0]);
        }
        return PWCalibrationVal;
      }

      uint16_t chosenPW = validPW[bestIdx];
      // Reconstruct the gap for the chosen sample so we can report its duty.
      double chosenGap = gapTarget + validGapDiff[bestIdx];
      double chosenDutyPercent = 0.0;
      if (periodUs > 0.0) {
        double dutyErrorFrac = -chosenGap / (2.0 * periodUs);
        chosenDutyPercent = (0.5 + dutyErrorFrac) * 100.0;
      }

      // Lock-in phase for this candidate PW:
      bool lockedIn = false;
      int consecutiveOk = 0;
      const int kMaxLockInTries = 8;

      for (int li = 0; li < kMaxLockInTries && !lockedIn; ++li) {
        pwm_set_chan_level(PW_PWM_SLICES[0],
                           pwm_gpio_to_channel(PW_PINS[0]),
                           chosenPW);
        PW[0]     = chosenPW;
        g_lastPWMeasurementRaw = chosenPW;
        delay(30);

        GapMeasurement gmLock = measure_gap(2);
        if (gmLock.timedOut || periodUs <= 0.0) {
          consecutiveOk = 0;
          continue;
        }

        double gapLock = (double)gmLock.value;
        double gapDiffLock = gapLock - gapTarget;
        double absGapDiffLock = fabs(gapDiffLock);

        if (absGapDiffLock <= (double)targetGap) {
          consecutiveOk++;
          if (consecutiveOk >= 3) {
            lockedIn = true;
            chosenGap = gapLock;
            if (periodUs > 0.0) {
              double dutyErrorFrac = -chosenGap / (2.0 * periodUs);
              chosenDutyPercent = (0.5 + dutyErrorFrac) * 100.0;
            }
            break;
          }
        } else {
          consecutiveOk = 0;
        }
      }

      if (lockedIn) {
        // Local refinement: probe a small neighbourhood around the locked-in PW
        // (PW-2..PW+2). For each candidate in this window, we also require a
        // mini lock-in: 3 consecutive measurements within the target gap band
        // at that PW before we consider it.
        uint16_t bestLocalPW = chosenPW;
        double   bestLocalGapAbs = bestAbs;

        int16_t startOffset = -2;
        int16_t endOffset   =  2;
        for (int16_t off = startOffset; off <= endOffset; ++off) {
          int32_t testPW32 = (int32_t)chosenPW + off;
          if (testPW32 < (int32_t)pwMin || testPW32 > (int32_t)pwMax) continue;
          uint16_t testPW = (uint16_t)testPW32;

          bool   localLocked = false;
          int    localConsecutiveOk = 0;
          double gapLocal = 0.0;
          const int kMaxLocalLockInTries = 8;

          for (int lli = 0; lli < kMaxLocalLockInTries && !localLocked; ++lli) {
            pwm_set_chan_level(PW_PWM_SLICES[0],
                               pwm_gpio_to_channel(PW_PINS[0]),
                               testPW);
            PW[0]     = testPW;
            g_lastPWMeasurementRaw = testPW;
            delay(30);

            GapMeasurement gmLocal = measure_gap(2);
            if (gmLocal.timedOut || periodUs <= 0.0) {
              localConsecutiveOk = 0;
              continue;
            }

            gapLocal = (double)gmLocal.value;
            double gapDiffLocal = gapLocal - gapTarget;
            double absGapDiffLocal = fabs(gapDiffLocal);

            if (absGapDiffLocal <= (double)targetGap) {
              localConsecutiveOk++;
              if (localConsecutiveOk >= 3) {
                localLocked = true;
                double dutyErrorFracLocal = -gapLocal / (2.0 * periodUs);
                double dutyPercentLocal = (0.5 + dutyErrorFracLocal) * 100.0;
                (void)dutyPercentLocal; // only used implicitly via bestLocalGapAbs

                if (absGapDiffLocal < bestLocalGapAbs) {
                  bestLocalGapAbs = absGapDiffLocal;
                  bestLocalPW     = testPW;
                  chosenGap       = gapLocal;
                }
                break;
              }
            } else {
              localConsecutiveOk = 0;
            }
          }
        }

        chosenPW = bestLocalPW;
        if (periodUs > 0.0) {
          double dutyErrorFrac = -chosenGap / (2.0 * periodUs);
          chosenDutyPercent = (0.5 + dutyErrorFrac) * 100.0;
        }

        if (autotuneDebug >= 1) {
          Serial.println((String)"[PW_CENTER_RESULT] note=" + DCO_calibration_current_note +
                         (String)" DCO=" + currentDCO +
                         (String)" PW_center=" + chosenPW +
                         (String)" duty≈" + chosenDutyPercent +
                         (String)"% bestGap=" + bestLocalGapAbs +
                         (String)"us inTolSamples=" + inTolForThisPass +
                         (String)" totalValid=" + validCount);
        }
        return chosenPW;
      }

      // This candidate failed lock-in; inflate its gap diff so we try the next
      // best one on the following attempt.
      validGapDiff[bestIdx] = (double)targetGap * 20.0;
      if (autotuneDebug >= 1) {
        Serial.println((String)"[PW_CENTER_LOCKIN_REJECT] note=" + DCO_calibration_current_note +
                       (String)" DCO=" + currentDCO +
                       (String)" PW=" + chosenPW +
                       (String)" could not get 3 consecutive in-band readings; trying next candidate.");
      }
    }

    // If we reach here, no candidate passed lock-in.
    if (autotuneDebug >= 1) {
      Serial.println((String)"[PW_CENTER_ABORT] note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" all candidates failed lock-in; keeping PW_center=" +
                     PW_CENTER[0]);
    }
    return PWCalibrationVal;
  } else {
    // No valid samples at all in the searched range: keep the existing PWCalibrationVal
    // and log the situation so the user can investigate.
    if (autotuneDebug >= 1) {
      Serial.println("PW search: no valid samples found; keeping current PWCalibrationVal.");
    }
    return PWCalibrationVal;
  }
}

// Locate PW center for the current DCO's voice by minimizing duty-cycle error
// at a reference note. Mode 0 = low note, mode 1 = higher note refinement.
void find_PW_center(uint8_t mode) {

  DCO_calibration_current_note = manual_DCO_calibration_start_note;
  VOICE_NOTES[0] = DCO_calibration_current_note;
  ampCompCalibrationVal = initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];

  uint16_t targetGap;
  uint8_t voiceTaskMode;
  

  if (mode == 0) {
    targetGap = compute_gap_tolerance_for_freq(sNotePitches[DCO_calibration_current_note - 12], 0.005);
    voiceTaskMode = 2;
  } else {
    DCO_calibration_current_note = 76;
    VOICE_NOTES[0] = DCO_calibration_current_note;
    targetGap = 5;
    voiceTaskMode = 3;
  }

  currentNoteCalibrationStart = micros();
  DCOCalibrationStart = millis();

  PIDOutputLowerLimit = 0;
  PIDOutputHigherLimit = DIV_COUNTER_PW;

  

  if (firstTuneFlag == true) {
    PW[0] = DIV_COUNTER_PW / 2;
    PWCalibrationVal = DIV_COUNTER_PW / 2;
    PW_CENTER[0] = DIV_COUNTER_PW / 2;
  } else {

    PW[0] = PW_CENTER[0];
    PWCalibrationVal = PW_CENTER[0];
  }
  // Center the starting PW
  pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO],
                     pwm_gpio_to_channel(RANGE_PINS[currentDCO]),
                     PW[0]);

  voice_task_autotune(voiceTaskMode, ampCompCalibrationVal);

  uint16_t centerPW = find_PW_for_target_duty(
    kPWCenterDutyFraction,
    targetGap,
    0,
    DIV_COUNTER_PW
  );
  Serial.println("PW center found !!!");
  update_FS_PWCenter(0, centerPW);
  PW_CENTER[0] = centerPW;

  // Apply the newly found PW center immediately to the hardware so that the
  // effect is visible on the pulse waveform as soon as calibration finishes.
  pwm_set_chan_level(PW_PWM_SLICES[0],
                     pwm_gpio_to_channel(PW_PINS[0]),
                     centerPW);
  PW[0]        = centerPW;
  g_lastPWMeasurementRaw    = centerPW;
}


// -----------------------------------------------------------------------------
// New, more reusable PW limit search implementation (v2)
// -----------------------------------------------------------------------------

PWLimitSearchResult search_PW_limit_from_center(
  uint8_t     voiceIdx,
  uint16_t    centerPW,
  PWLimitDir  dir,
  double      periodUs,
  double      targetDuty
) {
  PWLimitSearchResult result;
  result.ok                  = false;
  result.limitPW             = centerPW;
  result.finalDutyPercent    = -1.0;

  if (periodUs <= 0.0) {
    return result;
  }

  // We deliberately keep the same hard bounds convention as the legacy
  // find_PW_limit() so that behaviour is comparable:
  //  - LOW  side scans from center down to 0
  //  - HIGH side scans from center up to DIV_COUNTER_PW
  uint16_t minPW = (dir == PW_LIMIT_LOW)  ? 0           : centerPW;
  uint16_t maxPW = (dir == PW_LIMIT_LOW)  ? centerPW    : DIV_COUNTER_PW;

  // Coarse step size for scanning from center toward the limit. We re-use
  // the order of magnitude of the original heuristic but express the scan in
  // a more compact, symmetric way.
  uint16_t step = DIV_COUNTER_PW / 64;
  if (step == 0) step = 1;

  bool     haveBest   = false;
  uint16_t bestPW     = centerPW;
  double   bestDelta  = 1e12;
  double   bestDuty   = -1.0;   // duty (0..1) at bestPW when known

  unsigned long searchStartMs = millis();

  // Coarse scan: walk from center toward the requested side, tracking the
  // PW that gets closest to the target duty. We stop when we reach the
  // boundary, run out of time, or find a value within tolerance.
  for (uint16_t pw = centerPW; ; ) {
    if (millis() - searchStartMs > 60000UL) {
      // Safety timeout (same order of magnitude as the legacy implementation).
      break;
    }

    if (pw < minPW) pw = minPW;
    if (pw > maxPW) pw = maxPW;

    pwm_set_chan_level(PW_PWM_SLICES[voiceIdx],
                       pwm_gpio_to_channel(PW_PINS[voiceIdx]),
                       pw);
    PW[voiceIdx]           = pw;
    g_lastPWMeasurementRaw = pw;
    delay(30);

    GapMeasurement gm = measure_gap(2);
    if (!gm.timedOut) {
      double gap           = (double)gm.value;
      double dutyErrorFrac = gap / (2.0 * periodUs);
      double duty          = 0.5 + dutyErrorFrac;

      double delta = fabs(duty - targetDuty);
      if (!haveBest || delta < bestDelta) {
        haveBest  = true;
        bestDelta = delta;
        bestPW    = pw;
        bestDuty  = duty;
      }

      if (autotuneDebug >= 2) {
        const char *scanTag =
          (dir == PW_LIMIT_LOW) ? "[PW_LOW_SCAN_V2]" : "[PW_HIGH_SCAN_V2]";
        Serial.println((String)scanTag +
                       (String)" note=" + DCO_calibration_current_note +
                       (String)" DCO=" + currentDCO +
                       (String)" PW_raw=" + pw +
                       (String)" duty=" + (duty * 100.0) + "%" +
                       (String)" targetDuty=" + (targetDuty * 100.0) + "%");
      }

      // If we are already within tolerance, we can stop the coarse scan early.
      if (delta <= kPWLimitDutyTolerance) {
        break;
      }
    }

    // Step toward the boundary.
    if (dir == PW_LIMIT_LOW) {
      if (pw <= minPW + step) {
        break;
      }
      pw = (uint16_t)(pw - step);
    } else {  // PW_LIMIT_HIGH
      if (pw >= maxPW - step) {
        break;
      }
      pw = (uint16_t)(pw + step);
    }
  }

  if (!haveBest) {
    // Never saw a valid measurement; caller should keep previous limit.
    return result;
  }

  // Fine refinement around bestPW: search with step = 1 in a relatively
  // tight window around the best coarse candidate. This keeps the search
  // local so we do not wander too far from the best-known PW.
  uint16_t refineRadius = step / 2;
  if (refineRadius < 4)  refineRadius = 4;
  if (refineRadius > 32) refineRadius = 32;

  uint16_t startPW;
  if (bestPW > refineRadius) {
    startPW = bestPW - refineRadius;
  } else {
    startPW = minPW;
  }
  // Enforce the same [minPW, maxPW] bounds used in the coarse scan so that
  // the refinement phase never crosses to the other side of center.
  if (startPW < minPW) startPW = minPW;

  uint16_t endPW = bestPW + refineRadius;
  if (endPW > maxPW) {
    endPW = maxPW;
  }

  int consecutiveTimeouts = 0;
  for (uint16_t pw = startPW; pw <= endPW; ++pw) {
    pwm_set_chan_level(PW_PWM_SLICES[voiceIdx],
                       pwm_gpio_to_channel(PW_PINS[voiceIdx]),
                       pw);
    PW[voiceIdx]           = pw;
    g_lastPWMeasurementRaw = pw;
    delay(30);

    GapMeasurement gm = measure_gap(2);
    if (gm.timedOut || periodUs <= 0.0) {
      // If we are stepping deeper into the "edge" side and accumulate several
      // consecutive timeouts, stop refining in that direction to avoid
      // spending a long time in a region with no measurable signal.
      if (gm.timedOut) {
        ++consecutiveTimeouts;
        bool goingDeeperLow  = (dir == PW_LIMIT_LOW)  && (pw < bestPW);
        bool goingDeeperHigh = (dir == PW_LIMIT_HIGH) && (pw > bestPW);
        if ((goingDeeperLow || goingDeeperHigh) && consecutiveTimeouts >= 4) {
          break;
        }
      }
      continue;
    }
    consecutiveTimeouts = 0;

    double gap           = (double)gm.value;
    double dutyErrorFrac = gap / (2.0 * periodUs);
    double duty          = 0.5 + dutyErrorFrac;

    double delta = fabs(duty - targetDuty);
    if (delta < bestDelta) {
      bestDelta = delta;
      bestPW    = pw;
      bestDuty  = duty;
    }
  }

  // Final result: start from the best sample seen during coarse+fine.
  result.ok      = true;
  result.limitPW = bestPW;

  if (bestDuty >= 0.0) {
    result.finalDutyPercent = bestDuty * 100.0;
  }

  // Check whether the target duty is actually reachable within tolerance.
  double currentDutyFrac = result.finalDutyPercent / 100.0;
  if (result.finalDutyPercent <= 0.0 ||
      fabs(currentDutyFrac - targetDuty) > kPWLimitDutyTolerance) {
    // Not within tolerance: push all the way to the hardware boundary for
    // this side and treat that as the "best possible" limit. This matches
    // the specification that the target is considered unreachable only after
    // trying the maximum/minimum PW value.
    uint16_t boundaryPW = (dir == PW_LIMIT_LOW) ? minPW : maxPW;

    pwm_set_chan_level(PW_PWM_SLICES[voiceIdx],
                       pwm_gpio_to_channel(PW_PINS[voiceIdx]),
                       boundaryPW);
    PW[voiceIdx]           = boundaryPW;
    g_lastPWMeasurementRaw = boundaryPW;
    delay(30);

    GapMeasurement gmEdge = measure_gap(2);
    if (!gmEdge.timedOut && periodUs > 0.0) {
      double gap           = (double)gmEdge.value;
      double dutyErrorFrac = gap / (2.0 * periodUs);
      double duty          = 0.5 + dutyErrorFrac;
      result.limitPW        = boundaryPW;
      result.finalDutyPercent = duty * 100.0;
    } else {
      // If even the boundary cannot be measured reliably, we still honour the
      // boundary PW as the limit but leave finalDutyPercent as-is.
      result.limitPW = boundaryPW;
    }
  }

  return result;
}

void find_PW_limit_v2(PWLimitDir dir) {
  uint8_t voiceTaskMode = 2;

  // Configure the calibration context in the same way as the legacy
  // find_PW_limit() so that both implementations are comparable.
  DCO_calibration_current_note = manual_DCO_calibration_start_note;
  VOICE_NOTES[0] = DCO_calibration_current_note;
  ampCompCalibrationVal =
    initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];

  currentNoteCalibrationStart = micros();
  DCOCalibrationStart         = millis();

  PIDOutputLowerLimit = 0;
  PIDOutputHigherLimit = DIV_COUNTER_PW;

  double freqHz   = (double)sNotePitches[DCO_calibration_current_note - 12];
  double periodUs = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;

  uint8_t  voiceIdx = 0;
  uint16_t centerPW = PW_CENTER[voiceIdx];

  // Direction-dependent target duty HIGH (porcentaje en nivel alto).
  //  - Low limit:  kPWLowDutyFraction  (≈ 2% HIGH)
  //  - High limit: kPWHighDutyFraction (≈98% HIGH)
  double targetDuty = (dir == PW_LIMIT_LOW)
                      ? kPWLowDutyFraction
                      : kPWHighDutyFraction;

  // Update global logging context for gap measurements during PW-limit search.
  g_gapLogCurrentPeriodUs    = periodUs;
  g_gapLogTargetDutyFraction = targetDuty;

  // Configure the DCO for PW calibration mode.
  voice_task_autotune(voiceTaskMode, ampCompCalibrationVal);
  delay(100);

  PWLimitSearchResult res =
    search_PW_limit_from_center(voiceIdx, centerPW, dir, periodUs, targetDuty);

  if (!res.ok) {
    if (autotuneDebug >= 1) {
      const char *abortTag =
        (dir == PW_LIMIT_LOW) ? "[PW_LOW_ABORT_NO_SIGNAL_V2]" : "[PW_HIGH_ABORT_NO_SIGNAL_V2]";
      uint16_t keepPW =
        (dir == PW_LIMIT_LOW) ? PW_LOW_LIMIT[voiceIdx] : PW_HIGH_LIMIT[voiceIdx];
      Serial.println((String)abortTag +
                     (String)" note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" keeping_PW=" + keepPW);
    }
    return;
  }

  // Log result and commit it in the same style as the original function.
  double targetDutyPercent =
    (dir == PW_LIMIT_LOW)
      ? (kPWLowDutyFraction * 100.0)
      : ((1.0 - kPWHighDutyFraction) * 100.0);
  double targetHighDutyPercent = kPWHighDutyFraction * 100.0;

  if (autotuneDebug >= 1) {
    const char *resultTag =
      (dir == PW_LIMIT_LOW) ? "[PW_LOW_RESULT_V2]" : "[PW_HIGH_RESULT_V2]";
    Serial.println((String)resultTag +
                   (String)" note=" + DCO_calibration_current_note +
                   (String)" DCO=" + currentDCO +
                   (String)" PW_LIMIT=" + res.limitPW +
                   (String)" duty≈" + res.finalDutyPercent + "%" +
                   (String)" targetDuty=" + targetDutyPercent + "%" +
                   (dir == PW_LIMIT_LOW
                      ? (String)""
                      : (String)" targetHighDuty=" + targetHighDutyPercent + "%"));
  }

  if (dir == PW_LIMIT_LOW) {
    Serial.println("--------------------------------");
    Serial.println("PW low limit (v2) found !!!");
    Serial.println(
      (String)" PW_LIMIT=" + res.limitPW +
      (String)" duty≈" + res.finalDutyPercent + "%" +
      (String)" targetDuty=" + (kPWLowDutyFraction * 100.0) + "%");
    Serial.println("--------------------------------");
    update_FS_PW_Low_Limit(voiceIdx, res.limitPW);
    PW_LOW_LIMIT[voiceIdx] = res.limitPW;
  } else {
    Serial.println("--------------------------------");
    Serial.println("PW high limit (v2) found !!!");
    Serial.println(
      (String)" PW_LIMIT=" + res.limitPW +
      (String)" duty≈" + res.finalDutyPercent + "%" +
      (String)" targetDuty=" + ((1.0 - kPWHighDutyFraction) * 100.0) + "%" +
      (String)" targetHighDuty=" + (kPWHighDutyFraction * 100.0) + "%");
    Serial.println("--------------------------------");
    update_FS_PW_High_Limit(voiceIdx, res.limitPW);
    PW_HIGH_LIMIT[voiceIdx] = res.limitPW;
  }
}

//////////////////////////////////////////////////////////////////////////////
// Measure duty-cycle error on DCO_calibration_pin by timing rising/falling
// edges. Returns 0 when duty is ≈50%, or kGapTimeoutSentinel on timeout.
float find_gap(byte specialMode) {
  if (specialMode == 2) {  // find lowest freq mode
    samplesNumber = 12;
  } else {
    samplesNumber = 6;
  }

  // Estimate ideal period for the current note so we can reject obviously
  // invalid edge intervals (e.g. very short glitches) that do not match the
  // DCO's actual frequency.
  double freqHz = (double)sNotePitches[DCO_calibration_current_note - 12];
  double idealPeriodUs = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;
  double dtMinUs = 0.0;
  double dtMaxUs = 0.0;
  if (idealPeriodUs > 0.0) {
    // Accept any segment between ~1% and ~99% of the ideal period. This covers
    // extreme duty cycles (2%/98%) while rejecting very short/high-frequency
    // glitches that are clearly not the fundamental.
    dtMinUs = idealPeriodUs * 0.01;
    dtMaxUs = idealPeriodUs * 0.99;
    if (dtMinUs < (double)kEdgeDebounceMinUs) {
      dtMinUs = (double)kEdgeDebounceMinUs;
    }
    if (dtMaxUs > (double)kGapTimeoutUs) {
      dtMaxUs = (double)kGapTimeoutUs;
    }
  }

  // Reset edge-timing accumulators and counters at the start of each
  // measurement to avoid leaking partial sums from previous calls.
  pulseCounter         = 0;
  samplesCounter       = 0;
  risingEdgeTimeSum    = 0;
  fallingEdgeTimeSum   = 0;
  edgeDetectionLastVal = 0;

  // Local counters for how many rising/falling segments we actually measured.
  uint16_t risingCount  = 0;
  uint16_t fallingCount = 0;

  edgeDetectionLastTime = micros();

  while (samplesCounter < samplesNumber) {

    bool rawVal = digitalRead(DCO_calibration_pin);
    // Compensate for hardware polarity if needed so that 'val == 1' always
    // represents the same logical DCO level for duty measurements.
    bool val = kGapPolarityInverted ? !rawVal : rawVal;
    microsNow = micros();
    if ((microsNow - edgeDetectionLastTime) > kGapTimeoutUs) {

      pulseCounter = 0;
      samplesCounter = 0;
      DCO_calibration_difference = kGapTimeoutSentinel;
      val = 0;
      edgeDetectionLastVal = 0;

      if (autotuneDebug >= 3) {
        uint16_t pwRaw = g_lastPWMeasurementRaw;
        Serial.println((String)"[GAP_TIMEOUT] note=" + DCO_calibration_current_note +
                       (String)" DCO=" + currentDCO +
                       (String)" PW_raw=" + pwRaw +
                       (String)" ampComp=" + ampCompCalibrationVal);
      }

      microsNow = micros();
      edgeDetectionLastTime = microsNow;

      return kGapTimeoutSentinel;
    }
    if (val != edgeDetectionLastVal) {
      if ((microsNow - edgeDetectionLastTime) >= kEdgeDebounceMinUs) {

        edgeDetectionLastVal = val;

        if (pulseCounter == 1 && val == 0) {
          pulseCounter == 0;
        }
        if (pulseCounter > 2) {
          uint32_t dt = microsNow - edgeDetectionLastTime;
          bool intervalOk = true;
          if (idealPeriodUs > 0.0) {
            // Reject intervals that are incompatible with the ideal period.
            // This prevents very short spurious edges from corrupting the
            // duty measurement at low frequencies.
            if ((double)dt < dtMinUs || (double)dt > dtMaxUs) {
              intervalOk = false;
            }
          }

          if (intervalOk) {
          if (val == 0) {
              fallingEdgeTimeSum += dt;
              fallingCount++;
          } else {
              risingEdgeTimeSum += dt;
              risingCount++;
          }
          samplesCounter++;
          }
        }
        edgeDetectionLastTime = microsNow;
        pulseCounter++;
      }
    }
  }

  if (samplesCounter == samplesNumber) {

    // Compute average low and high segment durations directly from the number
    // of segments we actually accumulated.
    float avgLowUs  = (fallingCount  > 0) ? (float)fallingEdgeTimeSum  / (float)fallingCount  : 0.0f;
    float avgHighUs = (risingCount   > 0) ? (float)risingEdgeTimeSum   / (float)risingCount   : 0.0f;

    // Derived period and direct HIGH-duty estimate based purely on measured
    // low/high portions. Duty cycle is defined in la literatura como el
    // porcentaje de tiempo en nivel ALTO (HIGH) durante un período.
    float measuredPeriodUs = avgLowUs + avgHighUs;
    float dutyMeasuredFrac = (measuredPeriodUs > 0.0f) ? (avgHighUs / measuredPeriodUs) : 0.0f;

    // Positive DCO_calibration_difference now means HIGH segment longer than
    // LOW (duty > 50%); negative means LOW segment longer (duty < 50%).
    // This keeps the relation:
    //   duty_high - 0.5 = DCO_calibration_difference / (2 * periodUs)
    DCO_calibration_difference = avgHighUs - avgLowUs;

    if (autotuneDebug >= 2) {
      // Log raw gap measurement with context: which mode, note/DCO, the
      // current amplitude compensation value, the last PW we explicitly set,
      // and the inferred duty/target duty if a period is available.
      uint16_t pwRaw = g_lastPWMeasurementRaw;

      // Duty estimate using the same "diff vs ideal period" method used by
      // the PW search code.
      double dutyPercentIdeal = 0.0;
      double targetDutyPercent = g_gapLogTargetDutyFraction * 100.0;
      if (g_gapLogCurrentPeriodUs > 0.0) {
        double dutyErrorFrac = (double)DCO_calibration_difference / (2.0 * g_gapLogCurrentPeriodUs);
        dutyPercentIdeal = (0.5 + dutyErrorFrac) * 100.0;
      }

      // Direct duty estimate based only on measured low/high times.
      double dutyPercentMeasured = dutyMeasuredFrac * 100.0;

      Serial.println((String)"[GAP_MEASURE] mode=" + specialMode +
                     (String)" note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" AMP=" + ampCompCalibrationVal +
                     (String)" PW_raw=" + pwRaw +
                     (String)" diff=" + DCO_calibration_difference +
                     (String)" avgLowUs=" + avgLowUs +
                     (String)" avgHighUs=" + avgHighUs +
                     (String)" T_meas=" + measuredPeriodUs +
                     (String)" duty_meas≈" + dutyPercentMeasured + "%" +
                     (String)" duty_ideal≈" + dutyPercentIdeal + "%" +
                     (String)" targetDuty=" + targetDutyPercent + "%");
    }

    
    pulseCounter = 0;
    samplesCounter = 0;
    risingEdgeTimeSum = 0;
    fallingEdgeTimeSum = 0;
    edgeDetectionLastVal = 0;

  } else {
    return kGapTimeoutSentinel;
  }
  return (float)DCO_calibration_difference;
}

/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/

// Debug helper used during manual calibration: measure and report the
// duty-cycle difference from the target duty (normally 50%) for the
// current note/DCO. The result is sent to the mainboard as a 32-bit
// PARAM_GAP_FROM_DCO value, which the screen shows as "GAP".
void DCO_calibration_debug() {
  // Reuse the main gap-measurement path (which already handles polarity,
  // debouncing, and timeouts) so manual calibration sees the same notion
  // of "gap" as the automatic routines.
  GapMeasurement gm = measure_gap(0);  // target is 50% duty

  // Compute duty error relative to the center target (0.5) using the
  // *ideal* period for the current note. For manual trimming this is
  // sufficient and keeps the math simple.
  double dutyErrorPercentTimes100 = 0.0;  // duty error [%] * 100

  if (!gm.timedOut) {
    double freqHz = (double)sNotePitches[DCO_calibration_current_note - 12];
    if (freqHz > 0.0) {
      double periodUs = 1000000.0 / freqHz;
      // gm.value is the low-vs-high time difference (avgLowUs - avgHighUs).
      // For a perfect 50% duty, low and high are equal, so gm.value == 0.
      // Duty error fraction from 50% is thus:
      //   duty_low - 0.5 = (avgLowUs - avgHighUs) / (2 * periodUs)
      double dutyErrorFrac = (double)gm.value / (2.0 * periodUs);
      double dutyErrorPercent = dutyErrorFrac * 100.0;
      // Scale by 100 for two decimal digits of resolution on the screen.
      dutyErrorPercentTimes100 = dutyErrorPercent * 100.0;
    }
  } else {
    // On timeout, propagate a large sentinel so the UI can tell that the
    // signal is invalid/out of range instead of near 0%.
    dutyErrorPercentTimes100 = 0;  // or some large sentinel if preferred
  }

  if (autotuneDebug >= 1) {
    Serial.println((String)"[MANUAL_GAP] note=" + DCO_calibration_current_note +
                   (String)" DCO=" + currentDCO +
                   (String)" AMP=" + ampCompCalibrationVal +
                   (String)" gapUs=" + gm.value +
                   (String)" dutyErr(%)≈" + (dutyErrorPercentTimes100 / 100.0));
  }

  // Send as a 32-bit PARAM_GAP_FROM_DCO value through the standard
  // param protocol. With ENABLE_SCREEN_UART this goes direct to Screen;
  // otherwise Serial2 → Mainboard (which forwards to Screen).
  serialSendParam32(PARAM_GAP_FROM_DCO,
                    (int32_t)dutyErrorPercentTimes100);
}

#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/cv_out.ino"
// Phase 2: Mainboard setPWMOuts math → software VCA/VCF/reso levels (no HW PWM yet).
#include "include_all.h"

static inline uint16_t cv_lerp_u16(uint16_t value, uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1) {
  if (x1 == x0) return y0;
  return (uint16_t)(y0 + ((int32_t)(y1 - y0) * (int32_t)(value - x0)) / (int32_t)(x1 - x0));
}

// Boot: build AS2164 VCA linearize table (same control points as Mainboard).
void init_cv_out() {
  generateBezierArray({ 0, 4095 }, { 4095, 0 }, { 150, 1420 }, { -235, 815 }, 4096, AS2164_VCA_linearize_table);
  cv_update_mod_formulas();
}

// Recompute LFO1→VCA / EnvVCF→VCF / LFO2→VCF depth scalars.
void cv_update_mod_formulas() {
  ADSR2toVCF_formula = (1.0f / 512.0f) * (float)ADSR2toVCF;
  LFO2toVCF_formula = (1.0f / 512.0f) * (float)LFO2toVCF;
  LFO1toVCA_formula = (1.0f / 512.0f) * (float)LFO1toVCA;
}

// Hot path (~10 kHz with ADSR): EnvVCA/EnvVCF + LFO + keytrack/vel → soft CV levels.
void update_CV_outs() {
  static constexpr int DEFAULT_VCA_COMPENSATION = 100;

  if (timer1msFlag) {
    if (RESONANCEAmpCompensation) {
      static constexpr int MAX_RESONANCE = 2300;
      static constexpr int MIN_RESONANCE = 50;
      static constexpr int MAX_VCA_COMPENSATION = 315;
      static constexpr float COMPENSATION_FACTOR = 0.14f;
      (void)MAX_RESONANCE;
      VCAResonanceCompensation =
        (RESONANCE >= MIN_RESONANCE)
          ? (int16_t)(MAX_VCA_COMPENSATION - ((RESONANCE - MIN_RESONANCE) * COMPENSATION_FACTOR))
          : (int16_t)MAX_VCA_COMPENSATION;
    } else {
      VCAResonanceCompensation = DEFAULT_VCA_COMPENSATION;
    }

    if (VCFKeytrack != 0) {
      for (byte i = 0; i < NUM_VOICES_TOTAL; i++) {
        VCFKeytrackPerVoice[i] =
          1.00f + (float)(VCFKeytrackModifier * map(VOICE_NOTES[i], 0, 150, -60, 90));
      }
    } else {
      for (byte i = 0; i < NUM_VOICES_TOTAL; i++) {
        VCFKeytrackPerVoice[i] = 1.0f;
      }
    }

    if (analogDrift != 0) {
      for (byte i = 0; i < NUM_VOICES_TOTAL; i++) {
        // Monosynth: use osc-0 drift LFO (Mainboard used per-voice drift).
        VCF_DRIFT[i] = (float)LFO_DRIFT_LEVEL[0] * 0.002f * (float)analogDrift;
      }
    } else {
      for (byte i = 0; i < NUM_VOICES_TOTAL; i++) {
        VCF_DRIFT[i] = 0.0f;
      }
    }
  }

  const int16_t LFO1toVCA_calc = (int16_t)((float)LFO1Level * LFO1toVCA_formula);
  const float LFO2toVCF_mod = (float)LFO2Level * LFO2toVCF_formula;

  for (byte i = 0; i < NUM_VOICES_TOTAL; i++) {
    float VCA_velocityFactor = 1.0f;
    if (velocityToVCAVal != 0) {
      VCA_velocityFactor = 1.0f - ((float)velocityToVCA * (127 - midi_velocity[i]));
    }

    int16_t LFO1toVCA_current = (ADSR_VCA_Level[i] == 0) ? 0 : LFO1toVCA_calc;
    uint16_t VCA_Calculated =
      (uint16_t)constrain((float)(ADSR_VCA_Level[i] + LFO1toVCA_current) * VCA_velocityFactor, 0, 4095);
    VCA_PWM[i] = cv_lerp_u16(AS2164_VCA_linearize_table[VCA_Calculated], 0, 4095,
                             (uint16_t)VCAResonanceCompensation, (uint16_t)(4095 - VCALevel));

    float VCF_velocityFactor = 1.0f;
    if (velocityToVCFVal != 0) {
      VCF_velocityFactor = 1.0f - ((float)velocityToVCF * (127 - midi_velocity[i]));
    }
    float ADSR2toVCFcalculated = (float)ADSR_VCF_Level[i] * ADSR2toVCF_formula;
    float combinedValue = ADSR2toVCFcalculated + LFO2toVCF_mod + (float)CUTOFF + VCF_DRIFT[i];
    float finalValue = combinedValue * VCF_velocityFactor * VCFKeytrackPerVoice[i];
    VCF_PWM[i] = (uint16_t)(4095 - (int)constrain(finalValue, 0, 4095));
  }

  RESONANCE_PWM = RESONANCE;
#ifdef ENABLE_CV_OUTS
  write_cv_pwm();
#endif
}

#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/mcp4728_dco.ino"
// Minimal MCP4728 FastWrite driver (Mainboard mcpUpdate channel map).
// Addresses 0x63 / 0x64 / 0x65 — monosynth remap still TBD (PINOUT open item).

#ifdef ENABLE_MCP4728

#include <Wire.h>

static constexpr uint8_t MCP4728_ADDR_1 = 0x63;
static constexpr uint8_t MCP4728_ADDR_2 = 0x64;
static constexpr uint8_t MCP4728_ADDR_3 = 0x65;

// Fast Write: 8 bytes, channels A→D, PD=00, 12-bit value.
static void mcp4728_fastWrite(uint8_t addr, uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
  Wire.beginTransmission(addr);
  auto put = [](uint16_t v) {
    v &= 0x0FFF;
    Wire.write((uint8_t)((v >> 8) & 0x0F));
    Wire.write((uint8_t)(v & 0xFF));
  };
  put(a);
  put(b);
  put(c);
  put(d);
  Wire.endTransmission();
}

void init_MCP4728() {
  Wire.setSDA(MCP4728_SDA_PIN);
  Wire.setSCL(MCP4728_SCL_PIN);
  Wire.setClock(1000000);
  Wire.begin();

  // Idle high (same as Mainboard boot).
  mcp4728_fastWrite(MCP4728_ADDR_1, 4095, 4095, 4095, 4095);
  mcp4728_fastWrite(MCP4728_ADDR_2, 4095, 4095, 4095, 4095);
  mcp4728_fastWrite(MCP4728_ADDR_3, 4095, 4095, 4095, 4095);
}

// Push SQR1/SQR2/Sub levels — Mainboard channel comments preserved.
void mcpUpdate() {
  // V1 OSC1, V2 OSC2, V2 OSC1, V3 OSC2
  mcp4728_fastWrite(MCP4728_ADDR_1, SQR1Level, SQR2Level, SQR1Level, SQR2Level);
  // V3 OSC1, V4 OSC2, V4 OSC1, SUB3
  mcp4728_fastWrite(MCP4728_ADDR_2, SQR1Level, SQR2Level, SQR1Level, SubLevel);
  // SUB4, SUB1, SUB2, V1 OSC2
  mcp4728_fastWrite(MCP4728_ADDR_3, SubLevel, SubLevel, SubLevel, SQR2Level);
}

#else  // !ENABLE_MCP4728

void init_MCP4728() {}
void mcpUpdate() {}

#endif

#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/midi.ino"

// Init USB + DIN MIDI ports and register note/CC/program/pitch-bend handlers. Called from setup().
void init_midi() {
  MIDI_USB.begin(MIDI_CHANNEL_OMNI);
  MIDI_USB.setHandleNoteOn(handleNoteOn);
  MIDI_USB.setHandleNoteOff(handleNoteOff);
  MIDI_USB.setHandleControlChange(handleControlChange);
  MIDI_USB.setHandleProgramChange(handleProgramChange);
  MIDI_USB.setHandlePitchBend(handlePitchBend);


  MIDI_SERIAL.begin(MIDI_CHANNEL_OMNI);
  MIDI_SERIAL.setHandleNoteOn(handleNoteOn);
  MIDI_SERIAL.setHandleNoteOff(handleNoteOff);
  MIDI_SERIAL.setHandleControlChange(handleControlChange);
  MIDI_SERIAL.setHandleProgramChange(handleProgramChange);
  MIDI_SERIAL.setHandlePitchBend(handlePitchBend);
}


// MIDI library callback → note_on(). Invoked from loop via MIDI_*.read().
void handleNoteOn(byte channel, byte pitch, byte velocity) {
  note_on(pitch, velocity);
}
// MIDI library callback → note_off().
void handleNoteOff(byte channel, byte pitch, byte velocity) {
  note_off(pitch);
}

// MIDI CC handler (CC 42 sets pitch-bend range in semitones and updates Q24 multiplier).
void handleControlChange(byte channel, byte number, byte value) {
  // CC #42 is used to set the pitch bend range in semitones.
  if (number == 42) {
    pitchBendRange = value;
    // Optimized: Use fast fixed-point multiplication instead of float division.
    pitchBendMultiplier_q24 = (int32_t)(((int64_t)pitchBendRange * RECIP_TWELVE_Q24));
    pitchBendMultiplier = (float)pitchBendMultiplier_q24 / (float)(1 << 24);
  }
}

// MIDI program-change callback (currently unused / empty).
void handleProgramChange(byte channel, byte program) {
}

// MIDI pitch-bend callback → midi_pitch_bend (offset to 0..16383 style).
void handlePitchBend(byte channel, int pitchBend) {
  midi_pitch_bend = pitchBend + 8192;
}

// Allocate voice(s) from MIDI note-on per voiceMode/polyMode; set ADSR flags; notify mainboard.
void note_on(uint8_t note, uint8_t velocity) {

  switch (voiceMode) {
    case 0:
      VOICE_NOTES[0] = note;
      midi_velocity[0] = velocity;
      VOICES[0] = millis();
      note_on_flag[0] = 1;
      noteStart[0] = 1;
      serial_send_note_on(0, velocity, note - 36 + OSC1_interval);
      return;

      break;

    case 1:

      if (polyMode == 0) {
        if (STACK_VOICES < 2) {
          for (int i = 0; i < NUM_VOICES; i++)  // REVISAR!!
          {
            if (VOICE_NOTES[i] == note) {
              VOICES[i] = millis();
              midi_velocity[i] = velocity;
              note_on_flag[i] = 1;
              noteStart[i] = 1;
              serial_send_note_on(i, velocity, note - 36 + OSC1_interval);
              return;  // note already playing
            }
          }
        }

        for (int i = 0; i < STACK_VOICES; i++) {  // REVISAR!! Quizas debiera ser NUM_VOICES y no STACK
          uint8_t voice_num = get_free_voice();
          VOICES[voice_num] = millis();
          VOICE_NOTES[voice_num] = note;
          midi_velocity[voice_num] = velocity;
          note_on_flag[voice_num] = 1;
          noteStart[voice_num] = 1;
          serial_send_note_on(voice_num, velocity, note - 36 + OSC1_interval);
        }
      }

      if (polyMode == 1) {
        if (STACK_VOICES < 2) {
          for (int i = 0; i < NUM_VOICES; i++)  // REVISAR!!
          {
            if (VOICE_NOTES[i] == note) {
              VOICES[i] = 1;
              midi_velocity[i] = velocity;
              note_on_flag[i] = 1;
              noteStart[i] = 1;
              noteEnd[i] = 0;
              serial_send_note_on(i, velocity, note - 36 + OSC1_interval);
              return;  // note already playing
            }
          }
        }

        uint8_t voice_num = get_free_voice_sequential();
        VOICES[voice_num] = 1;
        VOICE_NOTES[voice_num] = note;
        midi_velocity[voice_num] = velocity;
        note_on_flag[voice_num] = 1;
        noteStart[voice_num] = 1;
        noteEnd[voice_num] = 0;
        serial_send_note_on(voice_num, velocity, note - 36 + OSC1_interval);
      }
      break;

    case 2:
      for (int i = 0; i < NUM_VOICES_TOTAL; i++)  // REVISAR!! // Previously NUM_VOICES
      {
        VOICES[i] = 1;
        VOICE_NOTES[i] = note;
        midi_velocity[i] = velocity;
        note_on_flag[i] = 1;
        noteStart[i] = 1;
        serial_send_note_on(i, velocity, note - 36 + OSC1_interval);
      }
      break;
    default:
      return;
      break;
  }
  last_midi_pitch_bend = 0;
}

// Release matching voice(s) on MIDI note-off; set noteEnd flags; notify mainboard.
void note_off(uint8_t note) {
  // gate off
  for (int i = 0; i < NUM_VOICES_TOTAL; i++)  // REVISAR!! // Previously NUM_VOICES
  {
    if (VOICE_NOTES[i] == note) {
      // gpio_put(GATE_PINS[i], 0);
      // VOICE_NOTES[i] = 0;
      VOICES[i] = 0;
      noteEnd[i] = 1;
      noteStart[i] = 0;
      serial_send_note_off(i);
    }
  }
}
#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/params.ino"
// Central parameter router for shared parameters.
//
// This module maps numeric parameter IDs (used over Serial / between MCUs)
// to concrete synth state changes on this MCU. It is the single place where
// "what does parameter X actually do?" is implemented.
//
// The stable parameter ID definitions live in params_def.h so all MCUs and
// tools can share the same mapping.
//
// High-level flow:
//   1) Some control source (front panel, STM32, MIDI, editor) decides that
//      parameter P should change to value V.
//   2) It sends P and V over the link (e.g. via Serial 'p'/'w'/'x' commands).
//   3) The receiver ends up calling:
//         update_parameters(paramNumber, paramValue);
//   4) update_parameters() looks up paramNumber in paramTable[] and calls
//      the corresponding apply_param_*() function.
//   5) That function updates internal state and performs any required
//      precomputations (fixed-point scales, LFO frequencies, etc.).
//
// How to add or modify a parameter on this MCU:
//   1) Define or reuse a ParamId in params_def.h.
//   2) Implement a new apply_param_*() function below that:
//        - Accepts int16_t (the raw transport value).
//        - Updates the appropriate globals / DSP structures.
//        - Computes any derived values (e.g. Q24 scales, Hz values).
//   3) Add an entry to paramTable[] that maps your ParamId to the new
//      apply_param_*() function.
//   4) Make sure the sending side (other MCU / UI) uses the same ParamId,
//      and sends values in the range/format your apply function expects.
//
// Notes:
//   - The transport is 16-bit (int16_t) for this router. For values derived
//     from larger types (e.g. 'x' 32-bit commands), the conversion happens
//     before calling update_parameters(), preserving the old behavior.
//   - If an unknown paramNumber is received, update_parameters() simply
//     ignores it (same as the old default: case).


// ---- Apply functions for each parameter (invoked only via paramTable / update_parameters) ----

// PARAM_SAW_STATUS: wave mux (74HC595 when ENABLE_WAVE_MUX).
static void apply_param_saw_status(int16_t v) {
  sawStatus = (v != 0);
  update_waveSelector(0);
}

static void apply_param_saw2_status(int16_t v) {
  saw2Status = (v != 0);
  update_waveSelector(1);
}

static void apply_param_tri_status(int16_t v) {
  triStatus = (v != 0);
  update_waveSelector(2);
}

static void apply_param_sine_status(int16_t v) {
  sineStatus = (v != 0);
  update_waveSelector(3);
}

// PARAM_SQR1_STATUS: OSC1 square/wave enable status.
static void apply_param_sqr1_status(int16_t v) {
  sqr1Status = v;
}

static void apply_param_sqr2_status(int16_t v) {
  sqr2Status = (v != 0);
  update_waveSelector(3);
}

static void apply_param_resonance_comp(int16_t v) {
  RESONANCEAmpCompensation = (v != 0);
}

static void apply_param_vca_adsr_restart(int16_t v) {
  VCAADSRRestart = (v != 0);
  ADSR_VCA_set_restart();
}

static void apply_param_vcf_adsr_restart(int16_t v) {
  VCFADSRRestart = (v != 0);
  ADSR_VCF_set_restart();
}

// PARAM_ADSR3_TO_OSC_SELECT: which osc(s) receive ADSR3→detune/PWM routing.
static void apply_param_adsr3_to_osc_select(int16_t v) {
  ADSR3ToOscSelect = v;
}

// PARAM_LFO1_WAVEFORM: set LFO1 waveform and refresh rate.
static void apply_param_lfo1_waveform(int16_t v) {
  LFO1Waveform = v;
  LFO1_class.setWaveForm(LFO1Waveform);
  LFO1_class.setMode0Freq((float)LFO1Speed, micros());
}

// PARAM_LFO2_WAVEFORM: set LFO2 waveform and refresh rate.
static void apply_param_lfo2_waveform(int16_t v) {
  LFO2Waveform = v;
  LFO2_class.setWaveForm(LFO2Waveform);
  LFO2_class.setMode0Freq((float)LFO2Speed, micros());
}

// PARAM_OSC1_INTERVAL: OSC1 transpose interval.
static void apply_param_osc1_interval(int16_t v) {
  OSC1_interval = v;
}

// PARAM_OSC2_INTERVAL: OSC2 transpose interval.
static void apply_param_osc2_interval(int16_t v) {
  OSC2_interval = v;
}

// PARAM_OSC3_INTERVAL: OSC3 transpose interval.
static void apply_param_osc3_interval(int16_t v) {
  OSC3_interval = v;
}

// PARAM_OSC2_DETUNE_VAL: OSC2 fine detune (stored inverted from UI value).
static void apply_param_osc2_detune_val(int16_t v) {
  OSC2DetuneVal = 512 - v;
}

// PARAM_OSC3_DETUNE_VAL: OSC3 fine detune (stored inverted from UI value).
static void apply_param_osc3_detune_val(int16_t v) {
  OSC3DetuneVal = 512 - v;
}

// PARAM_LFO2_TO_DETUNE2: LFO2 → OSC2 detune depth (Q24).
static void apply_param_lfo2_to_detune2(int16_t v) {
  float lfo2_amt = (float)expConverterFloat((uint8_t)v, 500) / 275000.0f;
  LFO2toDETUNE2_q24 = (int32_t)(lfo2_amt * (float)(1 << 24) + 0.5f);
}

// PARAM_LFO2_TO_DETUNE3: LFO2 → OSC3 detune depth (Q24).
static void apply_param_lfo2_to_detune3(int16_t v) {
  float lfo2_amt = (float)expConverterFloat((uint8_t)v, 500) / 275000.0f;
  LFO2toDETUNE3_q24 = (int32_t)(lfo2_amt * (float)(1 << 24) + 0.5f);
}

// PARAM_OSC_SYNC_MODE: osc sync / phase-align (updates phaseAlignOSC2, retriggers notes).
static void apply_param_osc_sync_mode(int16_t v) {
  oscSync = v;
  if (oscSync < 2) {
    for (int i = 0; i < NUM_OSCILLATORS; i++) {
      pio_sm_put(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i], pioPulseLength);
      pio_sm_exec(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i], pio_encode_pull(false, false));
      pio_sm_exec(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i], pio_encode_out(pio_y, 31));
    }
  } else {
    if (oscSync > 8) {
      phaseAlignOSC2 = oscSync * 2;
    } else {
      switch (oscSync) {
        case 2:
          phaseAlignOSC2 = 45;
          break;
        case 3:
          phaseAlignOSC2 = 90;
          break;
        case 4:
          phaseAlignOSC2 = 135;
          break;
        case 5:
          phaseAlignOSC2 = 180;
          break;
        case 6:
          phaseAlignOSC2 = 225;
          break;
        case 7:
          phaseAlignOSC2 = 270;
          break;
        case 8:
          phaseAlignOSC2 = 315;
          break;
        default:
          break;
      }
    }
  }
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    note_on_flag[i] = 1;
  }
}

// PARAM_PORTAMENTO_TIME: map UI value to portamento_time (µs-scale glide).
static void apply_param_portamento_time(int16_t v) {
  uint8_t portaSerial = (uint8_t)v;
  if (portaSerial == 0) {
    portamento_time = 0;
  } else if (portaSerial < 200) {
    portamento_time = (expConverter(portaSerial + 15, 100) * 2000);
  } else {
    portamento_time = map(portaSerial, 200, 255, 1000000, 10000000);
  }
}

static void apply_param_vcf_keytrack(int16_t v) {
  VCFKeytrack = v;
  if (VCFKeytrack != 0) {
    VCFKeytrackModifier = (float)VCFKeytrack / 8000.0f;
  } else {
    VCFKeytrackModifier = 1.0f;
  }
}

static void apply_param_velocity_to_vcf(int16_t v) {
  velocityToVCFVal = (int8_t)v;
  velocityToVCF = velocityToVCFVal * 0.0003935f;
}

static void apply_param_velocity_to_vca(int16_t v) {
  velocityToVCAVal = (int8_t)v;
  velocityToVCA = velocityToVCAVal * 0.0003935f;
}

static void apply_param_sqr1_level(int16_t v) {
  SQR1LevelVal = v;
  if (SQR1LevelVal < 0) SQR1LevelVal = 0;
  if (SQR1LevelVal > 128) SQR1LevelVal = 128;
  SQR1Level = (uint16_t)constrain((int)SQR1LevelVal * 32, 0, 4095);
  mcpUpdate();
}

static void apply_param_sqr2_level(int16_t v) {
  SQR2LevelVal = v;
  if (SQR2LevelVal < 0) SQR2LevelVal = 0;
  if (SQR2LevelVal > 128) SQR2LevelVal = 128;
  SQR2Level = (uint16_t)constrain((int)SQR2LevelVal * 32, 0, 4095);
  mcpUpdate();
}

static void apply_param_sub_level(int16_t v) {
  SubLevelVal = v;
  SubLevel = (uint16_t)constrain((int)SubLevelVal * 32, 0, 4095);
  mcpUpdate();
}

// PARAM_PORTAMENTO_MODE: 0 = fixed-time glide, else slew-rate.
static void apply_param_portamento_mode(int16_t v) {
  // Portamento mode: 0 = fixed-time glide, 1 = analog-style slew-rate
  portamento_mode = (v == 0) ? PORTA_MODE_TIME : PORTA_MODE_SLEW;
}

// PARAM_CALIBRATION_VALUE: reserved ID (no behavior).
static void apply_param_calibration_value(int16_t /*v*/) {
  // Placeholder: original code did nothing but kept the ID reserved.
}

// PARAM_VOICE_MODE: mono/poly/stack → setVoiceMode().
static void apply_param_voice_mode(int16_t v) {
  voiceMode = v;
  setVoiceMode();
}

// PARAM_UNISON_DETUNE: unison detune amount.
static void apply_param_unison_detune(int16_t v) {
  unisonDetune = v;
}

// PARAM_ANALOG_DRIFT_AMOUNT: drift modulation depth.
static void apply_param_analog_drift_amount(int16_t v) {
  analogDrift = v;
}

// PARAM_ANALOG_DRIFT_SPEED: recompute all drift LFO rates.
static void apply_param_analog_drift_speed(int16_t v) {
  analogDriftSpeed = v;
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    LFO_DRIFT_SPEED_OFFSET[i] =
      (float)(1.00f - (float)((float)analogDriftSpread * 0.005f) +
              (float)((float)analogDriftSpread * 0.00125f * (float)i)) *
      (float)expConverterFloat((float)analogDriftSpeed, 5000);
    LFO_DRIFT_CLASS[i].setMode0Freq(LFO_DRIFT_SPEED_OFFSET[i], micros());
  }
}

// PARAM_ANALOG_DRIFT_SPREAD: recompute per-osc drift speed offsets.
static void apply_param_analog_drift_spread(int16_t v) {
  analogDriftSpread = v;
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    LFO_DRIFT_SPEED_OFFSET[i] =
      (float)(1.00f - (float)((float)analogDriftSpread * 0.005f) +
              (float)((float)analogDriftSpread * 0.00125f * (float)i)) *
      (float)expConverterFloat((float)analogDriftSpeed, 5000);
    LFO_DRIFT_CLASS[i].setMode0Freq(LFO_DRIFT_SPEED_OFFSET[i], micros());
  }
}

// PARAM_SYNC_MODE: PIO sync topology → setSyncMode().
static void apply_param_sync_mode(int16_t v) {
  syncMode = v;
  setSyncMode();
}

// PARAM_LFO1_TO_DCO: LFO1 → DCO detune depth (float + Q24).
static void apply_param_lfo1_to_dco(int16_t v) {
  LFO1toDCOVal = v;
  // Compute LFO1->DCO modulation depth both in float (for any legacy use)
  // and in Q24 fixed-point for the fast detune path.
  float lfo1_amt = (float)expConverterFloat(LFO1toDCOVal, 500) / 275000.0f;
  LFO1toDCO = lfo1_amt;
  LFO1toDCO_q24 = (int32_t)(lfo1_amt * (float)(1 << 24) + 0.5f);
}

// PARAM_LFO1_SPEED: LFO1 rate in Hz (via expConverterFloat).
static void apply_param_lfo1_speed(int16_t v) {
  LFO1SpeedVal = v;
  LFO1Speed = expConverterFloat(LFO1SpeedVal, 5000);
  LFO1_class.setMode0Freq((float)LFO1Speed, micros());
}

// PARAM_LFO2_SPEED: LFO2 rate in Hz (via expConverterFloat).
static void apply_param_lfo2_speed(int16_t v) {
  LFO2SpeedVal = v;
  LFO2Speed = expConverterFloat(LFO2SpeedVal, 5000);
  LFO2_class.setMode0Freq((float)LFO2Speed, micros());
}

static void apply_param_vca_level(int16_t v) {
  VCALevel = (uint16_t)constrain((int)v, 0, 4095);
}

static void apply_param_lfo1_to_vca(int16_t v) {
  LFO1toVCA = (uint16_t)constrain((int)v, 0, 4095);
  cv_update_mod_formulas();
}

// PARAM_LFO2_TO_PW: LFO2 → pulse-width depth.
static void apply_param_lfo2_to_pw(int16_t v) {
  LFO2toPW = (int16_t)v;
}

// PARAM_ADSR3_TO_PWM: ADSR → PWM depth (centered around 512).
static void apply_param_adsr1_to_pwm(int16_t v) {
  ADSR1toPWM = (int16_t)v - 512;
}

// PARAM_ADSR3_TO_DETUNE1: ADSR → pitch detune depth + precomputed Q24 scale.
static void apply_param_adsr1_to_detune1(int16_t v) {
  // ADSR1toDETUNE1 controls how much ADSR1 modulates pitch (detune).
  // Original float formula was:
  //   ADSRModifier = linToLogLookup[level] * (ADSR1toDETUNE1 / 1080000.0f)
  // We now precompute a Q24 scale factor so the per-voice path stays fixed-point:
  //   ADSRModifier_q24 = linToLogLookup[level] * ADSR1toDETUNE1_scale_q24
  ADSR1toDETUNE1 = (int16_t)v;
  if (ADSR1toDETUNE1 == 0) {
    ADSR1toDETUNE1_scale_q24 = 0;
  } else {
    int64_t num = ((int64_t)ADSR1toDETUNE1 << 24);
    // Symmetric rounding toward nearest for positive/negative values
    const int32_t denom = 1080000;
    if (num >= 0) {
      num += (denom / 2);
    } else {
      num -= (denom / 2);
    }
    ADSR1toDETUNE1_scale_q24 = (int32_t)(num / denom);
  }
}

// PARAM_ADSR1_ATTACK_CURVE / DECAY: store for EnvVCA (engines Phase 2).
static void apply_param_adsr1_attack_curve(int16_t v) {
  ADSR1AttackCurveVal = (uint8_t)v;
}

static void apply_param_adsr1_decay_curve(int16_t v) {
  ADSR1DecayCurveVal = (uint8_t)v;
}

static void apply_param_adsr2_attack_curve(int16_t v) {
  ADSR2AttackCurveVal = (uint8_t)v;
}

static void apply_param_adsr2_decay_curve(int16_t v) {
  ADSR2DecayCurveVal = (uint8_t)v;
}

// PARAM_PWM_POTS_CONTROL_MANUAL: manual PWM pot control flag.
static void apply_param_pwm_pots_manual(int16_t v) {
  PWMPotsControlManual = (v != 0);
}

static void apply_param_adsr3_enabled(int16_t v) {
  ADSR3Enabled = (v != 0);
}

// PARAM_FUNCTION_KEY: reserved / handled elsewhere.
static void apply_param_function_key(int16_t /*v*/) {
}

// Gap is generated by DCO (autotune) and TX'd via serialSendParam32 → Screen.
static void apply_param_gap_from_dco(int16_t /*v*/) {
}

// PARAM_CALIBRATION_FLAG: start/stop auto-cal (loop1 runs DCO_calibration when set).
static void apply_param_calibration_flag(int16_t v) {
  calibrationFlag = v;
}

// PARAM_MANUAL_CALIBRATION_FLAG: enter/exit manual cal; rising edge TX offsets to Input/Mainboard.
static void apply_param_manual_calibration_flag(int16_t v) {
  // When manual calibration is active, both flags follow this param.
  // Rising edge (0 -> non-zero): broadcast current offsets upstream (Input hub or Mainboard).
  if (v != 0 && !manualCalibrationFlag) {
    for (uint8_t osc = 0; osc < NUM_OSCILLATORS; ++osc) {
      uint8_t idx    = osc;
      uint8_t offset = (uint8_t)manualCalibrationOffset[osc];
      uint16_t packed = ((uint16_t)idx << 8) | offset;
      // Send as 32-bit frame; receivers use lower 16 bits [index:8|offset:8].      
      serialSendParam32(PARAM_MANUAL_CALIBRATION_OFFSET_FROM_DCO, (uint32_t)packed);
    }
  }

  manualCalibrationFlag = v;
  calibrationFlag       = v;
}

// PARAM_MANUAL_CALIBRATION_STAGE: which osc/stage is being edited in manual cal UI.
static void apply_param_manual_calibration_stage(int16_t v) {
  int8_t stage = (int8_t)v;
  if (stage < 0) stage = 0;
  if (stage >= (int8_t)NUM_OSCILLATORS) stage = (int8_t)(NUM_OSCILLATORS - 1);
  manualCalibrationStage = stage;
}

// PARAM_MANUAL_CALIBRATION_OFFSET: per-osc manual amp offset for current stage.
static void apply_param_manual_calibration_offset(int16_t v) {
  uint8_t stage = (uint8_t)manualCalibrationStage;
  if (stage >= NUM_OSCILLATORS) stage = NUM_OSCILLATORS - 1;
  manualCalibrationOffset[stage] = (int8_t)v;
}

// Explicit "store manual calibration offsets" command. This is called when
// the user confirms manual calibration on the input controller, and is the
// only place where we persist manualCalibrationOffset[] to the filesystem.
static void apply_param_manual_calibration_store(int16_t /*v*/) {
  for (uint8_t osc = 0; osc < NUM_OSCILLATORS; ++osc) {
    update_FS_ManualCalibrationOffset(osc, manualCalibrationOffset[osc]);
  }
}

// ---- Parameter table ------------------------------------------------

static const ParamDescriptorT<int16_t> paramTable[] = {
  { PARAM_SAW_STATUS,                apply_param_saw_status },
  { PARAM_SAW2_STATUS,               apply_param_saw2_status },
  { PARAM_TRI_STATUS,                apply_param_tri_status },
  { PARAM_SINE_STATUS,               apply_param_sine_status },
  { PARAM_SQR1_STATUS,               apply_param_sqr1_status },
  { PARAM_SQR2_STATUS,               apply_param_sqr2_status },
  { PARAM_RESONANCE_COMPENSATION,    apply_param_resonance_comp },
  { PARAM_VCA_ADSR_RESTART,          apply_param_vca_adsr_restart },
  { PARAM_VCF_ADSR_RESTART,          apply_param_vcf_adsr_restart },
  { PARAM_ADSR3_TO_OSC_SELECT,       apply_param_adsr3_to_osc_select },
  { PARAM_LFO1_WAVEFORM,             apply_param_lfo1_waveform },
  { PARAM_LFO2_WAVEFORM,             apply_param_lfo2_waveform },
  { PARAM_OSC1_INTERVAL,             apply_param_osc1_interval },
  { PARAM_OSC2_INTERVAL,             apply_param_osc2_interval },
  { PARAM_OSC3_INTERVAL,             apply_param_osc3_interval },
  { PARAM_OSC2_DETUNE_VAL,           apply_param_osc2_detune_val },
  { PARAM_OSC3_DETUNE_VAL,           apply_param_osc3_detune_val },
  { PARAM_LFO2_TO_DETUNE2,           apply_param_lfo2_to_detune2 },
  { PARAM_LFO2_TO_DETUNE3,           apply_param_lfo2_to_detune3 },
  { PARAM_OSC_SYNC_MODE,             apply_param_osc_sync_mode },
  { PARAM_PORTAMENTO_TIME,           apply_param_portamento_time },
  { PARAM_PORTAMENTO_MODE,           apply_param_portamento_mode },
  { PARAM_VCF_KEYTRACK,              apply_param_vcf_keytrack },
  { PARAM_VELOCITY_TO_VCF,           apply_param_velocity_to_vcf },
  { PARAM_VELOCITY_TO_VCA,           apply_param_velocity_to_vca },
  { PARAM_SQR1_LEVEL,                apply_param_sqr1_level },
  { PARAM_SQR2_LEVEL,                apply_param_sqr2_level },
  { PARAM_SUB_LEVEL,                 apply_param_sub_level },
  { PARAM_CALIBRATION_VALUE,         apply_param_calibration_value },
  { PARAM_VOICE_MODE,                apply_param_voice_mode },
  { PARAM_UNISON_DETUNE,             apply_param_unison_detune },
  { PARAM_ANALOG_DRIFT_AMOUNT,       apply_param_analog_drift_amount },
  { PARAM_ANALOG_DRIFT_SPEED,        apply_param_analog_drift_speed },
  { PARAM_ANALOG_DRIFT_SPREAD,       apply_param_analog_drift_spread },
  { PARAM_SYNC_MODE,                 apply_param_sync_mode },
  { PARAM_LFO1_TO_DCO,               apply_param_lfo1_to_dco },
  { PARAM_LFO1_SPEED,                apply_param_lfo1_speed },
  { PARAM_LFO2_SPEED,                apply_param_lfo2_speed },
  { PARAM_VCA_LEVEL,                 apply_param_vca_level },
  { PARAM_LFO1_TO_VCA,               apply_param_lfo1_to_vca },
  { PARAM_LFO2_TO_PW,                apply_param_lfo2_to_pw },
  { PARAM_ADSR3_TO_PWM,              apply_param_adsr1_to_pwm },
  { PARAM_ADSR3_TO_DETUNE1,          apply_param_adsr1_to_detune1 },
  { PARAM_ADSR1_ATTACK_CURVE,        apply_param_adsr1_attack_curve },
  { PARAM_ADSR1_DECAY_CURVE,         apply_param_adsr1_decay_curve },
  { PARAM_ADSR2_ATTACK_CURVE,        apply_param_adsr2_attack_curve },
  { PARAM_ADSR2_DECAY_CURVE,         apply_param_adsr2_decay_curve },
  { PARAM_PWM_POTS_CONTROL_MANUAL,   apply_param_pwm_pots_manual },
  { PARAM_ADSR3_ENABLED,             apply_param_adsr3_enabled },
  { PARAM_FUNCTION_KEY,              apply_param_function_key },
  { PARAM_CALIBRATION_FLAG,          apply_param_calibration_flag },
  { PARAM_MANUAL_CALIBRATION_FLAG,   apply_param_manual_calibration_flag },
  { PARAM_MANUAL_CALIBRATION_STAGE,  apply_param_manual_calibration_stage },
  { PARAM_MANUAL_CALIBRATION_OFFSET, apply_param_manual_calibration_offset },
  { PARAM_GAP_FROM_DCO,              apply_param_gap_from_dco },
  { PARAM_MANUAL_CALIBRATION_STORE,  apply_param_manual_calibration_store }
};

static const size_t paramTableSize =
  sizeof(paramTable) / sizeof(paramTable[0]);

// Public entry point: called from Serial/MIDI/UI code.
inline void update_parameters(byte paramNumber, int16_t paramValue) {
  param_router_apply<int16_t>(paramTable, paramTableSize,
                              static_cast<uint16_t>(paramNumber),
                              paramValue);
}



#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/state_machines.ino"
// Load frequency_sync_4_jumps into all three PIO blocks and start all voice SMs. Called from setup1().
void init_pio() {

  offset[0] = pio_add_program(pio[0], &frequency_sync_4_jumps_program);
  offset[1] = pio_add_program(pio[1], &frequency_sync_4_jumps_program);
  offset[2] = pio_add_program(pio[2], &frequency_sync_4_jumps_program);
  // offset[0] = pio_add_program(pio[0], &frequency_program);
  // offset[1] = pio_add_program(pio[1], &frequency_program);
  // offset[2] = pio_add_program(pio[2], &frequency_program);
  start_voice_sms();
}

// Configure each DCO SM (sync sideset from syncMode), preload pioPulseLength into Y.
void start_voice_sms() {

  for (int i = 0; i < NUM_OSCILLATORS; i++) {

    uint8_t sidesetPin;
    switch (syncMode) {
      case 0:
        // Free-running: self sideset (same policy as setSyncMode)
        sidesetPin = RESET_PINS[i];
        break;
      case 1:
        // OSC2 syncs from OSC1; OSC3 free-running
        if (i == 1) {
          sidesetPin = RESET_PINS[0];
        } else {
          sidesetPin = RESET_PINS[i];
        }
        break;
      case 2:
        // OSC1 syncs from OSC2; OSC3 free-running
        if (i == 0) {
          sidesetPin = RESET_PINS[1];
        } else {
          sidesetPin = RESET_PINS[i];
        }
        break;
      default:
        sidesetPin = RESET_PINS[i];
        break;
    }

    // Freq only on SM0; amplitude uses RANGE PWM (not PIO).
    init_sm_sync(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i], offset[VOICE_TO_PIO[i]], RESET_PINS[i], sidesetPin);

    pio_sm_put(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i], pioPulseLength);

    pio_sm_exec(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i], pio_encode_pull(false, false));

    pio_sm_exec(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i], pio_encode_out(pio_y, 31));
  }
}

// Production SM init via frequency_sync_4_jumps. Called from start_voice_sms().
void init_sm_sync(PIO pio, uint sm, uint offset, uint pin, uint pin2) {
  frequency_sync_4_jumps(pio, sm, offset, pin, pin2);
  pio_sm_set_enabled(pio, sm, true);
}

// Simple test helper to push a clock divider from Hz. Currently unused by the main engine.

#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/utils.ino"

// Linear → logarithmic mapping (0..maxValue). Used by init_ADSR for linToLogLookup.
uint16_t linearToLogarithmic(uint16_t linearValue, float base, uint16_t maxValue) {
  if (linearValue < 0) linearValue = 0;
  if (linearValue > maxValue) linearValue = maxValue;

  float normalizedValue = (float)linearValue / (float)maxValue;
  float logValue = log(normalizedValue * (base - 1) + 1) / log(base);
  float maxLogValue = log(1 + (base - 1)) / log(base);
  uint16_t scaledLogValue = (uint16_t)(logValue * ((float)maxValue / maxLogValue));

  return scaledLogValue;
}

// Exp curve → float (x^2 / curve). Used by params/LFO drift and related control mapping.
float expConverterFloat(uint16_t readingValue, uint16_t curve) {
  uint16_t pow3Calc = readingValue;
  float expValOut = (float)pow3Calc * pow3Calc / curve;
  if (expValOut < 0.005) {
    expValOut = 0;
  }
  return expValOut;
}

// Exp curve → uint16 (x^2 / curve). Used e.g. by apply_param_portamento_time.
uint16_t expConverter(uint16_t readingValue, uint16_t curve) {
  uint16_t pow3Calc = readingValue;
  uint16_t expValOut = (float)pow3Calc * pow3Calc / curve;
  if (expValOut < 0.1) {
    expValOut = 0;
  }
  return expValOut;
}

#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/voices.ino"
#include "include_all.h"

// Enable/disable detailed DCO debug report (including OSC1 frequency stages)
#define DCO_DEBUG_REPORT 0


#ifdef RUNNING_AVERAGE
// RunningAverage object definitions for timing measurements
RunningAverage ra_pitchbend(2000);
RunningAverage ra_osc2_detune(2000);
RunningAverage ra_portamento(2000);
RunningAverage ra_adsr_modifier(2000);
RunningAverage ra_unison_modifier(2000);
RunningAverage ra_drift_multiplier(2000);
RunningAverage ra_modifiers_combination(2000);
RunningAverage ra_freq_scaling_x(2000);
RunningAverage ra_freq_scaling_ratio(2000);
RunningAverage ra_freq_scaling_post(2000);
RunningAverage ra_get_chan_level(2000);
RunningAverage ra_pwm_calculations(2000);
RunningAverage ra_voice_task_total(2000);
RunningAverage ra_clk_div_calc(2000);

unsigned long last_timing_print = 0;
unsigned long voice_task_max_time = 0;
const unsigned long TIMING_PRINT_INTERVAL = 1000;  // Print every 5 seconds
#endif

// Boot init: seed notes, build pitch tables, apply voice mode, run one voice_task_main().
void init_voices() {

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    VOICE_NOTES[i] = DCO_calibration_start_note;
  }

  initMultiplierTables();
  setVoiceMode();
  voice_task_main();
}

// Fast helper: convert a Q16 note (semitones) to Q24 frequency using linear
// interpolation on the sNotePitches_q24 table. Used in slew-rate mode.
static inline int64_t noteQ16_to_freqQ24(int32_t note_q16) {
  const size_t NOTE_TABLE_LEN = sizeof(sNotePitches_q24) / sizeof(sNotePitches_q24[0]);
  if (NOTE_TABLE_LEN == 0) return 0;

  int32_t noteInt = note_q16 >> 16;
  uint32_t frac = (uint32_t)note_q16 & 0xFFFF;

  if (noteInt <= 0) {
    if (NOTE_TABLE_LEN == 1) return sNotePitches_q24[0];
    if (frac == 0) return sNotePitches_q24[0];
    int64_t f0 = sNotePitches_q24[0];
    int64_t f1 = sNotePitches_q24[1];
    int64_t df = f1 - f0;
    return f0 + ((df * (int64_t)frac) >> 16);
  }
  if ((size_t)noteInt >= NOTE_TABLE_LEN - 1) {
    // Clamp to top of table
    return sNotePitches_q24[NOTE_TABLE_LEN - 1];
  }

  if (frac == 0) {
    // Exact semitone, just return table entry (common case).
    return sNotePitches_q24[noteInt];
  }

  int64_t f0 = sNotePitches_q24[noteInt];
  int64_t f1 = sNotePitches_q24[noteInt + 1];
  int64_t df = f1 - f0;
  return f0 + ((df * (int64_t)frac) >> 16);
}

// Helper: convert float Hz to Q24 fixed-point (Hz * 2^24)
static inline int64_t float_to_q24(float f) {
  return (int64_t)lrintf(f * (float)(1 << 24));
}

#ifdef USE_FLOAT_VOICE_TASK
// Helper: convert a semitone index (float) to Hz using sNotePitches[] with linear interpolation.
static inline float noteIndex_to_freqFloat(float noteIndex) {
  const size_t LEN = sizeof(sNotePitches) / sizeof(sNotePitches[0]);
  if (LEN == 0) return 0.0f;
  if (noteIndex <= 0.0f) return sNotePitches[0];
  if (noteIndex >= (float)(LEN - 1)) return sNotePitches[LEN - 1];

  int n0 = (int)floorf(noteIndex);
  int n1 = n0 + 1;
  float t = noteIndex - (float)n0;
  float f0 = sNotePitches[n0];
  float f1 = sNotePitches[n1];
  return f0 + (f1 - f0) * t;
}
#endif

#ifndef USE_FLOAT_VOICE_TASK
// Fixed-point realtime voice engine (portamento, modifiers, clkdiv, amp, PIO/PWM/PW).
// Selected by voice_task_main() when USE_FLOAT_VOICE_TASK is not defined.
inline void voice_task() {
#ifdef RUNNING_AVERAGE
  unsigned long voice_task_start_time = micros();
#endif

  // Track portamento-time and mode changes between calls so we can smoothly
  // retime the glide without introducing pitch discontinuities.
  static uint32_t last_portamento_time = 0;
  static uint8_t last_portamento_mode = PORTA_MODE_TIME;
  uint32_t portaTime = portamento_time;
  uint8_t portaMode = portamento_mode;
  bool portaTimeChanged = (portaTime != last_portamento_time);
  bool portaModeChanged = (portaMode != last_portamento_mode);

  // Pre-calculate pitch bend as a Q24 value. This is done once per voice_task call.
  int32_t calcPitchbend_q24;

#ifdef RUNNING_AVERAGE
  unsigned long t_start = micros();
#endif
  // Optimized: Perform pitch bend calculation entirely in fixed-point Q24.
  // ((bend / 8192.0) - 1.0) * pitchBendMultiplier
  // This avoids float conversions and multiplications in the hot path.
  int32_t bend_normalized_q24 = ((int32_t)midi_pitch_bend << 11) - (1 << 24);
  calcPitchbend_q24 = (int32_t)(((int64_t)bend_normalized_q24 * pitchBendMultiplier_q24) >> 24);
#ifdef RUNNING_AVERAGE
  ra_pitchbend.addValue((float)(micros() - t_start));
#endif

  last_midi_pitch_bend = midi_pitch_bend;

  // Hoist PWM parameters out of the loop. This is critical for performance,
  // as it reads the volatile LFO2toPW variable only once per task run.
  const int16_t local_ADSR1toPWM = ADSR1toPWM;
  const int16_t local_LFO2toPW = LFO2toPW;

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {

#if DCO_DEBUG_REPORT
    // Debug: track OSC1 frequency at key stages of the pipeline for DCO report.
    float dbg_freq_base_Hz = 0.0f;       // After portamento, before modifiers
    float dbg_freq_after_mod_Hz = 0.0f;  // After all modifiers applied (freq_q24_A)
#endif

    if (note_on_flag[i] == 1) {
      note_on_flag_flag[i] = true;
      note_on_flag[i] = 0;
    }

    if (VOICE_NOTES[i] >= 0) {
      uint8_t note1 = VOICE_NOTES[i] - 36 + OSC1_interval;
      if (note1 > highestNote) {
        note1 -= ((uint8_t(note1 - highestNote) / 12) * 12);
      }
      uint8_t note2 = note1 - 36 + OSC2_interval;
      if (note2 > highestNote) {
        note2 -= ((uint8_t(note2 - highestNote) / 12) * 12);
      }
      uint8_t note3 = note1 - 36 + OSC3_interval;
      if (note3 > highestNote) {
        note3 -= ((uint8_t(note3 - highestNote) / 12) * 12);
      }
      // Clamp note indexes to table (defensive)
      const size_t NOTE_TABLE_LEN = sizeof(sNotePitches_q24) / sizeof(sNotePitches_q24[0]);
      if (note1 >= NOTE_TABLE_LEN) note1 = (uint8_t)(NOTE_TABLE_LEN - 1);
      if (note2 >= NOTE_TABLE_LEN) note2 = (uint8_t)(NOTE_TABLE_LEN - 1);
      if (note3 >= NOTE_TABLE_LEN) note3 = (uint8_t)(NOTE_TABLE_LEN - 1);

#ifdef RUNNING_AVERAGE
      unsigned long t_osc2 = micros();
#endif
      // Optimized: Calculate OSC2/OSC3 detune in Q24 and keep it there.
      // The float conversion has been removed as it is no longer needed.
      // detune = 1.0 + 0.0002 * (256 - val)
      static constexpr int32_t DETUNE_SCALE_Q24 = (int32_t)(0.0002f * (float)(1 << 24) + 0.5f);
      int32_t detune_steps = ((int)256 - OSC2DetuneVal);
      int32_t detune_q24 = (1 << 24) + (detune_steps * DETUNE_SCALE_Q24);
      int32_t detune3_steps = ((int)256 - OSC3DetuneVal);
      int32_t detune3_q24 = (1 << 24) + (detune3_steps * DETUNE_SCALE_Q24);
#ifdef RUNNING_AVERAGE
      ra_osc2_detune.addValue((float)(micros() - t_osc2));
#endif

      int64_t freq_q24_A;
      int64_t freq_q24_B;
      int64_t freq_q24_C;

      // Fixed osc indices for current mono hardware (3 oscs on voice 0).
      // Future paraphonic mode can remap osc ownership per voice without gutting allocation.
      const uint8_t DCO_A = 0;
      const uint8_t DCO_B = 1;
      const uint8_t DCO_C = 2;

      // Serial.println("VOICE TASK 2");
      ////***********************    PORTAMENTO CODE   ****************************************/////
#ifdef RUNNING_AVERAGE
      unsigned long t_portamento = micros();
#endif
      if (portaTime > 0 /*&& portamento_start != 0 && portamento_stop != 0*/) {
        uint32_t now_us = micros();
        portamentoTimer[i] = now_us - portamentoStartMicros[i];

        if (note_on_flag_flag[i]) {
          // Serial.println("NOTE ON");
          portamentoStartMicros[i] = now_us;

          portamentoTimer[i] = 0;

          // Derive endpoints for portamento
          int64_t stopA_q24 = sNotePitches_q24[note1];
          int64_t stopB_q24 = sNotePitches_q24[note2];
          int64_t stopC_q24 = sNotePitches_q24[note3];
          portamento_stop_q24[DCO_A] = stopA_q24;
          portamento_stop_q24[DCO_B] = stopB_q24;
          portamento_stop_q24[DCO_C] = stopC_q24;

          int32_t T = (portaTime == 0) ? 1 : (int32_t)portaTime;

          if (portaMode == PORTA_MODE_TIME) {
            // Time-based mode: glide linearly in frequency.
            int64_t startA_q24 = portamento_cur_freq_q24[DCO_A];
            int64_t startB_q24 = portamento_cur_freq_q24[DCO_B];
            int64_t startC_q24 = portamento_cur_freq_q24[DCO_C];
            portamento_start_q24[DCO_A] = startA_q24;
            portamento_start_q24[DCO_B] = startB_q24;
            portamento_start_q24[DCO_C] = startC_q24;
            portamento_cur_freq_q24[DCO_A] = startA_q24;
            portamento_cur_freq_q24[DCO_B] = startB_q24;
            portamento_cur_freq_q24[DCO_C] = startC_q24;

            int64_t dA = stopA_q24 - startA_q24;
            int64_t dB = stopB_q24 - startB_q24;
            int64_t dC = stopC_q24 - startC_q24;

            // Fixed-time glide: span is covered in approximately portaTime microseconds.
            int64_t halfT = (int64_t)T >> 1;
            int64_t numA = (dA >= 0) ? (dA + halfT) : (dA - halfT);
            int64_t numB = (dB >= 0) ? (dB + halfT) : (dB - halfT);
            int64_t numC = (dC >= 0) ? (dC + halfT) : (dC - halfT);
            freqPortaStep_q24[DCO_A] = (numA / (int64_t)T);
            freqPortaStep_q24[DCO_B] = (numB / (int64_t)T);
            freqPortaStep_q24[DCO_C] = (numC / (int64_t)T);
          } else {
            // Slew-rate (musical) mode: glide linearly in note-space (semitones).
            // Use current note position as start; if uninitialized, fall back to target note.
            int32_t startNoteA_q16 = porta_note_cur_q16[DCO_A];
            int32_t startNoteB_q16 = porta_note_cur_q16[DCO_B];
            int32_t startNoteC_q16 = porta_note_cur_q16[DCO_C];
            int32_t targetNoteA_q16 = ((int32_t)note1) << 16;
            int32_t targetNoteB_q16 = ((int32_t)note2) << 16;
            int32_t targetNoteC_q16 = ((int32_t)note3) << 16;

            if (startNoteA_q16 == 0) startNoteA_q16 = targetNoteA_q16;
            if (startNoteB_q16 == 0) startNoteB_q16 = targetNoteB_q16;
            if (startNoteC_q16 == 0) startNoteC_q16 = targetNoteC_q16;

            porta_note_start_q16[DCO_A] = startNoteA_q16;
            porta_note_start_q16[DCO_B] = startNoteB_q16;
            porta_note_start_q16[DCO_C] = startNoteC_q16;
            porta_note_stop_q16[DCO_A] = targetNoteA_q16;
            porta_note_stop_q16[DCO_B] = targetNoteB_q16;
            porta_note_stop_q16[DCO_C] = targetNoteC_q16;

            int32_t dNoteA_q16 = porta_note_stop_q16[DCO_A] - porta_note_start_q16[DCO_A];
            int32_t dNoteB_q16 = porta_note_stop_q16[DCO_B] - porta_note_start_q16[DCO_B];
            int32_t dNoteC_q16 = porta_note_stop_q16[DCO_C] - porta_note_start_q16[DCO_C];

            // Per-microsecond step in Q16 notes.
            // Use symmetric rounding for step magnitude.
            int64_t halfT = (int64_t)T >> 1;
            int64_t numA = (dNoteA_q16 >= 0) ? ((int64_t)dNoteA_q16 + halfT) : ((int64_t)dNoteA_q16 - halfT);
            int64_t numB = (dNoteB_q16 >= 0) ? ((int64_t)dNoteB_q16 + halfT) : ((int64_t)dNoteB_q16 - halfT);
            int64_t numC = (dNoteC_q16 >= 0) ? ((int64_t)dNoteC_q16 + halfT) : ((int64_t)dNoteC_q16 - halfT);
            porta_note_step_q16[DCO_A] = (int32_t)(numA / (int64_t)T);
            porta_note_step_q16[DCO_B] = (int32_t)(numB / (int64_t)T);
            porta_note_step_q16[DCO_C] = (int32_t)(numC / (int64_t)T);

            // Ensure we always move for non-zero intervals; otherwise tiny intervals
            // with long times could quantize to zero step and "stick".
            if (dNoteA_q16 != 0 && porta_note_step_q16[DCO_A] == 0) {
              porta_note_step_q16[DCO_A] = (dNoteA_q16 > 0) ? 1 : -1;
            }
            if (dNoteB_q16 != 0 && porta_note_step_q16[DCO_B] == 0) {
              porta_note_step_q16[DCO_B] = (dNoteB_q16 > 0) ? 1 : -1;
            }
            if (dNoteC_q16 != 0 && porta_note_step_q16[DCO_C] == 0) {
              porta_note_step_q16[DCO_C] = (dNoteC_q16 > 0) ? 1 : -1;
            }

            // Initialize current note and frequency at start of glide
            porta_note_cur_q16[DCO_A] = startNoteA_q16;
            porta_note_cur_q16[DCO_B] = startNoteB_q16;
            porta_note_cur_q16[DCO_C] = startNoteC_q16;
            portamento_cur_freq_q24[DCO_A] = noteQ16_to_freqQ24(startNoteA_q16);
            portamento_cur_freq_q24[DCO_B] = noteQ16_to_freqQ24(startNoteB_q16);
            portamento_cur_freq_q24[DCO_C] = noteQ16_to_freqQ24(startNoteC_q16);
          }
        }

        // Compute current glide position using existing timing/slope
        int32_t elapsed_us = (int32_t)portamentoTimer[i];
        int64_t curA;
        int64_t curB;
        int64_t curC;

        if (portaMode == PORTA_MODE_TIME) {
          if ((uint32_t)elapsed_us > portaTime) {
            // Snap to target once we have exceeded the (current) portamento time
            curA = portamento_stop_q24[DCO_A];
            curB = portamento_stop_q24[DCO_B];
            curC = portamento_stop_q24[DCO_C];
          } else {
            // Absolute-time base in Q24
            curA = portamento_start_q24[DCO_A] + freqPortaStep_q24[DCO_A] * (int64_t)elapsed_us;
            curB = portamento_start_q24[DCO_B] + freqPortaStep_q24[DCO_B] * (int64_t)elapsed_us;
            curC = portamento_start_q24[DCO_C] + freqPortaStep_q24[DCO_C] * (int64_t)elapsed_us;
          }
        } else {
          // Slew-rate (musical) mode: step is constant in note-space; stop when we reach the target.
          int32_t dNoteA_q16 = porta_note_stop_q16[DCO_A] - porta_note_start_q16[DCO_A];
          int32_t dNoteB_q16 = porta_note_stop_q16[DCO_B] - porta_note_start_q16[DCO_B];
          int32_t dNoteC_q16 = porta_note_stop_q16[DCO_C] - porta_note_start_q16[DCO_C];

          int64_t curNoteA_q16 = (int64_t)porta_note_start_q16[DCO_A] + (int64_t)porta_note_step_q16[DCO_A] * (int64_t)elapsed_us;
          int64_t curNoteB_q16 = (int64_t)porta_note_start_q16[DCO_B] + (int64_t)porta_note_step_q16[DCO_B] * (int64_t)elapsed_us;
          int64_t curNoteC_q16 = (int64_t)porta_note_start_q16[DCO_C] + (int64_t)porta_note_step_q16[DCO_C] * (int64_t)elapsed_us;

          // Clamp when passing the target
          if ((dNoteA_q16 >= 0 && curNoteA_q16 >= (int64_t)porta_note_stop_q16[DCO_A]) ||
              (dNoteA_q16 < 0 && curNoteA_q16 <= (int64_t)porta_note_stop_q16[DCO_A])) {
            curNoteA_q16 = porta_note_stop_q16[DCO_A];
          }
          if ((dNoteB_q16 >= 0 && curNoteB_q16 >= (int64_t)porta_note_stop_q16[DCO_B]) ||
              (dNoteB_q16 < 0 && curNoteB_q16 <= (int64_t)porta_note_stop_q16[DCO_B])) {
            curNoteB_q16 = porta_note_stop_q16[DCO_B];
          }
          if ((dNoteC_q16 >= 0 && curNoteC_q16 >= (int64_t)porta_note_stop_q16[DCO_C]) ||
              (dNoteC_q16 < 0 && curNoteC_q16 <= (int64_t)porta_note_stop_q16[DCO_C])) {
            curNoteC_q16 = porta_note_stop_q16[DCO_C];
          }

          porta_note_cur_q16[DCO_A] = (int32_t)curNoteA_q16;
          porta_note_cur_q16[DCO_B] = (int32_t)curNoteB_q16;
          porta_note_cur_q16[DCO_C] = (int32_t)curNoteC_q16;

          curA = noteQ16_to_freqQ24(porta_note_cur_q16[DCO_A]);
          curB = noteQ16_to_freqQ24(porta_note_cur_q16[DCO_B]);
          curC = noteQ16_to_freqQ24(porta_note_cur_q16[DCO_C]);
        }

        portamento_cur_freq_q24[DCO_A] = curA;
        portamento_cur_freq_q24[DCO_B] = curB;
        portamento_cur_freq_q24[DCO_C] = curC;

        // If the portamento time or mode control changed while gliding, retime the glide
        // from the *current* position so there is no pitch jump, only a change
        // in glide speed / curve.
        if (portaTimeChanged || portaModeChanged) {
          int32_t T = (portaTime == 0) ? 1 : (int32_t)portaTime;

          portamentoStartMicros[i] = now_us;
          portamentoTimer[i] = 0;

          if (portaMode == PORTA_MODE_TIME) {
            // Recompute time-based glide from current frequency.
            int64_t targetA = sNotePitches_q24[note1];
            int64_t targetB = sNotePitches_q24[note2];
            int64_t targetC = sNotePitches_q24[note3];

            portamento_start_q24[DCO_A] = curA;
            portamento_start_q24[DCO_B] = curB;
            portamento_start_q24[DCO_C] = curC;
            portamento_stop_q24[DCO_A] = targetA;
            portamento_stop_q24[DCO_B] = targetB;
            portamento_stop_q24[DCO_C] = targetC;

            int64_t dA = targetA - curA;
            int64_t dB = targetB - curB;
            int64_t dC = targetC - curC;
            int64_t halfT = (int64_t)T >> 1;
            int64_t numA = (dA >= 0) ? (dA + halfT) : (dA - halfT);
            int64_t numB = (dB >= 0) ? (dB + halfT) : (dB - halfT);
            int64_t numC = (dC >= 0) ? (dC + halfT) : (dC - halfT);
            freqPortaStep_q24[DCO_A] = (numA / (int64_t)T);
            freqPortaStep_q24[DCO_B] = (numB / (int64_t)T);
            freqPortaStep_q24[DCO_C] = (numC / (int64_t)T);
          } else {
            // Recompute slew-rate glide from current note position.
            int32_t currentNoteA_q16 = porta_note_cur_q16[DCO_A];
            int32_t currentNoteB_q16 = porta_note_cur_q16[DCO_B];
            int32_t currentNoteC_q16 = porta_note_cur_q16[DCO_C];
            int32_t targetNoteA_q16 = ((int32_t)note1) << 16;
            int32_t targetNoteB_q16 = ((int32_t)note2) << 16;
            int32_t targetNoteC_q16 = ((int32_t)note3) << 16;

            porta_note_start_q16[DCO_A] = currentNoteA_q16;
            porta_note_start_q16[DCO_B] = currentNoteB_q16;
            porta_note_start_q16[DCO_C] = currentNoteC_q16;
            porta_note_stop_q16[DCO_A] = targetNoteA_q16;
            porta_note_stop_q16[DCO_B] = targetNoteB_q16;
            porta_note_stop_q16[DCO_C] = targetNoteC_q16;

            int32_t dNoteA_q16 = porta_note_stop_q16[DCO_A] - porta_note_start_q16[DCO_A];
            int32_t dNoteB_q16 = porta_note_stop_q16[DCO_B] - porta_note_start_q16[DCO_B];
            int32_t dNoteC_q16 = porta_note_stop_q16[DCO_C] - porta_note_start_q16[DCO_C];

            int64_t halfT = (int64_t)T >> 1;
            int64_t numA = (dNoteA_q16 >= 0) ? ((int64_t)dNoteA_q16 + halfT) : ((int64_t)dNoteA_q16 - halfT);
            int64_t numB = (dNoteB_q16 >= 0) ? ((int64_t)dNoteB_q16 + halfT) : ((int64_t)dNoteB_q16 - halfT);
            int64_t numC = (dNoteC_q16 >= 0) ? ((int64_t)dNoteC_q16 + halfT) : ((int64_t)dNoteC_q16 - halfT);
            porta_note_step_q16[DCO_A] = (int32_t)(numA / (int64_t)T);
            porta_note_step_q16[DCO_B] = (int32_t)(numB / (int64_t)T);
            porta_note_step_q16[DCO_C] = (int32_t)(numC / (int64_t)T);

            if (dNoteA_q16 != 0 && porta_note_step_q16[DCO_A] == 0) {
              porta_note_step_q16[DCO_A] = (dNoteA_q16 > 0) ? 1 : -1;
            }
            if (dNoteB_q16 != 0 && porta_note_step_q16[DCO_B] == 0) {
              porta_note_step_q16[DCO_B] = (dNoteB_q16 > 0) ? 1 : -1;
            }
            if (dNoteC_q16 != 0 && porta_note_step_q16[DCO_C] == 0) {
              porta_note_step_q16[DCO_C] = (dNoteC_q16 > 0) ? 1 : -1;
            }
          }
        }
      } else {
        portamento_cur_freq_q24[DCO_A] = sNotePitches_q24[note1];
        portamento_start_q24[DCO_A] = portamento_cur_freq_q24[DCO_A];
        portamento_stop_q24[DCO_A] = portamento_cur_freq_q24[DCO_A];

        portamento_cur_freq_q24[DCO_B] = sNotePitches_q24[note2];
        portamento_start_q24[DCO_B] = portamento_cur_freq_q24[DCO_B];
        portamento_stop_q24[DCO_B] = portamento_cur_freq_q24[DCO_B];

        portamento_cur_freq_q24[DCO_C] = sNotePitches_q24[note3];
        portamento_start_q24[DCO_C] = portamento_cur_freq_q24[DCO_C];
        portamento_stop_q24[DCO_C] = portamento_cur_freq_q24[DCO_C];
      }

#if DCO_DEBUG_REPORT
      // Debug: OSC1 base frequency after portamento, before modifiers (in Hz)
      dbg_freq_base_Hz = (float)portamento_cur_freq_q24[DCO_A] / (float)(1 << 24);
#endif
#ifdef RUNNING_AVERAGE
      ra_portamento.addValue((float)(micros() - t_portamento));
#endif
      ////***********************    PORTAMENTO CODE  END    ****************************************/////

#ifdef RUNNING_AVERAGE
      unsigned long t_adsr = micros();
#endif
      // Fixed-point ADSR modifier in Q24: ((linToLog * ADSR1toDETUNE1) / 1080000)
      int64_t ADSRModifier_q24 = 0;
      if (ADSR1toDETUNE1 != 0) {
        // Use precomputed Q24 scale: ADSR1toDETUNE1_scale_q24 = round(ADSR1toDETUNE1 * 2^24 / 1080000)
        ADSRModifier_q24 = (int64_t)linToLogLookup[ADSR1Level[i]] * (int32_t)ADSR1toDETUNE1_scale_q24;
      }
      // ADSR3→pitch select:
      //   0 = OSC1, 1 = OSC2, 2 = OSC1+OSC2 (legacy), 3 = OSC3, 4 = all three
      int64_t ADSRModifierOSC1_q24 = (ADSR3ToOscSelect == 0 || ADSR3ToOscSelect == 2 || ADSR3ToOscSelect == 4) ? ADSRModifier_q24 : 0;
      int64_t ADSRModifierOSC2_q24 = (ADSR3ToOscSelect == 1 || ADSR3ToOscSelect == 2 || ADSR3ToOscSelect == 4) ? ADSRModifier_q24 : 0;
      int64_t ADSRModifierOSC3_q24 = (ADSR3ToOscSelect == 3 || ADSR3ToOscSelect == 4) ? ADSRModifier_q24 : 0;
#ifdef RUNNING_AVERAGE
      ra_adsr_modifier.addValue((float)(micros() - t_adsr));
      unsigned long t_unison = micros();
#endif

      // Fixed-point unison modifier in Q24: 0.0001 * unisonDetune * step
      static constexpr int32_t UNISON_SCALE_Q24 = (int32_t)(0.0001f * (float)(1 << 24) + 0.5f);
      // Per-osc spread (matches float path): OSC1=0, OSC2=+1, OSC3=-1.
      static constexpr int32_t OSC_UNISON_STEP[3] = { 0, 1, -1 };
      int64_t unisonMODIFIER_OSC1_q24 = (int64_t)unisonDetune * (int64_t)UNISON_SCALE_Q24 * (int64_t)OSC_UNISON_STEP[0];
      int64_t unisonMODIFIER_OSC2_q24 = (int64_t)unisonDetune * (int64_t)UNISON_SCALE_Q24 * (int64_t)OSC_UNISON_STEP[1];
      int64_t unisonMODIFIER_OSC3_q24 = (int64_t)unisonDetune * (int64_t)UNISON_SCALE_Q24 * (int64_t)OSC_UNISON_STEP[2];
#if NUM_VOICES_TOTAL > 1
      // Classic poly voice-index alternating pattern on top of per-osc spread.
      int32_t voiceMag = (i >> 1) + 1;
      int32_t voiceSign = ((i & 0x01) == 0) ? 1 : -1;
      int64_t voiceUnison_q24 = (int64_t)unisonDetune * (int64_t)UNISON_SCALE_Q24 * (int64_t)(voiceSign * voiceMag);
      unisonMODIFIER_OSC1_q24 += voiceUnison_q24;
      unisonMODIFIER_OSC2_q24 += voiceUnison_q24;
      unisonMODIFIER_OSC3_q24 += voiceUnison_q24;
#endif
#ifdef RUNNING_AVERAGE
      ra_unison_modifier.addValue((float)(micros() - t_unison));
      unsigned long t_drift = micros();
#endif

      // Fixed-point drift modifiers in Q24: LFO_LEVEL * (0.0000005 * analogDrift)
      static constexpr int32_t DRIFT_UNIT_Q24 = (int32_t)(0.0000005f * (float)(1 << 24) + 0.5f);
      int32_t driftScale_q24 = (int32_t)((int32_t)analogDrift * DRIFT_UNIT_Q24);
      int64_t DETUNE_DRIFT_OSC1_q24 = (analogDrift != 0) ? ((int64_t)LFO_DRIFT_LEVEL[DCO_A] * (int64_t)driftScale_q24) : 0;
      int64_t DETUNE_DRIFT_OSC2_q24 = (analogDrift != 0) ? ((int64_t)LFO_DRIFT_LEVEL[DCO_B] * (int64_t)driftScale_q24) : 0;
      int64_t DETUNE_DRIFT_OSC3_q24 = (analogDrift != 0) ? ((int64_t)LFO_DRIFT_LEVEL[DCO_C] * (int64_t)driftScale_q24) : 0;
#ifdef RUNNING_AVERAGE
      ra_drift_multiplier.addValue((float)(micros() - t_drift));
#endif

#ifdef RUNNING_AVERAGE
      unsigned long t_modifiers = micros();
#endif
      // Combine modifiers in Q24 (faithful to original float path)
      int32_t detune_fifo_q24 = DETUNE_INTERNAL_FIFO_q24;

      // 1.00001f in Q24 (epsilon ≈ 168 LSBs)
      // Fixed-point equivalent of:
      //   modifiersAll = DETUNE_INTERNAL_FIFO_float + unisonMODIFIER + calcPitchbend + 1.00001f;
      // Unison is applied per-osc below; shared part is LFO1 FIFO + pitchbend + epsilon.
      int64_t modifiersBase_q24 =
        (int64_t)detune_fifo_q24 + (int64_t)calcPitchbend_q24 + (int64_t)Q24_ONE_EPS;
      int64_t freqModifiers_q24 = ADSRModifierOSC1_q24 + DETUNE_DRIFT_OSC1_q24 + modifiersBase_q24 + unisonMODIFIER_OSC1_q24;
      int64_t freq2Modifiers_q24 = ADSRModifierOSC2_q24 + DETUNE_DRIFT_OSC2_q24 + modifiersBase_q24 + unisonMODIFIER_OSC2_q24 + (int64_t)DETUNE_INTERNAL2_q24;
      int64_t freq3Modifiers_q24 = ADSRModifierOSC3_q24 + DETUNE_DRIFT_OSC3_q24 + modifiersBase_q24 + unisonMODIFIER_OSC3_q24 + (int64_t)DETUNE_INTERNAL3_q24;
#ifdef RUNNING_AVERAGE
      ra_modifiers_combination.addValue((float)(micros() - t_modifiers));
      unsigned long t_freq_scaling_x = micros();
#endif



      // Fast fixed-point equivalent of:
      //   freq  *= interpolatePitchMultiplier(freqModifiers)/multiplierTableScale;
      //   freq2 *= OSC2_detune * interpolatePitchMultiplier(freq2Modifiers)/multiplierTableScale;
      //   freq3 *= OSC3_detune * interpolatePitchMultiplier(freq3Modifiers)/multiplierTableScale;
      // High-resolution fixed-point x with truncation toward zero (matches original float cast):
      // xQ16 = trunc((q24 * scale) / 2^8) to carry 16 fractional bits of table-units
      int64_t x1_q24s = (freqModifiers_q24 * (int64_t)multiplierTableScale);   // Q24 * int -> Q24
      int64_t x2_q24s = (freq2Modifiers_q24 * (int64_t)multiplierTableScale);  // Q24 * int -> Q24
      int64_t x3_q24s = (freq3Modifiers_q24 * (int64_t)multiplierTableScale);
      int32_t xScaled1_Q16 = (x1_q24s >= 0) ? (int32_t)(x1_q24s >> 8) : (int32_t)(-((-x1_q24s) >> 8));
      int32_t xScaled2_Q16 = (x2_q24s >= 0) ? (int32_t)(x2_q24s >> 8) : (int32_t)(-((-x2_q24s) >> 8));
      int32_t xScaled3_Q16 = (x3_q24s >= 0) ? (int32_t)(x3_q24s >> 8) : (int32_t)(-((-x3_q24s) >> 8));

#ifdef RUNNING_AVERAGE
      ra_freq_scaling_x.addValue((float)(micros() - t_freq_scaling_x));
      unsigned long t_freq_scaling_ratio = micros();
#endif

#if PITCH_USE_RATIO_Q16
      int32_t ratio1_Q16 = interpolateRatioQ16_cached(xScaled1_Q16, DCO_A);
      int32_t ratio2_Q16 = interpolateRatioQ16_cached(xScaled2_Q16, DCO_B);
      int32_t ratio3_Q16 = interpolateRatioQ16_cached(xScaled3_Q16, DCO_C);
#ifdef RUNNING_AVERAGE
      ra_freq_scaling_ratio.addValue((float)(micros() - t_freq_scaling_ratio));
      unsigned long t_freq_scaling_post = micros();
#endif

      freq_q24_A = (portamento_cur_freq_q24[DCO_A] * (int64_t)ratio1_Q16) >> 16;
      // Combine OSC2 ratio with detune into one Q16 factor
      // detune_Q16 = round(detune_q24 / 2^8)
      int32_t detune_Q16 = (int32_t)((((int64_t)detune_q24) + 128) >> 8);
      // combined_Q16 = round((ratio2_Q16 * detune_Q16) / 2^16)
      int32_t combined_Q16 = (int32_t)((((int64_t)ratio2_Q16 * (int64_t)detune_Q16) + (1LL << 15)) >> 16);
      freq_q24_B = (portamento_cur_freq_q24[DCO_B] * (int64_t)combined_Q16) >> 16;
      // Combine OSC3 ratio with detune into one Q16 factor
      int32_t detune3_Q16 = (int32_t)((((int64_t)detune3_q24) + 128) >> 8);
      int32_t combined3_Q16 = (int32_t)((((int64_t)ratio3_Q16 * (int64_t)detune3_Q16) + (1LL << 15)) >> 16);
      freq_q24_C = (portamento_cur_freq_q24[DCO_C] * (int64_t)combined3_Q16) >> 16;
#else
#ifdef RUNNING_AVERAGE
      ra_freq_scaling_ratio.addValue((float)(micros() - t_freq_scaling_ratio));
      unsigned long t_freq_scaling_post = micros();
#endif

      int32_t yTab1 = interpolatePitchMultiplierIntQ16_cached(xScaled1_Q16, DCO_A);
      int32_t yTab2 = interpolatePitchMultiplierIntQ16_cached(xScaled2_Q16, DCO_B);
      int32_t yTab3 = interpolatePitchMultiplierIntQ16_cached(xScaled3_Q16, DCO_C);
      // Convert yTab -> ratioQ16 using reciprocal-multiply (round((yTab<<16)/10000))
      uint64_t numA = ((uint64_t)(uint32_t)yTab1 << 16) + 5000u;
      int32_t ratio1_Q16_fallback = (int32_t)((numA * 0xD1B71759ULL) >> 45);
      uint64_t numB = ((uint64_t)(uint32_t)yTab2 << 16) + 5000u;
      int32_t ratio2_Q16_fallback = (int32_t)((numB * 0xD1B71759ULL) >> 45);
      uint64_t numC = ((uint64_t)(uint32_t)yTab3 << 16) + 5000u;
      int32_t ratio3_Q16_fallback = (int32_t)((numC * 0xD1B71759ULL) >> 45);
      // Scale A with ratioQ16
      freq_q24_A = (portamento_cur_freq_q24[DCO_A] * (int64_t)ratio1_Q16_fallback) >> 16;
      // Combine OSC2 ratio with detune into one Q16 factor
      int32_t detune_Q16_fb = (int32_t)((((int64_t)detune_q24) + 128) >> 8);
      int32_t combined_Q16_fb = (int32_t)((((int64_t)ratio2_Q16_fallback * (int64_t)detune_Q16_fb) + (1LL << 15)) >> 16);
      freq_q24_B = (portamento_cur_freq_q24[DCO_B] * (int64_t)combined_Q16_fb) >> 16;
      // Combine OSC3 ratio with detune into one Q16 factor
      int32_t detune3_Q16_fb = (int32_t)((((int64_t)detune3_q24) + 128) >> 8);
      int32_t combined3_Q16_fb = (int32_t)((((int64_t)ratio3_Q16_fallback * (int64_t)detune3_Q16_fb) + (1LL << 15)) >> 16);
      freq_q24_C = (portamento_cur_freq_q24[DCO_C] * (int64_t)combined3_Q16_fb) >> 16;
#endif

#if DCO_DEBUG_REPORT
      // Debug: OSC1 frequency after all modifiers applied (in Hz)
      dbg_freq_after_mod_Hz = (float)freq_q24_A / (float)(1 << 24);
#endif


      // Per-cycle: no caching; compute dividers directly from current Q18 frequency

#ifdef RUNNING_AVERAGE
      ra_freq_scaling_post.addValue((float)(micros() - t_freq_scaling_post));
#endif
      // Convert from Q24 fixed-point to a compact Q4 (Hz * 2^4) representation.
      // freq_q24_X is Hz * 2^24, so shifting right by 20 yields Hz * 2^4.
      uint32_t freqA_Q4 = (uint32_t)((freq_q24_A + (1LL << 19)) >> 20);  // round to nearest
      uint32_t freqB_Q4 = (uint32_t)((freq_q24_B + (1LL << 19)) >> 20);  // round to nearest
      uint32_t freqC_Q4 = (uint32_t)((freq_q24_C + (1LL << 19)) >> 20);
      if (freqA_Q4 == 0) freqA_Q4 = 1;
      if (freqB_Q4 == 0) freqB_Q4 = 1;
      if (freqC_Q4 == 0) freqC_Q4 = 1;

      uint8_t pioNumberA = VOICE_TO_PIO[DCO_A];
      uint8_t pioNumberB = VOICE_TO_PIO[DCO_B];
      uint8_t pioNumberC = VOICE_TO_PIO[DCO_C];
      PIO pioN_A = pio[VOICE_TO_PIO[DCO_A]];
      PIO pioN_B = pio[VOICE_TO_PIO[DCO_B]];
      PIO pioN_C = pio[VOICE_TO_PIO[DCO_C]];
      uint8_t smAN = VOICE_TO_SM[DCO_A];
      uint8_t smBN = VOICE_TO_SM[DCO_B];
      uint8_t smCN = VOICE_TO_SM[DCO_C];

      // voice_task_3_time = micros() - voice_task_start_time;

#ifdef RUNNING_AVERAGE
      unsigned long t_clk_div = micros();
#endif

      register uint32_t clk_div1, clk_div2, clk_div3;

      uint8_t arbitrary_measured_correction_value = 0; // 60 is a measured correction for the PIO
      
      uint32_t phaseDelay = 0;

      uint32_t total_cycles1, total_cycles2, total_cycles3;

#if HIGH_PRECISION_CLKDIV
      // High-precision path: use full Q24 frequency with 64-bit intermediate divide.
      if (freq_q24_A > 0) {
        uint64_t num1 = ((uint64_t)sysClock_Hz << 24) + (uint64_t)(freq_q24_A / 2);
        total_cycles1 = (uint32_t)(num1 / (uint64_t)freq_q24_A);
      } else {
        total_cycles1 = 0;
      }

      if (freq_q24_B > 0) {
        uint64_t num2 = ((uint64_t)sysClock_Hz << 24) + (uint64_t)(freq_q24_B / 2);
        total_cycles2 = (uint32_t)(num2 / (uint64_t)freq_q24_B);
      } else {
        total_cycles2 = 0;
      }

      if (freq_q24_C > 0) {
        uint64_t num3 = ((uint64_t)sysClock_Hz << 24) + (uint64_t)(freq_q24_C / 2);
        total_cycles3 = (uint32_t)(num3 / (uint64_t)freq_q24_C);
      } else {
        total_cycles3 = 0;
      }
#else
      // --- Oscillator 1: Fixed-point Calculation (no float / 64-bit divide) ---
      // freqA_Q4 represents Hz * 2^4, so multiply sysClock_Hz by 2^4 and divide.
      total_cycles1 = (sysClock_Hz * 16u + (freqA_Q4 / 2u)) / freqA_Q4;  // rounded

      // --- Oscillator 2: Fixed-point Calculation (no float / 64-bit divide) ---
      total_cycles2 = (sysClock_Hz * 16u + (freqB_Q4 / 2u)) / freqB_Q4;  // rounded

      // --- Oscillator 3: Fixed-point Calculation (no float / 64-bit divide) ---
      total_cycles3 = (sysClock_Hz * 16u + (freqC_Q4 / 2u)) / freqC_Q4;
#endif

      // Use rounded division when computing clk_div to minimise bias.
      uint32_t total_osr_val1 = total_cycles1 - T_HIGH_TOTAL_CYCLES - T_LOW_OVERHEAD_CYCLES + arbitrary_measured_correction_value;  
      clk_div1 = (total_osr_val1 + (NUM_OSR_CHUNKS / 2u)) / NUM_OSR_CHUNKS;

      // 1. Calculate the dynamic phase and high period on EVERY call.
      //    Use a single high-precision multiply/divide to avoid compounding
      //    rounding error from per-degree quantisation.
      // Phase align applies to OSC2 only (OSC1↔OSC2 sync); OSC3 is free-running.
      if (oscSync > 1 && phaseAlignOSC2 != 0) {
        // phaseDelay ~= total_cycles2 * phaseAlignOSC2 / 360
        uint64_t phase_num = (uint64_t)total_cycles2 * (uint64_t)phaseAlignOSC2;
        phaseDelay = (uint32_t)((phase_num + 180u) / 360u);
      } else {
        phaseDelay = 0;
      }
      uint32_t y_val2 = pioPulseLength + phaseDelay;
      uint32_t high_total_cycles2 = y_val2 + T_HIGH_OVERHEAD_CYCLES;

      // 2. Calculate the low period using the CORRECT, potentially phase-delayed high period.
      //    This is the critical fix.
      uint32_t total_osr_val2 = total_cycles2 - high_total_cycles2 - T_LOW_OVERHEAD_CYCLES + arbitrary_measured_correction_value;
      clk_div2 = (total_osr_val2 + (NUM_OSR_CHUNKS / 2u)) / NUM_OSR_CHUNKS;

      uint32_t total_osr_val3 = total_cycles3 - T_HIGH_TOTAL_CYCLES - T_LOW_OVERHEAD_CYCLES + arbitrary_measured_correction_value;
      clk_div3 = (total_osr_val3 + (NUM_OSR_CHUNKS / 2u)) / NUM_OSR_CHUNKS;

#ifdef RUNNING_AVERAGE
      ra_clk_div_calc.addValue((float)(micros() - t_clk_div));
#endif


#ifdef RUNNING_AVERAGE
      unsigned long t_chan_level = micros();
#endif

      uint16_t chanLevel, chanLevel2, chanLevel3;

      // Derive Q16 from Q24 for amp-comp, then to Hz*2^FREQ_FRAC_BITS (get_chan_level does not need higher precision)
      int32_t freq_q16_A = (int32_t)((freq_q24_A + (1LL << 7)) >> 8);
      int32_t freq_q16_B = (int32_t)((freq_q24_B + (1LL << 7)) >> 8);
      int32_t freq_q16_C = (int32_t)((freq_q24_C + (1LL << 7)) >> 8);
      const int Q16_TO_FREQ_SHIFT = (16 - FREQ_FRAC_BITS);
      int32_t freqFx_A = (freq_q16_A >= 0) ? (freq_q16_A >> Q16_TO_FREQ_SHIFT)
                                           : -((-freq_q16_A) >> Q16_TO_FREQ_SHIFT);
      int32_t freqFx_B = (freq_q16_B >= 0) ? (freq_q16_B >> Q16_TO_FREQ_SHIFT)
                                           : -((-freq_q16_B) >> Q16_TO_FREQ_SHIFT);
      int32_t freqFx_C = (freq_q16_C >= 0) ? (freq_q16_C >> Q16_TO_FREQ_SHIFT)
                                           : -((-freq_q16_C) >> Q16_TO_FREQ_SHIFT);
      switch (syncMode) {
        case 0:
          chanLevel = get_chan_level_lookup_fast(freqFx_A, DCO_A);
          chanLevel2 = get_chan_level_lookup_fast(freqFx_B, DCO_B);
          chanLevel3 = get_chan_level_lookup_fast(freqFx_C, DCO_C);
          break;
        case 1:
          chanLevel = get_chan_level_lookup_fast((freqFx_A > freqFx_B ? freqFx_A : freqFx_B), DCO_A);
          chanLevel2 = get_chan_level_lookup_fast(freqFx_B, DCO_B);
          chanLevel3 = get_chan_level_lookup_fast(freqFx_C, DCO_C);
          break;
        case 2:
          chanLevel = get_chan_level_lookup_fast(freqFx_A, DCO_A);
          chanLevel2 = get_chan_level_lookup_fast((freqFx_A > freqFx_B ? freqFx_A : freqFx_B), DCO_B);
          chanLevel3 = get_chan_level_lookup_fast(freqFx_C, DCO_C);
          break;
        default:
          chanLevel = get_chan_level_lookup_fast(freqFx_A, DCO_A);
          chanLevel2 = get_chan_level_lookup_fast(freqFx_B, DCO_B);
          chanLevel3 = get_chan_level_lookup_fast(freqFx_C, DCO_C);
          break;
      }
#ifdef RUNNING_AVERAGE
      ra_get_chan_level.addValue((float)(micros() - t_chan_level));
#endif

      pio_sm_put(pioN_A, smAN, clk_div1);
      pio_sm_put(pioN_B, smBN, clk_div2);
      pio_sm_put(pioN_C, smCN, clk_div3);
      pio_sm_exec(pioN_A, smAN, pio_encode_pull(false, false));
      pio_sm_exec(pioN_B, smBN, pio_encode_pull(false, false));
      pio_sm_exec(pioN_C, smCN, pio_encode_pull(false, false));

      if (note_on_flag_flag[i]) {
        // --- Reverse Calculation to find the expected output frequency ---
        uint32_t actual_total_osr_val = (clk_div1 * NUM_OSR_CHUNKS);  // This is what the PIO actually gets
        uint32_t actual_total_period = T_HIGH_TOTAL_CYCLES + actual_total_osr_val + T_LOW_OVERHEAD_CYCLES;
        float expected_freq = (double)sysClock_Hz / (double)actual_total_period;

#if DCO_DEBUG_REPORT
        // --- Print Diagnostic Report ---
        Serial.println("----------------[ DCO DEBUG REPORT ]----------------");
        Serial.printf("Target Freq In:   %.2f Hz\n", (float)freq_q24_A / (float)(1 << 24));
        Serial.printf("Total Cycles Calc:  %lu (Target for the whole period)\n", total_cycles1);
        Serial.printf("High Period Fixed:  %lu cycles (From constants)\n", T_HIGH_TOTAL_CYCLES);
        Serial.printf("Low Overhead Fixed: %lu cycles (From constants)\n", T_LOW_OVERHEAD_CYCLES);
        Serial.printf("Total OSR Delay:    %lu cycles (Remaining for loops)\n", total_osr_val1);
        Serial.printf("clk_div (Average):  %lu (Value sent to PIO)\n", clk_div1);
        Serial.println("---");
        Serial.printf("Actual Period Gen:  %lu cycles (High + (clk_div*%u) + Low)\n",
                      actual_total_period, (unsigned)NUM_OSR_CHUNKS);
        Serial.printf("==> Expected Freq Out: %.2f Hz\n", expected_freq);
        Serial.println("---");

        Serial.println("OSC1 Frequency Stages:");
        Serial.printf("  Base after portamento:     %.4f Hz\n", dbg_freq_base_Hz);
        Serial.printf("  After modifiers (Q24):     %.4f Hz\n", dbg_freq_after_mod_Hz);
        Serial.printf("  Quantized by PIO (clkdiv): %.4f Hz\n", expected_freq);
        Serial.println("---");

        Serial.println("OSC1 Modifier Breakdown (Q24/Q16):");
        Serial.printf("  ADSRModifierOSC1_q24:      %.6f\n", (double)ADSRModifierOSC1_q24 / (double)(1 << 24));
        Serial.printf("  DETUNE_DRIFT_OSC1_q24:     %.6f\n", (double)DETUNE_DRIFT_OSC1_q24 / (double)(1 << 24));
        Serial.printf("  detune_fifo_q24:           %.6f\n", (double)detune_fifo_q24 / (double)(1 << 24));
        Serial.printf("  unisonMODIFIER_q24:        %.6f\n", (double)unisonMODIFIER_q24 / (double)(1 << 24));
        Serial.printf("  pitchbend_q24:             %.6f\n", (double)calcPitchbend_q24 / (double)(1 << 24));
        Serial.printf("  Q24_ONE_EPS:               %.6f\n", (double)Q24_ONE_EPS / (double)(1 << 24));
        Serial.printf("  modifiersAll_q24:          %.6f\n", (double)modifiersAll_q24 / (double)(1 << 24));
        Serial.printf("  freqModifiers_q24:         %.6f\n", (double)freqModifiers_q24 / (double)(1 << 24));
        Serial.println("---");

        Serial.println("OSC1 Multiplier Table Inputs:");
        Serial.printf("  x1_q24s (table-units*Q24): %.6f\n", (double)x1_q24s / (double)(1 << 24));
        Serial.printf("  xScaled1_Q16:              %ld (int)\n", (long)xScaled1_Q16);
        Serial.printf("  ratio1_Q16:                %.6f\n", (double)ratio1_Q16 / (double)(1 << 16));
        Serial.println("----------------------------------------------------\n");

#endif

        if (oscSync == 1) {
          pio_sm_exec(pioN_A, smAN, pio_encode_jmp(10 + offset[pioNumberA]));  // OSC Sync MODE
          pio_sm_exec(pioN_B, smBN, pio_encode_jmp(10 + offset[pioNumberB]));
        }

        if (oscSync > 1) {
          // OSC1/OSC2 live on different PIO blocks — enable/disable each SM separately.
          pio_sm_set_enabled(pioN_A, smAN, false);
          pio_sm_set_enabled(pioN_B, smBN, false);

          pio_sm_clear_fifos(pioN_B, smBN);
          pio_sm_clear_fifos(pioN_A, smAN);

          pio_sm_put(pioN_B, smBN, y_val2);
          pio_sm_exec(pioN_B, smBN, pio_encode_pull(false, false));
          pio_sm_exec(pioN_B, smBN, pio_encode_out(pio_y, 31));

          pio_sm_put(pioN_A, smAN, clk_div1);
          pio_sm_put(pioN_B, smBN, clk_div2);
          pio_sm_exec(pioN_A, smAN, pio_encode_pull(false, true));
          pio_sm_exec(pioN_B, smBN, pio_encode_pull(false, true));

          pio_sm_exec(pioN_A, smAN, pio_encode_jmp(10 + offset[pioNumberA]));  // OSC Sync MODE
          pio_sm_exec(pioN_B, smBN, pio_encode_jmp(10 + offset[pioNumberB]));

          pio_sm_set_enabled(pioN_A, smAN, true);
          pio_sm_set_enabled(pioN_B, smBN, true);
        }

        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], pwm_gpio_to_channel(RANGE_PINS[DCO_C]), chanLevel3);
      }

      if (timer99microsFlag) {
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], pwm_gpio_to_channel(RANGE_PINS[DCO_C]), chanLevel3);

        if (sqr1Status) {
#ifdef RUNNING_AVERAGE
          unsigned long t_pwm = micros();
#endif
          // Optimized: This version avoids storing large intermediate products.
          // The multiplication and shift are combined into one expression per modulator,
          // allowing the compiler to make better use of registers.
          int32_t adsr1_delta = ((int32_t)ADSR1Level[i] * local_ADSR1toPWM) >> 11;
          int32_t lfo2_delta = ((int32_t)LFO2Level * local_LFO2toPW) >> 9;
          int32_t pw_calc = (int32_t)DIV_COUNTER_PW - 1 - lfo2_delta - PW[0] + adsr1_delta;

          if (pw_calc < 0) pw_calc = 0;
          if (pw_calc > (int32_t)DIV_COUNTER_PW - 1) pw_calc = (int32_t)DIV_COUNTER_PW - 1;
          PW_PWM[i] = (uint16_t)pw_calc;
#ifdef RUNNING_AVERAGE
          ra_pwm_calculations.addValue((float)(micros() - t_pwm));
#endif
          // PW_PWM[i] = (uint16_t)constrain(DIV_COUNTER_PW - 1 - /*((float)ADSR3Level[i] * ADSR3toPWM_formula)*/ - ((float)LFO2Level * LFO2toPWM_formula) - PW /*+ RANDOMNESS1 + RANDOMNESS2*/, 0, DIV_COUNTER_PW-1);
          pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), get_PW_level_interpolated(PW_PWM[i], i));

        } else {
          pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), 0);
        }
      }
    }
    note_on_flag_flag[i] = false;
  }

#ifdef RUNNING_AVERAGE
  unsigned long voice_task_duration = micros() - voice_task_start_time;
  ra_voice_task_total.addValue((float)voice_task_duration);
  if (voice_task_duration > voice_task_max_time) {
    voice_task_max_time = voice_task_duration;
  }
#endif

  // Update cached portamento parameters for next call
  last_portamento_time = portaTime;
  last_portamento_mode = portaMode;
}

#endif  // !USE_FLOAT_VOICE_TASK

// Dispatch entry point: select float vs fixed-point implementation at compile time.
inline void voice_task_main() {
#ifdef USE_FLOAT_VOICE_TASK
  voice_task_float();
#else
  voice_task();
#endif
}

#ifdef USE_FLOAT_VOICE_TASK
// Float realtime voice engine (same stages as voice_task, in Hz). Current default with USE_FLOAT_ENGINE.
inline void voice_task_float() {
  #ifdef RUNNING_AVERAGE
    unsigned long voice_task_start_time = micros();
  #endif
  
    // --- Track portamento control changes exactly as in original ---
    static uint32_t last_portamento_time = 0;
    static uint8_t  last_portamento_mode = PORTA_MODE_SLEW;
    uint32_t portaTime = portamento_time;
    uint8_t  portaMode = portamento_mode;
    bool portaTimeChanged = (portaTime != last_portamento_time);
    bool portaModeChanged = (portaMode != last_portamento_mode);
  
    // --- 1. Pitch bend as float, equivalent to Q24 math ---
  #ifdef RUNNING_AVERAGE
    unsigned long t_start = micros();
  #endif
    // Use original float pitch bend behaviour, but derive multiplier from Q24
    float pitchBendMultiplier = (float)pitchBendMultiplier_q24 / (float)(1 << 24);
    float calcPitchbend;

    if (midi_pitch_bend == 8192) {
      calcPitchbend = 0.0f;
    } else if (midi_pitch_bend < 8192) {
      calcPitchbend = (((float)midi_pitch_bend / 8190.99f) - 1.0f) * pitchBendMultiplier;
    } else {  // midi_pitch_bend > 8192
      calcPitchbend = (((float)midi_pitch_bend / 8192.99f) - 1.0f) * pitchBendMultiplier;
    }
  
  #ifdef RUNNING_AVERAGE
    ra_pitchbend.addValue((float)(micros() - t_start));
  #endif
  
    last_midi_pitch_bend = midi_pitch_bend;
  
    // Cache PWM sources like original
    const int16_t local_ADSR1toPWM = ADSR1toPWM;
    const int16_t local_LFO2toPW   = LFO2toPW;
  
    // --- 2. Per-voice loop (mirror original structure) ---
    for (int i = 0; i < NUM_VOICES_TOTAL; ++i) {
  
  #if DCO_DEBUG_REPORT
      float dbg_freq_base_Hz      = 0.0f;
      float dbg_freq_after_mod_Hz = 0.0f;
  #endif
  
      if (note_on_flag[i] == 1) {
        note_on_flag_flag[i] = true;
        note_on_flag[i]      = 0;
      }
  
      if (VOICE_NOTES[i] < 0) {
        note_on_flag_flag[i] = false;
        continue;
      }
  
      // --- 2.1 Note indices (unchanged logic) ---
      uint8_t note1 = VOICE_NOTES[i] - 36 + OSC1_interval;
      if (note1 > highestNote) {
        note1 -= ((uint8_t(note1 - highestNote) / 12) * 12);
      }
      uint8_t note2 = note1 - 36 + OSC2_interval;
      if (note2 > highestNote) {
        note2 -= ((uint8_t(note2 - highestNote) / 12) * 12);
      }
      uint8_t note3 = note1 - 36 + OSC3_interval;
      if (note3 > highestNote) {
        note3 -= ((uint8_t(note3 - highestNote) / 12) * 12);
      }
  
      const size_t NOTE_TABLE_LEN = sizeof(sNotePitches) / sizeof(sNotePitches[0]);
      if (note1 >= NOTE_TABLE_LEN) note1 = (uint8_t)(NOTE_TABLE_LEN - 1);
      if (note2 >= NOTE_TABLE_LEN) note2 = (uint8_t)(NOTE_TABLE_LEN - 1);
      if (note3 >= NOTE_TABLE_LEN) note3 = (uint8_t)(NOTE_TABLE_LEN - 1);
  
  #ifdef RUNNING_AVERAGE
      unsigned long t_osc2 = micros();
  #endif
      // --- 2.2 OSC2/OSC3 detune (float equivalent of Q24) ---
      float detuneSteps = (float)((int)256 - OSC2DetuneVal);
      float osc2DetuneRatio = 1.0f + 0.0002f * detuneSteps;
      float detune3Steps = (float)((int)256 - OSC3DetuneVal);
      float osc3DetuneRatio = 1.0f + 0.0002f * detune3Steps;
  #ifdef RUNNING_AVERAGE
      ra_osc2_detune.addValue((float)(micros() - t_osc2));
  #endif
  
      // base note frequencies from float table
      float noteFreq1 = sNotePitches[note1];
      float noteFreq2 = sNotePitches[note2];
      float noteFreq3 = sNotePitches[note3];
  
      float freqA, freqB, freqC;      // will hold the portamento-processed base freqs
  
      const uint8_t DCO_A = 0;
      const uint8_t DCO_B = 1;
      const uint8_t DCO_C = 2;
  
      // --- 2.3 Portamento (time mode & slew mode), float-only implementation ---
  #ifdef RUNNING_AVERAGE
      unsigned long t_portamento = micros();
  #endif
  
      if (portaTime > 0) {
        uint32_t now_us = micros();
        portamentoTimer[i] = now_us - portamentoStartMicros[i];
  
        if (note_on_flag_flag[i]) {
          portamentoStartMicros[i] = now_us;
          portamentoTimer[i]       = 0;
  
          float T = (portaTime == 0) ? 1.0f : (float)portaTime;

          if (portaMode == PORTA_MODE_TIME) {
            // TIME-BASED: glide linearly in frequency (Hz) over portaTime microseconds.
            float stopA  = noteFreq1;
            float stopB  = noteFreq2;
            float stopC  = noteFreq3;
            float startA = porta_freq_cur_f[DCO_A];
            float startB = porta_freq_cur_f[DCO_B];
            float startC = porta_freq_cur_f[DCO_C];

            porta_freq_start_f[DCO_A] = startA;
            porta_freq_start_f[DCO_B] = startB;
            porta_freq_start_f[DCO_C] = startC;
            porta_freq_stop_f [DCO_A] = stopA;
            porta_freq_stop_f [DCO_B] = stopB;
            porta_freq_stop_f [DCO_C] = stopC;

            float dA = stopA - startA;
            float dB = stopB - startB;
            float dC = stopC - startC;

            float stepA = dA / T;
            float stepB = dB / T;
            float stepC = dC / T;
            if (dA != 0.0f && stepA == 0.0f) stepA = (dA > 0.0f) ? (1.0f / T) : (-1.0f / T);
            if (dB != 0.0f && stepB == 0.0f) stepB = (dB > 0.0f) ? (1.0f / T) : (-1.0f / T);
            if (dC != 0.0f && stepC == 0.0f) stepC = (dC > 0.0f) ? (1.0f / T) : (-1.0f / T);

            porta_freq_step_f[DCO_A] = stepA;  // Hz per microsecond
            porta_freq_step_f[DCO_B] = stepB;
            porta_freq_step_f[DCO_C] = stepC;

          } else {
            // SLEW-RATE (musical) mode: glide linearly in note-space (semitones),
            // using the same rounding/step behaviour as the fixed Q16 code.
            float startNoteA = porta_note_cur_f[DCO_A];
            float startNoteB = porta_note_cur_f[DCO_B];
            float startNoteC = porta_note_cur_f[DCO_C];
            float targetNoteA = (float)note1;
            float targetNoteB = (float)note2;
            float targetNoteC = (float)note3;
  
            if (startNoteA == 0.0f) startNoteA = targetNoteA;
            if (startNoteB == 0.0f) startNoteB = targetNoteB;
            if (startNoteC == 0.0f) startNoteC = targetNoteC;
  
            porta_note_start_f[DCO_A] = startNoteA;
            porta_note_start_f[DCO_B] = startNoteB;
            porta_note_start_f[DCO_C] = startNoteC;
            porta_note_stop_f [DCO_A] = targetNoteA;
            porta_note_stop_f [DCO_B] = targetNoteB;
            porta_note_stop_f [DCO_C] = targetNoteC;
  
            float dNoteA = targetNoteA - startNoteA;
            float dNoteB = targetNoteB - startNoteB;
            float dNoteC = targetNoteC - startNoteC;
  
            // Float analogue of the Q16 step calculation:
            // stepNote ≈ round(dNote * 2^16 / T) / 2^16
            const float SCALE = 65536.0f;
            float T = (portaTime == 0) ? 1.0f : (float)portaTime;
            float halfT = 0.5f * T;
  
            float dA_q16 = dNoteA * SCALE;
            float dB_q16 = dNoteB * SCALE;
            float dC_q16 = dNoteC * SCALE;
  
            float numA = (dA_q16 >= 0.0f) ? (dA_q16 + halfT) : (dA_q16 - halfT);
            float numB = (dB_q16 >= 0.0f) ? (dB_q16 + halfT) : (dB_q16 - halfT);
            float numC = (dC_q16 >= 0.0f) ? (dC_q16 + halfT) : (dC_q16 - halfT);
  
            float step_q16_A = numA / T;
            float step_q16_B = numB / T;
            float step_q16_C = numC / T;
  
            float stepNoteA = step_q16_A / SCALE;
            float stepNoteB = step_q16_B / SCALE;
            float stepNoteC = step_q16_C / SCALE;
  
            // Ensure we always move for non-zero intervals (same intent as fixed code).
            if (dNoteA != 0.0f && stepNoteA == 0.0f)
              stepNoteA = (dNoteA > 0.0f) ? (1.0f / SCALE) : (-1.0f / SCALE);
            if (dNoteB != 0.0f && stepNoteB == 0.0f)
              stepNoteB = (dNoteB > 0.0f) ? (1.0f / SCALE) : (-1.0f / SCALE);
            if (dNoteC != 0.0f && stepNoteC == 0.0f)
              stepNoteC = (dNoteC > 0.0f) ? (1.0f / SCALE) : (-1.0f / SCALE);
  
            porta_note_step_f[DCO_A] = stepNoteA;   // semitones per microsecond
            porta_note_step_f[DCO_B] = stepNoteB;
            porta_note_step_f[DCO_C] = stepNoteC;
  
            porta_note_cur_f[DCO_A] = startNoteA;
            porta_note_cur_f[DCO_B] = startNoteB;
            porta_note_cur_f[DCO_C] = startNoteC;
  
            porta_freq_cur_f[DCO_A] = noteIndex_to_freqFloat(startNoteA);
            porta_freq_cur_f[DCO_B] = noteIndex_to_freqFloat(startNoteB);
            porta_freq_cur_f[DCO_C] = noteIndex_to_freqFloat(startNoteC);
          }
        }
  
        int32_t elapsed_us = (int32_t)portamentoTimer[i];
  
        float curA, curB, curC;
        if (portaMode == PORTA_MODE_TIME) {
          float startA = porta_freq_start_f[DCO_A];
          float startB = porta_freq_start_f[DCO_B];
          float startC = porta_freq_start_f[DCO_C];
          float stopA  = porta_freq_stop_f [DCO_A];
          float stopB  = porta_freq_stop_f [DCO_B];
          float stopC  = porta_freq_stop_f [DCO_C];

          if ((uint32_t)elapsed_us > portaTime || portaTime == 0) {
            curA = stopA;
            curB = stopB;
            curC = stopC;
          } else {
            curA = startA + porta_freq_step_f[DCO_A] * (float)elapsed_us;
            curB = startB + porta_freq_step_f[DCO_B] * (float)elapsed_us;
            curC = startC + porta_freq_step_f[DCO_C] * (float)elapsed_us;
          }

          porta_freq_cur_f[DCO_A] = curA;
          porta_freq_cur_f[DCO_B] = curB;
          porta_freq_cur_f[DCO_C] = curC;
        } else {
          float startNoteA = porta_note_start_f[DCO_A];
          float startNoteB = porta_note_start_f[DCO_B];
          float startNoteC = porta_note_start_f[DCO_C];
          float stopNoteA  = porta_note_stop_f [DCO_A];
          float stopNoteB  = porta_note_stop_f [DCO_B];
          float stopNoteC  = porta_note_stop_f [DCO_C];

          float dNoteA = stopNoteA - startNoteA;
          float dNoteB = stopNoteB - startNoteB;
          float dNoteC = stopNoteC - startNoteC;

          float curNoteA = startNoteA + porta_note_step_f[DCO_A] * (float)elapsed_us;
          float curNoteB = startNoteB + porta_note_step_f[DCO_B] * (float)elapsed_us;
          float curNoteC = startNoteC + porta_note_step_f[DCO_C] * (float)elapsed_us;

          // Clamp to stop as original
          if ((dNoteA >= 0.0f && curNoteA >= stopNoteA) ||
              (dNoteA <  0.0f && curNoteA <= stopNoteA)) {
            curNoteA = stopNoteA;
          }
          if ((dNoteB >= 0.0f && curNoteB >= stopNoteB) ||
              (dNoteB <  0.0f && curNoteB <= stopNoteB)) {
            curNoteB = stopNoteB;
          }
          if ((dNoteC >= 0.0f && curNoteC >= stopNoteC) ||
              (dNoteC <  0.0f && curNoteC <= stopNoteC)) {
            curNoteC = stopNoteC;
          }

          porta_note_cur_f[DCO_A] = curNoteA;
          porta_note_cur_f[DCO_B] = curNoteB;
          porta_note_cur_f[DCO_C] = curNoteC;

          curA = noteIndex_to_freqFloat(curNoteA);
          curB = noteIndex_to_freqFloat(curNoteB);
          curC = noteIndex_to_freqFloat(curNoteC);

          porta_freq_cur_f[DCO_A] = curA;
          porta_freq_cur_f[DCO_B] = curB;
          porta_freq_cur_f[DCO_C] = curC;
        }
  
        freqA = curA;
        freqB = curB;
        freqC = curC;

        // If the portamento time or mode control changed while gliding, retime the glide
        // from the *current* position so there is no pitch jump, only a change in glide speed/curve.
        if (portaTimeChanged || portaModeChanged) {
          float T = (portaTime == 0) ? 1.0f : (float)portaTime;

          portamentoStartMicros[i] = now_us;
          portamentoTimer[i]       = 0;

          if (portaMode == PORTA_MODE_TIME) {
            // Recompute time-based glide from current frequency.
            float targetA = noteFreq1;
            float targetB = noteFreq2;
            float targetC = noteFreq3;

            porta_freq_start_f[DCO_A] = curA;
            porta_freq_start_f[DCO_B] = curB;
            porta_freq_start_f[DCO_C] = curC;
            porta_freq_stop_f [DCO_A] = targetA;
            porta_freq_stop_f [DCO_B] = targetB;
            porta_freq_stop_f [DCO_C] = targetC;

            float dA = targetA - curA;
            float dB = targetB - curB;
            float dC = targetC - curC;
            float stepA = dA / T;
            float stepB = dB / T;
            float stepC = dC / T;
            if (dA != 0.0f && stepA == 0.0f) stepA = (dA > 0.0f) ? (1.0f / T) : (-1.0f / T);
            if (dB != 0.0f && stepB == 0.0f) stepB = (dB > 0.0f) ? (1.0f / T) : (-1.0f / T);
            if (dC != 0.0f && stepC == 0.0f) stepC = (dC > 0.0f) ? (1.0f / T) : (-1.0f / T);

            porta_freq_step_f[DCO_A] = stepA;
            porta_freq_step_f[DCO_B] = stepB;
            porta_freq_step_f[DCO_C] = stepC;
          } else {
            // Recompute slew-rate glide from current note position (same as fixed Q16 logic).
            float currentNoteA = porta_note_cur_f[DCO_A];
            float currentNoteB = porta_note_cur_f[DCO_B];
            float currentNoteC = porta_note_cur_f[DCO_C];
            float targetNoteA  = (float)note1;
            float targetNoteB  = (float)note2;
            float targetNoteC  = (float)note3;

            porta_note_start_f[DCO_A] = currentNoteA;
            porta_note_start_f[DCO_B] = currentNoteB;
            porta_note_start_f[DCO_C] = currentNoteC;
            porta_note_stop_f [DCO_A] = targetNoteA;
            porta_note_stop_f [DCO_B] = targetNoteB;
            porta_note_stop_f [DCO_C] = targetNoteC;

            float dNoteA = targetNoteA - currentNoteA;
            float dNoteB = targetNoteB - currentNoteB;
            float dNoteC = targetNoteC - currentNoteC;

            const float SCALE = 65536.0f;
            float T = (portaTime == 0) ? 1.0f : (float)portaTime;
            float halfT = 0.5f * T;

            float dA_q16 = dNoteA * SCALE;
            float dB_q16 = dNoteB * SCALE;
            float dC_q16 = dNoteC * SCALE;

            float numA = (dA_q16 >= 0.0f) ? (dA_q16 + halfT) : (dA_q16 - halfT);
            float numB = (dB_q16 >= 0.0f) ? (dB_q16 + halfT) : (dB_q16 - halfT);
            float numC = (dC_q16 >= 0.0f) ? (dC_q16 + halfT) : (dC_q16 - halfT);

            float step_q16_A = numA / T;
            float step_q16_B = numB / T;
            float step_q16_C = numC / T;

            float stepNoteA = step_q16_A / SCALE;
            float stepNoteB = step_q16_B / SCALE;
            float stepNoteC = step_q16_C / SCALE;

            if (dNoteA != 0.0f && stepNoteA == 0.0f)
              stepNoteA = (dNoteA > 0.0f) ? (1.0f / SCALE) : (-1.0f / SCALE);
            if (dNoteB != 0.0f && stepNoteB == 0.0f)
              stepNoteB = (dNoteB > 0.0f) ? (1.0f / SCALE) : (-1.0f / SCALE);
            if (dNoteC != 0.0f && stepNoteC == 0.0f)
              stepNoteC = (dNoteC > 0.0f) ? (1.0f / SCALE) : (-1.0f / SCALE);

            porta_note_step_f[DCO_A] = stepNoteA;
            porta_note_step_f[DCO_B] = stepNoteB;
            porta_note_step_f[DCO_C] = stepNoteC;

            // Keep Hz-domain state coherent at the new start.
            porta_freq_cur_f[DCO_A] = noteIndex_to_freqFloat(currentNoteA);
            porta_freq_cur_f[DCO_B] = noteIndex_to_freqFloat(currentNoteB);
            porta_freq_cur_f[DCO_C] = noteIndex_to_freqFloat(currentNoteC);
          }
        }
  
      } else {
        // No portamento
        freqA = noteFreq1;
        freqB = noteFreq2;
        freqC = noteFreq3;

        // Keep float portamento state coherent when portamento is off.
        porta_freq_cur_f[DCO_A] = freqA;
        porta_freq_cur_f[DCO_B] = freqB;
        porta_freq_cur_f[DCO_C] = freqC;
        porta_note_cur_f[DCO_A] = (float)note1;
        porta_note_cur_f[DCO_B] = (float)note2;
        porta_note_cur_f[DCO_C] = (float)note3;
      }
  
  #if DCO_DEBUG_REPORT
      dbg_freq_base_Hz = freqA;
  #endif
  
  #ifdef RUNNING_AVERAGE
      ra_portamento.addValue((float)(micros() - t_portamento));
  #endif
  
      // --- 2.4 ADSR detune (float equivalent of Q24) ---
  #ifdef RUNNING_AVERAGE
      unsigned long t_adsr = micros();
  #endif
      float ADSRModifier = 0.0f;
      if (ADSR1toDETUNE1 != 0) {
        float env = (float)linToLogLookup[ADSR1Level[i]];  // original int table
        float scale = (float)ADSR1toDETUNE1_scale_q24 / (float)(1 << 24);
        ADSRModifier = env * scale;
      }
      float ADSRModifierOSC1 = (ADSR3ToOscSelect == 0 || ADSR3ToOscSelect == 2 || ADSR3ToOscSelect == 4) ? ADSRModifier : 0.0f;
      float ADSRModifierOSC2 = (ADSR3ToOscSelect == 1 || ADSR3ToOscSelect == 2 || ADSR3ToOscSelect == 4) ? ADSRModifier : 0.0f;
      float ADSRModifierOSC3 = (ADSR3ToOscSelect == 3 || ADSR3ToOscSelect == 4) ? ADSRModifier : 0.0f;
  #ifdef RUNNING_AVERAGE
      ra_adsr_modifier.addValue((float)(micros() - t_adsr));
      unsigned long t_unison = micros();
  #endif
  
      // --- 2.5 Unison modifier (float equivalent) ---
      static constexpr float UNISON_SCALE = 0.0001f; // from original Q24 constant
      static constexpr int32_t OSC_UNISON_STEP[3] = { 0, 1, -1 };
      float unisonMODIFIER_OSC1 = (float)unisonDetune * UNISON_SCALE * (float)OSC_UNISON_STEP[0];
      float unisonMODIFIER_OSC2 = (float)unisonDetune * UNISON_SCALE * (float)OSC_UNISON_STEP[1];
      float unisonMODIFIER_OSC3 = (float)unisonDetune * UNISON_SCALE * (float)OSC_UNISON_STEP[2];
  #ifdef RUNNING_AVERAGE
      ra_unison_modifier.addValue((float)(micros() - t_unison));
      unsigned long t_drift = micros();
  #endif
  
      // --- 2.6 Drift modifiers (float) ---
      static constexpr float DRIFT_UNIT = 0.0000005f; // from original
      float driftScale = (float)analogDrift * DRIFT_UNIT;
      float DETUNE_DRIFT_OSC1 = (analogDrift != 0) ? (float)LFO_DRIFT_LEVEL[DCO_A] * driftScale : 0.0f;
      float DETUNE_DRIFT_OSC2 = (analogDrift != 0) ? (float)LFO_DRIFT_LEVEL[DCO_B] * driftScale : 0.0f;
      float DETUNE_DRIFT_OSC3 = (analogDrift != 0) ? (float)LFO_DRIFT_LEVEL[DCO_C] * driftScale : 0.0f;
  #ifdef RUNNING_AVERAGE
      ra_drift_multiplier.addValue((float)(micros() - t_drift));
  #endif
  
  #ifdef RUNNING_AVERAGE
      unsigned long t_modifiers = micros();
  #endif
  
      float detune_fifo = (float)DETUNE_INTERNAL_FIFO_q24 / (float)(1 << 24);
      float detune2      = (float)DETUNE_INTERNAL2_q24     / (float)(1 << 24);
      float detune3      = (float)DETUNE_INTERNAL3_q24     / (float)(1 << 24);
      float eps          = (float)Q24_ONE_EPS              / (float)(1 << 24);
      float pitchBendF   = calcPitchbend;
  
      float modifiersBase = detune_fifo + pitchBendF + eps;
      float freqModifiers1 = ADSRModifierOSC1 + DETUNE_DRIFT_OSC1 + modifiersBase + unisonMODIFIER_OSC1;
      float freqModifiers2 = ADSRModifierOSC2 + DETUNE_DRIFT_OSC2 + modifiersBase + unisonMODIFIER_OSC2 + detune2;
      float freqModifiers3 = ADSRModifierOSC3 + DETUNE_DRIFT_OSC3 + modifiersBase + unisonMODIFIER_OSC3 + detune3;
  
  #ifdef RUNNING_AVERAGE
      ra_modifiers_combination.addValue((float)(micros() - t_modifiers));
      unsigned long t_freq_scaling_x = micros();
  #endif
  
      // --- 2.7 Multiplier table x scaling & ratio interpolation (float version) ---
      float x1 = freqModifiers1 * (float)multiplierTableScale;
      float x2 = freqModifiers2 * (float)multiplierTableScale;
      float x3 = freqModifiers3 * (float)multiplierTableScale;
  
  #ifdef RUNNING_AVERAGE
      ra_freq_scaling_x.addValue((float)(micros() - t_freq_scaling_x));
      unsigned long t_freq_scaling_ratio = micros();
  #endif
  
  #if PITCH_USE_RATIO_Q16
      float ratio1 = interpolateRatioFloat_cached(x1, DCO_A);
      float ratio2 = interpolateRatioFloat_cached(x2, DCO_B);
      float ratio3 = interpolateRatioFloat_cached(x3, DCO_C);
  #ifdef RUNNING_AVERAGE
      ra_freq_scaling_ratio.addValue((float)(micros() - t_freq_scaling_ratio));
      unsigned long t_freq_scaling_post = micros();
  #endif
      // Apply ratios to portamento frequencies, with osc2/osc3 detune (Hz domain).
      float freqA_Hz = freqA * ratio1;
      float freqB_Hz = freqB * (ratio2 * osc2DetuneRatio);
      float freqC_Hz = freqC * (ratio3 * osc3DetuneRatio);
  #else
      // If you keep the IntQ16 path, you can still derive ratio as float from that
      ...
  #endif
  
  #if DCO_DEBUG_REPORT
      dbg_freq_after_mod_Hz = freqA_Hz;
  #endif
  
  #ifdef RUNNING_AVERAGE
      ra_freq_scaling_post.addValue((float)(micros() - t_freq_scaling_post));
  #endif
  
      // --- 2.8 Clock divider calculation (float equivalent) ---
  #ifdef RUNNING_AVERAGE
      unsigned long t_clk_div = micros();
  #endif

      float totalCycles1_f = (float)sysClock_Hz / freqA_Hz;
      float totalCycles2_f = (float)sysClock_Hz / freqB_Hz;
      float totalCycles3_f = (float)sysClock_Hz / freqC_Hz;

      float correction = 0.0f;   // keep your measured correction if needed
      float total_osr_val1_f = totalCycles1_f - (float)T_HIGH_TOTAL_CYCLES
                             - (float)T_LOW_OVERHEAD_CYCLES + correction;
      uint32_t clk_div1 = (uint32_t)((total_osr_val1_f / (float)NUM_OSR_CHUNKS) + 0.5f);

      float phaseDelay_f = 0.0f;
      if (oscSync > 1 && phaseAlignOSC2 != 0) {
        phaseDelay_f = totalCycles2_f * ((float)phaseAlignOSC2 / 360.0f);
      }
      float y_val2_f = (float)pioPulseLength + phaseDelay_f;
      float high_total_cycles2_f = y_val2_f + (float)T_HIGH_OVERHEAD_CYCLES;
      float total_osr_val2_f = totalCycles2_f - high_total_cycles2_f
                             - (float)T_LOW_OVERHEAD_CYCLES + correction;
      uint32_t clk_div2 = (uint32_t)((total_osr_val2_f / (float)NUM_OSR_CHUNKS) + 0.5f);

      float total_osr_val3_f = totalCycles3_f - (float)T_HIGH_TOTAL_CYCLES
                             - (float)T_LOW_OVERHEAD_CYCLES + correction;
      uint32_t clk_div3 = (uint32_t)((total_osr_val3_f / (float)NUM_OSR_CHUNKS) + 0.5f);

  #ifdef RUNNING_AVERAGE
      ra_clk_div_calc.addValue((float)(micros() - t_clk_div));
  #endif
  
      // --- 2.9 Amplitude compensation using engine-agnostic helper ---
  #ifdef RUNNING_AVERAGE
      unsigned long t_chan_level = micros();
  #endif

      uint16_t chanLevel, chanLevel2, chanLevel3;
      switch (syncMode) {
        case 0:
          chanLevel  = get_chan_level_for_engine(freqA_Hz, DCO_A);
          chanLevel2 = get_chan_level_for_engine(freqB_Hz, DCO_B);
          chanLevel3 = get_chan_level_for_engine(freqC_Hz, DCO_C);
          break;
        case 1: {
          float maxFreq = (freqA_Hz > freqB_Hz) ? freqA_Hz : freqB_Hz;
          chanLevel  = get_chan_level_for_engine(maxFreq, DCO_A);
          chanLevel2 = get_chan_level_for_engine(freqB_Hz, DCO_B);
          chanLevel3 = get_chan_level_for_engine(freqC_Hz, DCO_C);
          break;
        }
        case 2: {
          float maxFreq = (freqA_Hz > freqB_Hz) ? freqA_Hz : freqB_Hz;
          chanLevel  = get_chan_level_for_engine(freqA_Hz, DCO_A);
          chanLevel2 = get_chan_level_for_engine(maxFreq, DCO_B);
          chanLevel3 = get_chan_level_for_engine(freqC_Hz, DCO_C);
          break;
        }
        default:
          chanLevel  = get_chan_level_for_engine(freqA_Hz, DCO_A);
          chanLevel2 = get_chan_level_for_engine(freqB_Hz, DCO_B);
          chanLevel3 = get_chan_level_for_engine(freqC_Hz, DCO_C);
          break;
      }
  #ifdef RUNNING_AVERAGE
      ra_get_chan_level.addValue((float)(micros() - t_chan_level));
  #endif
  
      // --- 2.10 PIO + PWM + PW math (very close to original, but float inside PW calc) ---
      uint8_t pioNumberA = VOICE_TO_PIO[DCO_A];
      uint8_t pioNumberB = VOICE_TO_PIO[DCO_B];
      uint8_t pioNumberC = VOICE_TO_PIO[DCO_C];
      PIO pioN_A = pio[VOICE_TO_PIO[DCO_A]];
      PIO pioN_B = pio[VOICE_TO_PIO[DCO_B]];
      PIO pioN_C = pio[VOICE_TO_PIO[DCO_C]];
      uint8_t sm1N = VOICE_TO_SM[DCO_A];
      uint8_t sm2N = VOICE_TO_SM[DCO_B];
      uint8_t smCN = VOICE_TO_SM[DCO_C];
  
      pio_sm_put(pioN_A, sm1N, clk_div1);
      pio_sm_put(pioN_B, sm2N, clk_div2);
      pio_sm_put(pioN_C, smCN, clk_div3);
      pio_sm_exec(pioN_A, sm1N, pio_encode_pull(false, false));
      pio_sm_exec(pioN_B, sm2N, pio_encode_pull(false, false));
      pio_sm_exec(pioN_C, smCN, pio_encode_pull(false, false));
  
      if (note_on_flag_flag[i]) {
        // Sync logic mirrored from fixed-point voice_task, using float-derived clk_div and phase.
        if (oscSync == 1) {
          pio_sm_exec(pioN_A, sm1N, pio_encode_jmp(10 + offset[pioNumberA]));  // OSC Sync MODE
          pio_sm_exec(pioN_B, sm2N, pio_encode_jmp(10 + offset[pioNumberB]));
        }

        if (oscSync > 1) {
          // OSC1/OSC2 live on different PIO blocks — enable/disable each SM separately.
          pio_sm_set_enabled(pioN_A, sm1N, false);
          pio_sm_set_enabled(pioN_B, sm2N, false);

          pio_sm_clear_fifos(pioN_B, sm2N);
          pio_sm_clear_fifos(pioN_A, sm1N);

          uint32_t y_val2_u = (uint32_t)(y_val2_f + 0.5f);
          pio_sm_put(pioN_B, sm2N, y_val2_u);
          pio_sm_exec(pioN_B, sm2N, pio_encode_pull(false, false));
          pio_sm_exec(pioN_B, sm2N, pio_encode_out(pio_y, 31));

          pio_sm_put(pioN_A, sm1N, clk_div1);
          pio_sm_put(pioN_B, sm2N, clk_div2);
          pio_sm_exec(pioN_A, sm1N, pio_encode_pull(false, true));
          pio_sm_exec(pioN_B, sm2N, pio_encode_pull(false, true));

          pio_sm_exec(pioN_A, sm1N, pio_encode_jmp(10 + offset[pioNumberA]));  // OSC Sync MODE
          pio_sm_exec(pioN_B, sm2N, pio_encode_jmp(10 + offset[pioNumberB]));

          pio_sm_set_enabled(pioN_A, sm1N, true);
          pio_sm_set_enabled(pioN_B, sm2N, true);
        }

        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], pwm_gpio_to_channel(RANGE_PINS[DCO_C]), chanLevel3);
      }
  
      if (timer99microsFlag) {
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], pwm_gpio_to_channel(RANGE_PINS[DCO_C]), chanLevel3);
  
        if (sqr1Status) {
  #ifdef RUNNING_AVERAGE
          unsigned long t_pwm = micros();
  #endif
          float adsr1_delta = ((float)ADSR1Level[i] * (float)local_ADSR1toPWM) / 2048.0f; // 2^11
          float lfo2_delta  = ((float)LFO2Level    * (float)local_LFO2toPW)   / 512.0f;   // 2^9
          float pw_calc =
              (float)DIV_COUNTER_PW - 1.0f
            - (float)PW[0]
            - lfo2_delta
            + adsr1_delta;
  
          if (pw_calc < 0.0f) pw_calc = 0.0f;
          if (pw_calc > (float)(DIV_COUNTER_PW - 1)) pw_calc = (float)(DIV_COUNTER_PW - 1);
  
          PW_PWM[i] = (uint16_t)pw_calc;
  #ifdef RUNNING_AVERAGE
          ra_pwm_calculations.addValue((float)(micros() - t_pwm));
  #endif
          pwm_set_chan_level(PW_PWM_SLICES[i],
                             pwm_gpio_to_channel(PW_PINS[i]),
                             get_PW_level_interpolated(PW_PWM[i], i));
        } else {
          pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), 0);
        }
      }
  
      note_on_flag_flag[i] = false;
    }
  
  #ifdef RUNNING_AVERAGE
    unsigned long voice_task_duration = micros() - voice_task_start_time;
    ra_voice_task_total.addValue((float)voice_task_duration);
    if (voice_task_duration > voice_task_max_time) {
      voice_task_max_time = voice_task_duration;
    }
  #endif

  last_portamento_time = portaTime;
  last_portamento_mode = portaMode;
}
#endif  // USE_FLOAT_VOICE_TASK

// Round-robin free-voice allocator. Called from note_on() when polyMode == 1.
inline uint8_t get_free_voice_sequential() {
  uint8_t nextVoice;
  uint8_t freeVoices = 0;

  if (VOICES[VOICES_LAST_SEQUENCE[NUM_VOICES_TOTAL - 1]] == 1 || VOICES[VOICES_LAST_SEQUENCE[NUM_VOICES_TOTAL - 1]] == 0) {
    for (int voiceIndex = NUM_VOICES_TOTAL - 1; voiceIndex > 0; voiceIndex--) {
      if (VOICES[VOICES_LAST_SEQUENCE[voiceIndex]] == 0) {
        nextVoice = VOICES_LAST_SEQUENCE[voiceIndex];
        freeVoices = 1;
        for (int freeIndex = voiceIndex; freeIndex > 0; freeIndex--) {
          VOICES_LAST_SEQUENCE[freeIndex] = VOICES_LAST_SEQUENCE[freeIndex - 1];
        }
        VOICES_LAST_SEQUENCE[0] = nextVoice;
        return nextVoice;
      }
    }
  } else {
    if (VOICES[VOICES_LAST_SEQUENCE[NUM_VOICES_TOTAL - 1]] == 0) {
      nextVoice = VOICES_LAST_SEQUENCE[NUM_VOICES_TOTAL - 1];

      for (int voiceIndex = NUM_VOICES_TOTAL - 1; voiceIndex > 0; voiceIndex--) {
        VOICES_LAST_SEQUENCE[voiceIndex] = VOICES_LAST_SEQUENCE[voiceIndex - 1];
      }

      VOICES_LAST_SEQUENCE[0] = nextVoice;

      return nextVoice;
    }
  }
  if (freeVoices == 0) {
    nextVoice = VOICES_LAST_SEQUENCE[NUM_VOICES_TOTAL - 1];

    for (int voiceIndex = NUM_VOICES_TOTAL - 1; voiceIndex > 0; voiceIndex--) {
      VOICES_LAST_SEQUENCE[voiceIndex] = VOICES_LAST_SEQUENCE[voiceIndex - 1];
    }

    VOICES_LAST_SEQUENCE[0] = nextVoice;
  }
  return nextVoice;
}

// Oldest-voice / steal allocator. Called from note_on() when polyMode == 0.
inline uint8_t get_free_voice() {
  uint32_t oldest_time = millis();
  uint8_t oldest_voice = 0;

  for (int i = 0; i < NUM_VOICES_TOTAL; i++)  // REVISAR!!
  {
    uint8_t n = (NEXT_VOICE + i) % NUM_VOICES_TOTAL;

    if (VOICES[n] == 0) {
      NEXT_VOICE = (n + 1) % NUM_VOICES_TOTAL;
      return n;
    }

    if (VOICES[i] < oldest_time) {
      oldest_time = VOICES[i];
      oldest_voice = i;
    }
  }

  NEXT_VOICE = (oldest_voice + 1) % NUM_VOICES_TOTAL;
  return oldest_voice;
}

// Map voiceMode → NUM_VOICES / STACK_VOICES. Called from init_voices and apply_param_voice_mode.
inline void setVoiceMode() {
  switch (voiceMode) {
    case 0:
      NUM_VOICES = 1;
      STACK_VOICES = 1;
      break;
    case 1:
      NUM_VOICES = NUM_VOICES_TOTAL;
      STACK_VOICES = 1;
      break;
    case 2:
      NUM_VOICES = NUM_VOICES_TOTAL;
      STACK_VOICES = NUM_VOICES_TOTAL;
      break;
  }
}

// Reconfigure PIO sideset pins for oscillator sync topology and retrigger voices.
// Called from apply_param_sync_mode (Serial2).
void setSyncMode() {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    uint8_t sidesetPin;
    switch (syncMode) {
      case 0:
        sidesetPin = RESET_PINS[i];
        break;
      case 1:
        // OSC2 syncs from OSC1; OSC3 free-running
        if (i == 1) {
          sidesetPin = RESET_PINS[0];
        } else {
          sidesetPin = RESET_PINS[i];
        }
        break;
      case 2:
        // OSC1 syncs from OSC2; OSC3 free-running
        if (i == 0) {
          sidesetPin = RESET_PINS[1];
        } else {
          sidesetPin = RESET_PINS[i];
        }
        break;
      default:
        sidesetPin = RESET_PINS[i];
        break;
    }

    pio_sm_set_sideset_pins(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i], sidesetPin);
    pio_gpio_init(pio[VOICE_TO_PIO[i]], sidesetPin);
    pio_sm_restart(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i]);  // IS THIS NEEDED ?
  }

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    note_on_flag[i] = 1;
  }
}

#ifndef USE_FLOAT_AMP_COMP
/**
 * @brief Fast amplitude compensation lookup tuned for the RP2040.
 *
 * Uses a deterministic window selection rule that matches the float path:
 * for each oscillator we choose the *first* window i such that
 *   freqRow[i] <= x < freqRow[i+2]
 * scanning from low to high. This guarantees that a given x always maps to the
 * same window regardless of whether we are gliding up or down in frequency
 * (no hysteresis), while keeping the table small enough that a linear scan
 * remains very cheap on the M0+.
 *
 * The core calculation uses the proven, numerically stable fixed-point
 * quadratic method with precomputed Q-format coefficients.
 */
uint16_t get_chan_level_lookup_fast(int32_t x, uint8_t voiceN) {
  // --- 1. Load pointers to this oscillator's table rows (cache friendly) ---
  const int32_t* freqRow   = ampCompFrequencyArray[voiceN];
  const int32_t* ampRow    = ampCompArray[voiceN];
  const int32_t* xBaseRow  = xBaseWIN[voiceN];
  const int32_t* spanRow   = dxWIN[voiceN];
  const uint32_t* invRow_q28 = invDxWIN_q28[voiceN];
  const int32_t* aRow      = aQWIN_fast[voiceN];
  const int32_t* bRow      = bQWIN_fast[voiceN];
  const uint16_t* cRow     = cQWIN[voiceN];

  // --- 2. Handle boundary conditions ---
  if (x <= freqRow[0]) return (uint16_t)ampRow[0];
  const int lastIdx = ampCompTableSize;

  // --- 2a. Global plateau clamp using precomputed plateau start ---
  // If the calibrated table provides a real breakpoint with freq < AMP_COMP_MAX_HZ
  // and amp == DIV_COUNTER, clamp everything at/above that frequency directly
  // to DIV_COUNTER. This avoids evaluating the quadratic in the plateau region
  // and ignores any synthetic high-frequency filler points.
  if (plateauStartIndex[voiceN] >= 0) {
    int32_t plateauFreqQ = plateauStartFreqQ[voiceN];
    if (x >= plateauFreqQ) {
      return (uint16_t)DIV_COUNTER;
    }
  }

  // --- 3. Find the correct window with a deterministic linear scan ---
  // We deliberately scan in ascending order and pick the *first* window that
  // covers x in the [freqRow[i], freqRow[i+2]) sense. This matches the float
  // implementation and avoids path-dependent window choices when the windows
  // overlap, which is what previously caused different results when
  // approaching the plateau from above vs below.
  const int maxWindow = ampCompTableSize - 2;
  int window = 0;
  for (int i = 0; i <= maxWindow; ++i) {
    if (x >= freqRow[i] && x < freqRow[i + 2]) {
      window = i;
      break;
    }
  }

  // --- 4. Core quadratic calculation ---
  // This path is now fully branchless for maximum speed.
  int32_t dx = x - xBaseRow[window];
  const int32_t span = spanRow[window];
  if (dx < 0) dx = 0;
  if (dx > span) dx = span;

  // Calculate t in Q(T_FRAC) using a clean 64-bit multiply-shift.
  // T_FRAC is 14, matching the legacy high-precision path.
  const uint32_t inv_q28 = invRow_q28[window];
  uint32_t t_q = (uint32_t)(((uint64_t)dx * inv_q28) >> (28 - T_FRAC));

  // y(t) = a*t^2 + b*t + c
  // All intermediate math uses 64-bit to prevent overflow.
  int64_t a = aRow[window];
  int64_t b = bRow[window];
  int32_t c = cRow[window];

  // Perform the quadratic evaluation with correct scaling at each step.
  uint32_t t2 = (uint32_t)(((uint32_t)t_q * t_q) >> T_FRAC);
  int32_t term_a = (int32_t)((a * t2) >> T_FRAC);
  int32_t term_b = (int32_t)((b * t_q) >> T_FRAC);

  // Sum the terms (all are Q(T_FRAC)) and then scale back to Q0 with rounding.
  int32_t y_q = term_a + term_b + (c << T_FRAC);
  int32_t y = (y_q + (1 << (T_FRAC - 1))) >> T_FRAC;

  // --- 6. Clamp and return final value ---
  if (y < 0) y = 0;
  if (y > (int32_t)DIV_COUNTER) y = (int32_t)DIV_COUNTER;

  return (uint16_t)y;
}
#endif  // !USE_FLOAT_AMP_COMP

#ifdef USE_FLOAT_AMP_COMP
/**
 * @brief Pure-float amplitude compensation lookup (Hz domain) for the RP2350 float engine.
 *
 * Uses sanitized float-domain frequency table (Hz) and precomputed quadratic coefficients,
 * with plateau handling and the same window layout as the fixed-point path.
 */
inline uint16_t get_chan_level_float(float freqHz, uint8_t voiceN) {
  // Boundary conditions in Hz (direct table access for minimum overhead)
  if (freqHz <= ampCompFrequencyHz[voiceN][0]) {
    return ampCompArray[voiceN][0];
  }

  // Global plateau clamp using precomputed plateau start (Hz domain).
  // Once we reach the first real DIV_COUNTER point below AMP_COMP_MAX_HZ,
  // treat the response as fully saturated.
  if (plateauStartIndex[voiceN] >= 0) {
    float plateauFreqHz = plateauStartFreqHz[voiceN];
    if (freqHz >= plateauFreqHz) {
      return (uint16_t)DIV_COUNTER;
    }
  }

  // --- Window selection: deterministic linear scan, mirroring fixed-point path ---
  // We have (ampCompTableSize - 1) quadratic windows, each using points
  // (i, i+1, i+2); the last window (index ampCompTableSize-2) uses the sentinel
  // at ampCompTableSize as its right endpoint. We scan from low to high and
  // pick the *first* window whose [x0, x2) interval contains freqHz, ensuring
  // hysteresis-free behaviour when approaching the plateau from either side.
  const int maxWindow = ampCompTableSize - 2;
  int window = 0;
  for (int i = 0; i <= maxWindow; ++i) {
    if (freqHz >= ampCompFrequencyHz[voiceN][i] &&
        freqHz <  ampCompFrequencyHz[voiceN][i + 2]) {
      window = i;
      break;
    }
  }

  float a = aCoeff[voiceN][window];
  float b = bCoeff[voiceN][window];
  float c = cCoeff[voiceN][window];

  float interpolatedValue = (a * freqHz + b) * freqHz + c;
  return round(interpolatedValue);
}
#endif  // USE_FLOAT_AMP_COMP

// Map raw PW counter into calibrated center/limits for a voice. Used on the 99 µs PW update path.
inline uint16_t get_PW_level_interpolated(uint16_t PWval, uint8_t voiceN) {

  uint16_t chanLevel;

  // Horizontal PW axis: 0 .. DIV_COUNTER_PW-1 (pot/LFO/ADSR domain)
  // Vertical axis (output): mapped to calibrated low/center/high PWM limits.

  if (PWval >= (DIV_COUNTER_PW - 1)) {
    // Above max PW, clamp to calibrated high limit.
    return PW_HIGH_LIMIT[voiceN];
  } else if (PWval <= 0) {
    // Below min PW, clamp to calibrated low limit.
    return PW_LOW_LIMIT[voiceN];
  } else {
    uint16_t pwLowBreak  = PW_LOOKUP[0];  // usually 0
    uint16_t pwMidBreak  = PW_LOOKUP[1];  // mid-point
    uint16_t pwHighBreak = PW_LOOKUP[2];  // usually DIV_COUNTER_PW-1

    if (PWval >= pwMidBreak) {
      // Upper half: interpolate from center to high limit.
      chanLevel = map(PWval,
                      pwMidBreak, pwHighBreak,
                      PW_CENTER[voiceN], PW_HIGH_LIMIT[voiceN]);
    } else {
      // Lower half: interpolate from low limit to center.
      chanLevel = map(PWval,
                      pwLowBreak, pwMidBreak,
                      PW_LOW_LIMIT[voiceN], PW_CENTER[voiceN]);
    }

    return chanLevel;
  }
}

// Drive one oscillator for calibration measurement (manual cal and nested auto-cal probes).
void voice_task_autotune(uint8_t taskAutotuneVoiceMode, uint16_t calibrationValue) {

  float freq;
  uint8_t note1;  // = 57;
  int chanLevel = ampCompCalibrationVal;

  if (VOICE_NOTES[0] > 0) {
    note1 = VOICE_NOTES[0] - 12;
  }

  if (taskAutotuneVoiceMode == 1 || taskAutotuneVoiceMode == 4) {
    freq = PIDOutput;
  } else {
    freq = (float)sNotePitches[note1];
  }

  uint32_t clk_div1 = (uint32_t)(((float)sysClock_Hz / freq) - pioPulseLength) / NUM_OSR_CHUNKS;

  if (freq == 0)
  clk_div1 = 0;

  if (manualCalibrationFlag == true) {  // One Ocillator at a time to get correct gap

    uint8_t currentCalibrationOscillator = (uint8_t)manualCalibrationStage;
    if (currentCalibrationOscillator >= NUM_OSCILLATORS) {
      currentCalibrationOscillator = NUM_OSCILLATORS - 1;
    }

    // ALL AT ONCE
    for (int i = 0; i < NUM_OSCILLATORS; i++) {
      uint8_t pioNumber = VOICE_TO_PIO[i];
      PIO pioN = pio[VOICE_TO_PIO[i]];
      uint8_t sm1N = VOICE_TO_SM[i];

      if (i != currentCalibrationOscillator) {
        uint32_t silence_clk_div1 = 200;

        pio_sm_put(pioN, sm1N, silence_clk_div1);
        pio_sm_exec(pioN, sm1N, pio_encode_pull(false, false));
        pwm_set_chan_level(RANGE_PWM_SLICES[i], pwm_gpio_to_channel(RANGE_PINS[i]), 0);
      } else {

        pio_sm_put(pioN, sm1N, clk_div1);

        pio_sm_exec(pioN, sm1N, pio_encode_pull(false, false));

        pwm_set_chan_level(RANGE_PWM_SLICES[i], pwm_gpio_to_channel(RANGE_PINS[i]), calibrationValue);

        pwm_set_chan_level(PW_PWM_SLICES[0], pwm_gpio_to_channel(PW_PINS[0]), 0);

        //Serial.println((String) "currentCalibrationOscillator: " + (int)currentCalibrationOscillator + (String) "   calibrationValue: " + (int)calibrationValue);
      }
    }
  } else {

    uint8_t pioNumber = VOICE_TO_PIO[currentDCO];
    PIO pioN = pio[VOICE_TO_PIO[currentDCO]];
    uint8_t sm1N = VOICE_TO_SM[currentDCO];

    pio_sm_put(pioN, sm1N, clk_div1);
    pio_sm_exec(pioN, sm1N, pio_encode_pull(false, false));

    switch (taskAutotuneVoiceMode) {
      case 0:
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], pwm_gpio_to_channel(RANGE_PINS[currentDCO]), calibrationValue);
        break;
      case 1:
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], pwm_gpio_to_channel(RANGE_PINS[currentDCO]), calibrationValue);
        pio_sm_exec(pioN, sm1N, pio_encode_jmp(10 + offset[pioNumber]));
        break;
      case 2:
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], pwm_gpio_to_channel(RANGE_PINS[currentDCO]), chanLevel);
        break;
      case 3:
        chanLevel = get_chan_level_for_engine(freq, currentDCO);
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], pwm_gpio_to_channel(RANGE_PINS[currentDCO]), chanLevel);
      case 4:
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], pwm_gpio_to_channel(RANGE_PINS[currentDCO]), calibrationValue);
        break;
    }

    //}
    // Serial.println((String) "| currentDCO: " + currentDCO + (String) " | freq: " + freq + (String) " | clk_div1: " + clk_div1 + (String) " | ampCompCalibrationVal: " + ampCompCalibrationVal);
  }
}

// Cached variant: pass DCO index to reuse last segment and avoid binary search
inline int32_t interpolatePitchMultiplierIntQ16_cached(int32_t xQ16, int dcoIndex) {
  int32_t xInt = xQ16 >> 16;
  // Clamp to bounds using integer part
  if (xInt <= xMultiplierTable[0]) {
    return yMultiplierTable[0];
  }
  if (xInt >= xMultiplierTable[multiplierTableSize - 1]) {
    return yMultiplierTable[multiplierTableSize - 1];
  }
  int low = interpSegCache[dcoIndex];
  // Validate cache; adjust locally if possible
  if (low < 0 || low > multiplierTableSize - 2 || !(xMultiplierTable[low] <= xInt && xInt < xMultiplierTable[low + 1])) {
    // Try step toward correct segment
    if (low >= 0 && low < multiplierTableSize - 1) {
      if (xInt >= xMultiplierTable[low + 1]) {
        while (low < multiplierTableSize - 2 && xInt >= xMultiplierTable[low + 1]) low++;
      } else if (xInt < xMultiplierTable[low]) {
        while (low > 0 && xInt < xMultiplierTable[low]) low--;
      }
    }
    // If still wrong, do binary search
    if (!(low >= 0 && low < multiplierTableSize - 1 && xMultiplierTable[low] <= xInt && xInt < xMultiplierTable[low + 1])) {
      int l = 0, h = multiplierTableSize - 1;
      while (l <= h) {
        int m = (l + h) >> 1;
        if (xMultiplierTable[m] <= xInt && xInt < xMultiplierTable[m + 1]) {
          low = m;
          break;
        } else if (xInt < xMultiplierTable[m]) {
          h = m - 1;
        } else {
          l = m + 1;
        }
      }
      if (low < 0) low = 0;
      if (low > multiplierTableSize - 2) low = multiplierTableSize - 2;
    }
    interpSegCache[dcoIndex] = (int16_t)low;
  }
  int32_t x0 = xMultiplierTable[low];
  int32_t y0 = yMultiplierTable[low];
  int32_t slope = slopeQ20[low];
#ifdef PITCH_INTERP_USE_Q8
  // 32-bit friendly path: slope in Q8, delta in Q8; total 16 frac bits
  int32_t deltaQ8 = (xQ16 - (x0 << 16)) >> 8;
  int32_t slope8 = slopeQ8[low];
  // Product is Q16; shift by 16 to return table units (with rounding)
  int32_t y = y0 + (int32_t)((((int64_t)deltaQ8 * (int64_t)slope8) + (1LL << 15)) >> 16);
#elif defined(PITCH_INTERP_USE_Q12)
  // Medium-precision path: slope in Q12, delta in Q12; total 24 frac bits
  int32_t deltaQ12 = (xQ16 - (x0 << 16)) >> 4;
  int32_t slope12 = slopeQ12[low];
  int32_t y = y0 + (int32_t)((((int64_t)deltaQ12 * (int64_t)slope12) + (1LL << 23)) >> 24);
#else
  // High-precision path: slope in Q20, delta in Q16
  int32_t deltaQ16 = xQ16 - (x0 << 16);
  int32_t y = y0 + (int32_t)((((int64_t)deltaQ16 * (int64_t)slope) + (1LL << 35)) >> 36);
#endif
  return y;
}

// Cached Q16 ratio interpolator: returns multiplier ratio in Q16 without divide
inline int32_t interpolateRatioQ16_cached(int32_t xQ16, int dcoIndex) {
  int32_t xInt = xQ16 >> 16;
  // Clamp to bounds using integer part
  if (xInt <= xMultiplierTable[0]) {
    // Convert table y->Q16 ratio with rounding using reciprocal-multiply (n/10000 ≈ (n * M) >> 45)
    uint64_t num0 = ((uint64_t)(uint32_t)yMultiplierTable[0] << 16) + 5000u;
    return (int32_t)((num0 * 0xD1B71759ULL) >> 45);
  }
  if (xInt >= xMultiplierTable[multiplierTableSize - 1]) {
    uint64_t numN = ((uint64_t)(uint32_t)yMultiplierTable[multiplierTableSize - 1] << 16) + 5000u;
    return (int32_t)((numN * 0xD1B71759ULL) >> 45);
  }
  int low = interpSegCache[dcoIndex];
  // Validate cache; adjust locally if possible
  if (low < 0 || low > multiplierTableSize - 2 || !(xMultiplierTable[low] <= xInt && xInt < xMultiplierTable[low + 1])) {
    // Try step toward correct segment
    if (low >= 0 && low < multiplierTableSize - 1) {
      if (xInt >= xMultiplierTable[low + 1]) {
        while (low < multiplierTableSize - 2 && xInt >= xMultiplierTable[low + 1]) low++;
      } else if (xInt < xMultiplierTable[low]) {
        while (low > 0 && xInt < xMultiplierTable[low]) low--;
      }
    }
    // If still wrong, do binary search
    if (!(low >= 0 && low < multiplierTableSize - 1 && xMultiplierTable[low] <= xInt && xInt < xMultiplierTable[low + 1])) {
      int l = 0, h = multiplierTableSize - 1;
      while (l <= h) {
        int m = (l + h) >> 1;
        if (xMultiplierTable[m] <= xInt && xInt < xMultiplierTable[m + 1]) {
          low = m;
          break;
        } else if (xInt < xMultiplierTable[m]) {
          h = m - 1;
        } else {
          l = m + 1;
        }
      }
      if (low < 0) low = 0;
      if (low > multiplierTableSize - 2) low = multiplierTableSize - 2;
    }
    interpSegCache[dcoIndex] = (int16_t)low;
  }
  // Interpolate y in table units using high-precision slope (same as IntQ16 path)
  int32_t x0 = xMultiplierTable[low];
  int32_t y0 = yMultiplierTable[low];
  int32_t slope = slopeQ20[low];
  int32_t deltaQ16 = xQ16 - (x0 << 16);
  int32_t yTab = y0 + (int32_t)((((int64_t)deltaQ16 * (int64_t)slope) + (1LL << 35)) >> 36);
  // Convert y (table units) to ratio Q16 with rounding using reciprocal-multiply
  uint64_t num = ((uint64_t)(uint32_t)yTab << 16) + 5000u;  // scale/2
  int32_t ratioQ16 = (int32_t)((num * 0xD1B71759ULL) >> 45);
  return ratioQ16;
}

// Float ratio interpolator: same table/segment logic as interpolateRatioQ16_cached,
// but operates directly in float "table units" and returns a float ratio.
inline float interpolateRatioFloat_cached(float x, int dcoIndex) {
  // Interpret x in same "table units" domain as xMultiplierTableF
  // Clamp to bounds using float table values (mirrors integer path behaviour)
  if (x <= xMultiplierTableF[0]) {
    return yMultiplierTableF[0] / (float)multiplierTableScale;
  }
  if (x >= xMultiplierTableF[multiplierTableSize - 1]) {
    return yMultiplierTableF[multiplierTableSize - 1] / (float)multiplierTableScale;
  }

  int low = interpSegCache[dcoIndex];

  // Validate cache; adjust locally if possible
  if (low < 0 || low > multiplierTableSize - 2 ||
      !(xMultiplierTableF[low] <= x && x < xMultiplierTableF[low + 1])) {
    // Try stepping toward correct segment
    if (low >= 0 && low < multiplierTableSize - 1) {
      if (x >= xMultiplierTableF[low + 1]) {
        while (low < multiplierTableSize - 2 && x >= xMultiplierTableF[low + 1]) {
          ++low;
        }
      } else if (x < xMultiplierTableF[low]) {
        while (low > 0 && x < xMultiplierTableF[low]) {
          --low;
        }
      }
    }
    // If still wrong, fall back to binary search
    if (!(low >= 0 && low < multiplierTableSize - 1 &&
          xMultiplierTableF[low] <= x && x < xMultiplierTableF[low + 1])) {
      int l = 0;
      int h = multiplierTableSize - 1;
      while (l <= h) {
        int m = (l + h) >> 1;
        if (xMultiplierTableF[m] <= x && x < xMultiplierTableF[m + 1]) {
          low = m;
          break;
        } else if (x < xMultiplierTableF[m]) {
          h = m - 1;
        } else {
          l = m + 1;
        }
      }
      if (low < 0) low = 0;
      if (low > multiplierTableSize - 2) low = multiplierTableSize - 2;
    }
    interpSegCache[dcoIndex] = (int16_t)low;
  }

  // Linear interpolation in table units using precomputed float slopes
  float x0 = xMultiplierTableF[low];
  float y0 = yMultiplierTableF[low];
  float m  = slopeF[low];                 // dy/dx as float
  float yTab = y0 + m * (x - x0);         // table units

  // Convert table units to ratio (same as yTab / multiplierTableScale)
  float ratio = yTab / (float)multiplierTableScale;
  return ratio;
}
// Build integer/float pitch-multiplier tables and slopes (boot). Called from init_voices().
void initMultiplierTables() {

  float y_value;
  double divisor = multiplierTableSize;
  double fraction = 4.00d / divisor;

  // Build analytic tables once, then quantize for fixed engine and keep high-precision
  // float mirrors for the float engine.
  for (int i = 0; i < multiplierTableSize; i++) {
    double x;

    if (i == 0) {
      x = -1.00d;
      y_value = 0.25d;
    } else if (i == multiplierTableSize - 1) {
      x = 3.0d;
      y_value = 4.0d;
    } else {
      x = (-1.00d + (fraction * (double)i));
      y_value = expInterpolationSolveY(x + 1.00d, 1.00d, 3.00d, 0.50d, 2.00d);
    }

    // Integer tables used by the fixed-point engine (unchanged behaviour).
    xMultiplierTable[i] = (int32_t)(x * (double)multiplierTableScale);
    yMultiplierTable[i] = (int32_t)(y_value * (double)multiplierTableScale);
    x0Q16_tbl[i]        = xMultiplierTable[i] << 16;

    // High-precision float mirrors used by the float engine: keep the analytic values
    // directly in table units (no extra quantisation beyond multiplierTableScale).
    xMultiplierTableF[i] = (float)(x * (double)multiplierTableScale);
    yMultiplierTableF[i] = (float)(y_value * (double)multiplierTableScale);
  }
  // Precompute slopes for fast integer interpolation
  for (int i = 0; i < (multiplierTableSize - 1); ++i) {
    int32_t dx = xMultiplierTable[i + 1] - xMultiplierTable[i];
    if (dx == 0) dx = 1;
    // Precompute slope in Q20 for fast multiply-only interpolation
    int32_t dy = yMultiplierTable[i + 1] - yMultiplierTable[i];
    int64_t numSlope = ((int64_t)dy << 20) + (dx > 0 ? dx / 2 : -dx / 2);
    slopeQ20[i] = (int32_t)(numSlope / (int64_t)dx);
#ifdef PITCH_INTERP_USE_Q8
    // Optional lower-precision slope for 32-bit fast path
    int64_t numSlope8 = ((int64_t)dy << 8) + (dx > 0 ? dx / 2 : -dx / 2);
    slopeQ8[i] = (int32_t)(numSlope8 / (int64_t)dx);
#endif
#ifdef PITCH_INTERP_USE_Q12
    // Medium-precision slope for balanced speed/accuracy
    int64_t numSlope12 = ((int64_t)dy << 12) + (dx > 0 ? dx / 2 : -dx / 2);
    slopeQ12[i] = (int32_t)(numSlope12 / (int64_t)dx);
#endif
  }
  // Initialize per-DCO cache to invalid
  for (int d = 0; d < NUM_OSCILLATORS; ++d) interpSegCache[d] = -1;

  for (int i = 0; i < multiplierTableSize - 1; ++i) {
    // Float slopes computed directly from the float tables to avoid inheriting
    // fixed-point quantisation error.
    float dxF = xMultiplierTableF[i + 1] - xMultiplierTableF[i];
    if (dxF == 0.0f) dxF = 1.0f;
    float dyF = yMultiplierTableF[i + 1] - yMultiplierTableF[i];
    slopeF[i] = dyF / dxF;
  }
}

#ifdef RUNNING_AVERAGE
// Print RUNNING_AVERAGE timing stats for voice-task phases. Called from print_running_averages().
void print_voice_task_timings() {
  Serial.println("\n=== VOICE_TASK TIMING STATISTICS (microseconds) ===");
  Serial.print("Pitch Bend Calc:      ");
  if (ra_pitchbend.getCount() > 0) Serial.println(ra_pitchbend.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("OSC2 Detune:          ");
  if (ra_osc2_detune.getCount() > 0) Serial.println(ra_osc2_detune.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("Portamento:           ");
  if (ra_portamento.getCount() > 0) Serial.println(ra_portamento.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("ADSR Modifier:        ");
  if (ra_adsr_modifier.getCount() > 0) Serial.println(ra_adsr_modifier.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("Unison Modifier:      ");
  if (ra_unison_modifier.getCount() > 0) Serial.println(ra_unison_modifier.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("Drift Modifier:       ");
  if (ra_drift_multiplier.getCount() > 0) Serial.println(ra_drift_multiplier.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("Modifiers Combination:");
  if (ra_modifiers_combination.getCount() > 0) Serial.println(ra_modifiers_combination.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("Freq Scaling x:       ");
  if (ra_freq_scaling_x.getCount() > 0) Serial.println(ra_freq_scaling_x.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("Freq Scaling ratio:   ");
  if (ra_freq_scaling_ratio.getCount() > 0) Serial.println(ra_freq_scaling_ratio.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("Freq Scaling post:    ");
  if (ra_freq_scaling_post.getCount() > 0) Serial.println(ra_freq_scaling_post.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("Get Chan Level:       ");
  if (ra_get_chan_level.getCount() > 0) Serial.println(ra_get_chan_level.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("Clock Div Calc:       ");
  if (ra_clk_div_calc.getCount() > 0) Serial.println(ra_clk_div_calc.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("PWM Calculations:     ");
  if (ra_pwm_calculations.getCount() > 0) Serial.println(ra_pwm_calculations.getFastAverage(), 2);
  else Serial.println("N/A");

  Serial.print("Voice Task Total:     ");
  Serial.print(ra_voice_task_total.getFastAverage(), 2);
  Serial.print(" avg, max ");
  Serial.println(voice_task_max_time);

#ifdef CLKDIV_BENCHMARK
  // Print clock-divider float vs double comparison stats
  if (clkdiv_bench_count > 0) {
    double avgFloatUs  = clkdiv_time_float_sum_us  / (double)clkdiv_bench_count;
    double avgDoubleUs = clkdiv_time_double_sum_us / (double)clkdiv_bench_count;
    double avgDelta1   = (double)clkdiv_delta1_sum / (double)clkdiv_bench_count;
    double avgDelta2   = (double)clkdiv_delta2_sum / (double)clkdiv_bench_count;
    double avgFreqDiff1 = clkdiv_freq1_diff_sum / (double)clkdiv_bench_count;
    double avgFreqDiff2 = clkdiv_freq2_diff_sum / (double)clkdiv_bench_count;

    Serial.println("=== CLKDIV BENCH ===");
    Serial.print("count=");
    Serial.print(clkdiv_bench_count);
    Serial.print(" avgFloatUs=");
    Serial.print(avgFloatUs, 3);
    Serial.print(" avgDoubleUs=");
    Serial.print(avgDoubleUs, 3);
    Serial.println();

    Serial.print("clk_div1 delta avg=");
    Serial.print(avgDelta1, 3);
    Serial.print(" maxAbs=");
    Serial.print(clkdiv_delta1_max);
    Serial.println();

    Serial.print("clk_div2 delta avg=");
    Serial.print(avgDelta2, 3);
    Serial.print(" maxAbs=");
    Serial.print(clkdiv_delta2_max);
    Serial.println();

    Serial.print("freq1 diff avg=");
    Serial.print(avgFreqDiff1, 6);
    Serial.print(" Hz maxAbs=");
    Serial.print(clkdiv_freq1_diff_max_abs, 6);
    Serial.println();

    Serial.print("freq2 diff avg=");
    Serial.print(avgFreqDiff2, 6);
    Serial.print(" Hz maxAbs=");
    Serial.print(clkdiv_freq2_diff_max_abs, 6);
    Serial.println();

    // Print example of last measured target vs output frequencies
    Serial.print("OSC1 last: target=");
    Serial.print(clkdiv_last_target1_Hz, 6);
    Serial.print(" Hz float=");
    Serial.print(clkdiv_last_out1_float_Hz, 6);
    Serial.print(" Hz double=");
    Serial.print(clkdiv_last_out1_double_Hz, 6);
    Serial.println(" Hz");

    Serial.print("OSC2 last: target=");
    Serial.print(clkdiv_last_target2_Hz, 6);
    Serial.print(" Hz float=");
    Serial.print(clkdiv_last_out2_float_Hz, 6);
    Serial.print(" Hz double=");
    Serial.print(clkdiv_last_out2_double_Hz, 6);
    Serial.println(" Hz");
    Serial.println("====================");

    // Reset accumulators
    clkdiv_bench_count         = 0;
    clkdiv_time_float_sum_us  = 0.0;
    clkdiv_time_double_sum_us = 0.0;
    clkdiv_delta1_sum = clkdiv_delta2_sum = 0;
    clkdiv_delta1_max = clkdiv_delta2_max = 0;
    clkdiv_freq1_diff_sum = clkdiv_freq2_diff_sum = 0.0;
    clkdiv_freq1_diff_max_abs = clkdiv_freq2_diff_max_abs = 0.0;
  }
#endif

  Serial.println("===================================================\n");
}
#endif
#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/wave_mux.ino"
// Dual 74HC595 wave enables (Mainboard RoxMux port — bit-bang, no RoxMux dep).
// Active-low enables; pin indices match Mainboard "crossed cables" map (4 voice slots).

#ifdef ENABLE_WAVE_MUX

static uint16_t waveMuxBits = 0xFFFF;  // all off (1 = disabled)

// Crossed-cable map from Mainboard/waveSelector.h (voice slots 0..3).
static const uint8_t triPins[4]  = { 14, 10, 6, 2 };
static const uint8_t sinePins[4] = { 13, 9, 5, 1 };
static const uint8_t saw2Pins[4] = { 12, 8, 4, 0 };
static const uint8_t sawPins[4]  = { 15, 11, 7, 3 };

static inline void waveMuxWritePin(uint8_t pin, bool high) {
  if (pin > 15) return;
  if (high) {
    waveMuxBits |= (uint16_t)(1u << pin);
  } else {
    waveMuxBits &= (uint16_t)~(1u << pin);
  }
}

static void waveMuxShiftOut() {
  // Shift MSB first into daisy-chain (chip2 then chip1 convention used by Rox74HC595).
  for (int i = 15; i >= 0; --i) {
    gpio_put(HC595_DATA_PIN, (waveMuxBits >> i) & 1u);
    gpio_put(HC595_CLK_PIN, 1);
    busy_wait_us_32(1);
    gpio_put(HC595_CLK_PIN, 0);
  }
  gpio_put(HC595_LATCH_PIN, 1);
  busy_wait_us_32(1);
  gpio_put(HC595_LATCH_PIN, 0);
}

void init_waveSelector() {
  pinMode(HC595_LATCH_PIN, OUTPUT);
  pinMode(HC595_DATA_PIN, OUTPUT);
  pinMode(HC595_CLK_PIN, OUTPUT);
  digitalWrite(HC595_LATCH_PIN, LOW);
  digitalWrite(HC595_CLK_PIN, LOW);
  digitalWrite(HC595_DATA_PIN, LOW);
  waveMuxBits = 0xFFFF;
  waveMuxShiftOut();
}

// Update one wave family (0–3) or all (4) from status flags. Monosynth: only slot 0 live.
void update_waveSelector(byte wave) {
  switch (wave) {
    case 0:
      for (int i = 0; i < 4; i++) {
        waveMuxWritePin(sawPins[i], (i < NUM_VOICES) ? !sawStatus : 1);
      }
      break;
    case 1:
      for (int i = 0; i < 4; i++) {
        waveMuxWritePin(saw2Pins[i], (i < NUM_VOICES) ? !saw2Status : 1);
      }
      break;
    case 2:
      for (int i = 0; i < 4; i++) {
        waveMuxWritePin(triPins[i], (i < NUM_VOICES) ? !triStatus : 1);
      }
      break;
    case 3:
      for (int i = 0; i < 4; i++) {
        waveMuxWritePin(sinePins[i], (i < NUM_VOICES) ? !sqr2Status : 1);
      }
      break;
    case 4:
      for (int i = 0; i < 4; i++) {
        waveMuxWritePin(sawPins[i], (i < NUM_VOICES) ? !sawStatus : 1);
        waveMuxWritePin(saw2Pins[i], (i < NUM_VOICES) ? !saw2Status : 1);
        waveMuxWritePin(triPins[i], (i < NUM_VOICES) ? !triStatus : 1);
        waveMuxWritePin(sinePins[i], (i < NUM_VOICES) ? !sqr2Status : 1);
      }
      break;
    default:
      break;
  }
  waveMuxShiftOut();
}

#else  // !ENABLE_WAVE_MUX

void init_waveSelector() {}
void update_waveSelector(byte) {}

#endif

