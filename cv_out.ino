/**
 * @file DCO-cv_out.ino
 * @brief DCO Control Voltage & Pitch Modulation Math (RP2040/RP2350).
 * @details Evaluates polyphonic pitch, PW, and detune modulation via the Shared Matrix Engine.
 */

 #include "include_all.h"
 #include <string.h>
 
 // =============================================================================
 // DCO SHADOW STORAGE & DELTA BUFFERS
 // =============================================================================
 uint16_t ADSR_VCA_attack = 0, ADSR_VCA_decay = 0, ADSR_VCA_sustain = 0, ADSR_VCA_release = 0;
 uint16_t ADSR_VCF_attack = 0, ADSR_VCF_decay = 0, ADSR_VCF_sustain = 0, ADSR_VCF_release = 0;
 bool ADSR1Restart = true, ADSR2Restart = true, ADSR3Restart = true;
 
 uint8_t ADSR1AttackCurveVal = 1, ADSR1DecayCurveVal = 2, ADSR1ReleaseCurveVal = 1;
 uint8_t ADSR2AttackCurveVal = 4, ADSR2DecayCurveVal = 6, ADSR2ReleaseCurveVal = 1;
 uint8_t ADSR3AttackCurveVal = 7, ADSR3DecayCurveVal = 7, ADSR3ReleaseCurveVal = 7;
 
 uint16_t CUTOFF = 1024, RESONANCE = 0, LFO2toVCF = 0, VCALevel = 0, LFO1toVCA = 0;
 int16_t ADSR2toVCF = 0, ADSR1toVCA = 0;
 uint16_t DIST_DRIVE = 0, DIST_MIX = 0;
 uint8_t FILTER_MODE = 0;
 
 int32_t ADSR2toVCF_scale_q15 = 0, LFO2toVCF_scale_q15 = 0, LFO1toVCA_scale_q15 = 0;
 int32_t VCFKeytrackModifier_q15 = 32768;
 int32_t VCFKeytrackPerVoice_q15[NUM_VOICES_TOTAL] = {32768, 32768, 32768, 32768};
 int32_t velocityToVCF_q15 = 0, velocityToVCA_q15 = 0, vcf_drift_scale_q15 = 0;
 volatile int16_t VCF_DRIFT[NUM_VOICES_TOTAL] = {0};
 
 volatile int32_t matrix_pitch_mod_q24[NUM_VOICES_TOTAL] = {0};
 volatile int32_t matrix_pw_mod[NUM_VOICES_TOTAL]        = {0};
 volatile int32_t matrix_detune_mod[NUM_VOICES_TOTAL]    = {0};
 
 bool RESONANCEAmpCompensation = true;
 int16_t VCAResonanceCompensation = 100, VCFKeytrack = 0;
 int8_t velocityToVCFVal = 0, velocityToVCAVal = 0;
 
 uint16_t VCA_PWM[NUM_VOICES_TOTAL] = {0};
 uint16_t VCF_PWM[NUM_VOICES_TOTAL] = {0};
 uint16_t RESONANCE_PWM[NUM_FILTERS] = {0};
 uint16_t AS2164_VCA_linearize_table[4096] = {0};
 
 int16_t OSC1LevelVal = 0, OSC2LevelVal = 0, OSC3LevelVal = 0, SubLevelVal = 0;
 uint16_t OSC1Level = 0, OSC2Level = 0, OSC3Level = 0, SubLevel = 0;
 bool ADSR3Enabled = false;
 
 #ifdef ENABLE_CV_OUTS
 static constexpr int32_t CV_U12_MAX = 4095;
 static inline uint16_t cv_clamp_u12(int32_t v) {
   if (v < 0) return 0;
   if (v > CV_U12_MAX) return (uint16_t)CV_U12_MAX;
   return (uint16_t)v;
 }
 #endif
 
 /**
  * @brief Boot initialization for DCO CV / lookup structures.
  */
 void init_cv_out() {
 #ifdef ENABLE_CV_OUTS
   generateBezierArray({ 0, 4095 }, { 4095, 0 }, { 150, 1420 }, { -235, 815 }, 4096, AS2164_VCA_linearize_table);
   cv_update_mod_scales();
 #endif
 }
 
 // These scales are handled strictly on the STM32 Mainboard side.
 void cv_bake_adsr2_to_vcf_scale() {}
 void cv_bake_lfo2_to_vcf_scale()  {}
 void cv_bake_lfo1_to_vca_scale()  {}
 void cv_update_mod_scales()       {}
 
 /**
  * @brief Hot path (~10-20 kHz): Evaluates modulation matrix deltas for all voices.
  */
 void __not_in_flash_func(update_CV_outs)() {
 #ifndef ENABLE_MB_MOD_STREAM
   // 1. If in calibration, zero the modulation buffers immediately
   if (manualCalibrationFlag) {
     for (uint8_t i = 0; i < NUM_VOICES_TOTAL; i++) {
       mod_matrix_clear_voice(i);
       matrix_pitch_mod_q24[i] = 0;
       matrix_pw_mod[i]        = 0;
       matrix_detune_mod[i]    = 0;
     }
     return;
   }
 
   // 2. Ingest all voice sources into a contiguous array (Zero stack reallocation)
   ModSources sources[NUM_VOICES_TOTAL];
   const int16_t pb = (int16_t)((int32_t)midi_pitch_bend - 8192);
 
   for (uint8_t i = 0; i < NUM_VOICES_TOTAL; i++) {
     const uint8_t oscA = (uint8_t)(i * 2);
     sources[i].lfo1          = LFO1Level;
     sources[i].lfo2          = LFO2Level;
     sources[i].pitch_bend    = pb;
     sources[i].drift_global  = LFO_DRIFT_LEVEL[0];
     sources[i].drift_voice   = LFO_DRIFT_LEVEL[oscA];
     sources[i].env_vca       = 0;
     sources[i].env_vcf       = 0;
     sources[i].env_dco       = ADSR3Level_q15[i];
     sources[i].velocity      = velocity[i];
     sources[i].keytrack_note = VOICE_NOTES[i] ? VOICE_NOTES[i] : 60;
   }
 
   // 3. High-Performance Evaluation: Single pass over active slots for all voices
   mod_matrix_accumulate_all(sources, NUM_VOICES_TOTAL);
 
   // 4. Extract hardware modulation deltas
   for (uint8_t i = 0; i < NUM_VOICES_TOTAL; i++) {
     matrix_pitch_mod_q24[i] = mod_matrix_get_dest(i, DEST_PITCH);
     matrix_pw_mod[i]        = mod_matrix_get_dest(i, DEST_PW);
     matrix_detune_mod[i]    = mod_matrix_get_dest(i, DEST_OSC2_DETUNE);
   }
 
 #ifdef ENABLE_CV_OUTS
   // Standalone bench CV output loop (If hardware VCA/VCF is wired locally)
   uint16_t dist_out = DIST_DRIVE;
   uint16_t dist_mix_out = DIST_MIX;
   dist_out     = apply_mod_dir_12b(DIST_DRIVE, mod_matrix_get_dest(0, DEST_DIST_DRIVE));
   dist_mix_out = apply_mod_dir_12b(DIST_MIX, mod_matrix_get_dest(0, DEST_DIST_MIX));
 #endif
 
 #endif // !ENABLE_MB_MOD_STREAM
 }
 
 /**
  * @brief Manual calibration wave selector setup.
  */
 void update_CV_outs_manual_calibration() {
 #ifndef ENABLE_CV_OUTS
   byte stage = (byte)manualCalibrationStage;
   waveSelector_manual_calibration(stage);
 #else
   // Standalone bench calibration hook
 #endif
 }