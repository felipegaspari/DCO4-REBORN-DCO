// /**
//  * @file mod_matrix.ino
//  * @brief DCO Sparse Modulation Matrix Implementation.
//  * @details Implements dynamic slot-depth chaining, independent per-voice S&H, and polyphonic routing.
//  */

//  #include "include_all.h"
//  #include <string.h>
 
//  // =============================================================================
//  // STORAGE DEFINITIONS FOR EXTERN VARIABLES
//  // =============================================================================
 
//  uint8_t presetName[16] = { 32, 32, 32, 32, 32, 32, 32, 32,
//                             32, 32, 32, 32, 32, 32, 32, 32 };
//  uint8_t velocity[NUM_VOICES_TOTAL] = { 0 };
 
//  uint16_t ADSR_VCA_attack  = 0;
//  uint16_t ADSR_VCA_decay   = 0;
//  uint16_t ADSR_VCA_sustain = 4095;
//  uint16_t ADSR_VCA_release = 0;
 
//  uint16_t ADSR_VCF_attack  = 0;
//  uint16_t ADSR_VCF_decay   = 0;
//  uint16_t ADSR_VCF_sustain = 4095;
//  uint16_t ADSR_VCF_release = 0;
 
//  uint8_t ADSR1AttackCurveVal  = 1;
//  uint8_t ADSR1DecayCurveVal   = 2;
//  uint8_t ADSR1ReleaseCurveVal = 1;
 
//  uint8_t ADSR2AttackCurveVal  = 4;
//  uint8_t ADSR2DecayCurveVal   = 6;
//  uint8_t ADSR2ReleaseCurveVal = 1;
 
//  uint8_t ADSR3AttackCurveVal  = 7;
//  uint8_t ADSR3DecayCurveVal   = 7;
//  uint8_t ADSR3ReleaseCurveVal = 7;
 
//  uint16_t CUTOFF     = 1024;
//  uint16_t RESONANCE  = 0;
//  int16_t  ADSR2toVCF = 0;
//  uint16_t LFO2toVCF  = 0;
//  int16_t  ADSR1toVCA = 0;
//  uint16_t VCALevel   = 0;
//  uint16_t LFO1toVCA  = 0;
 
//  uint16_t DIST_DRIVE  = 0;
//  uint16_t DIST_MIX    = 0;
//  uint8_t  FILTER_MODE = 0;
 
//  int32_t vcf_drift_scale_q15 = 0;
 
//  // Polyphonic pitch modulation state
//  volatile int32_t matrix_pitch_mod_q24[NUM_VOICES_TOTAL] = { 0 };
 
//  // =============================================================================
//  // MOD MATRIX INTERNAL STATE
//  // =============================================================================
 
//  static ModSlot g_mod_slots[MOD_SLOT_COUNT];
//  static int16_t random_sh_q15[NUM_VOICES_TOTAL] = { 0 };
//  static uint8_t mod_aftertouch = 0;
//  static uint8_t mod_wheel = 0;
//  static uint8_t g_mod_live_mask = 0;
//  static uint8_t g_mod_pitch_mask = 0;
 
//  // Per-voice S&H attack edge detection
//  static int16_t last_env_level[NUM_VOICES_TOTAL] = { 0 };
//  static bool in_attack[NUM_VOICES_TOTAL] = { false };
 
//  // Frame-to-frame persistent depth chaining state (Allows backward routing)
//  static int32_t prev_depth_mods[NUM_VOICES_TOTAL][MOD_SLOT_COUNT];
 
//  static void mod_refresh_slot_live(uint8_t slot) {
//    const ModSlot& s = g_mod_slots[slot];
//    const uint8_t bit = (uint8_t)(1u << slot);
//    const bool live = (s.source != SRC_OFF && s.source < MOD_SRC_COUNT && s.dest < MOD_DEST_COUNT);
//    if (live) {
//      g_mod_live_mask |= bit;
//      if (s.dest == DEST_PITCH) g_mod_pitch_mask |= bit;
//      else g_mod_pitch_mask &= (uint8_t)~bit;
//    } else {
//      g_mod_live_mask &= (uint8_t)~bit;
//      g_mod_pitch_mask &= (uint8_t)~bit;
//    }
//  }
 
//  static constexpr int32_t MOD_PITCH_TO_Q24_MUL = (int32_t)(((int64_t)1 << 24) * 32768 / (int64_t)MOD_PITCH_DEPTH_FULL);
 
//  static inline uint16_t mod_clamp_u16(int32_t v) { return (v < 0) ? 0 : ((v > 4095) ? 4095 : (uint16_t)v); }
//  static inline int32_t mod_clamp_q15(int32_t v)  { return (v < -32768) ? -32768 : ((v > 32767) ? 32767 : v); }
 
//  void mod_matrix_init() {
//    for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
//      g_mod_slots[i].source = SRC_OFF;
//      g_mod_slots[i].dest = MOD_DEST_EMPTY;
//      g_mod_slots[i].depth = 0;
//    }
//    g_mod_live_mask = 0; 
//    g_mod_pitch_mask = 0;
//    mod_aftertouch = 0; 
//    mod_wheel = 0;
 
//    for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
//      random_sh_q15[i] = 0;
//      matrix_pitch_mod_q24[i] = 0;
//      last_env_level[i] = 0;
//      in_attack[i] = false;
//      for (int s = 0; s < MOD_SLOT_COUNT; s++) {
//        prev_depth_mods[i][s] = 0;
//      }
//    }
//  }
 
//  // Per-voice autonomous Sample & Hold tick
//  static void update_sample_and_hold_voice(uint8_t voice) {
//    const int16_t cur_env_level = ADSR_VCA_Level_q15[voice];
//    if (cur_env_level > last_env_level[voice]) {
//      if (!in_attack[voice]) {
//        in_attack[voice] = true;
//        random_sh_q15[voice] = (int16_t)random(-32768, 32767);
//      }
//    } else if (cur_env_level < last_env_level[voice]) {
//      in_attack[voice] = false;
//    }
//    last_env_level[voice] = cur_env_level;
//  }
 
//  // Polyphonic pitch evaluation for a specific voice
//  int32_t mod_matrix_eval_pitch_voice_q24(uint8_t voice, int16_t lfo1_q15, int16_t lfo2_q15) {
//    if (g_mod_pitch_mask == 0 || voice >= NUM_VOICES_TOTAL) return 0;
 
//    update_sample_and_hold_voice(voice);
 
//    int32_t temp_sums[MOD_DEST_COUNT] = { 0 };
 
//    for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
//      if (g_mod_slots[i].source == SRC_OFF) continue;
 
//      // Apply slot depth chaining (including backward modulated depth from previous frame)
//      int32_t current_depth = (int32_t)g_mod_slots[i].depth + prev_depth_mods[voice][i];
//      current_depth = mod_clamp_q15(current_depth);
//      if (current_depth == 0) continue;
 
//      // Evaluate source for this specific voice
//      int32_t src_val = 0;
//      switch (g_mod_slots[i].source) {
//        case SRC_LFO1:        src_val = (int32_t)lfo1_q15; break;
//        case SRC_LFO2:        src_val = (int32_t)lfo2_q15; break;
//        case SRC_MODWHEEL:    src_val = (int32_t)mod_wheel * 258; break;
//        case SRC_AFTERTC:     src_val = (int32_t)mod_aftertouch * 258; break;
//        case SRC_BEND:        src_val = ((int32_t)midi_pitch_bend - 8192) << 2; break;
//        case SRC_DRIFT:       src_val = (int32_t)LFO_DRIFT_LEVEL[0]; break;
 
//        // Polyphonic per-voice sources
//        case SRC_ENV_DCO:     src_val = (int32_t)ADSR3Level_q15[voice]; break;
//        case SRC_ENV_VCA:     src_val = (int32_t)ADSR_VCA_Level_q15[voice]; break;
//        case SRC_VELOCITY:    src_val = (int32_t)midi_velocity[voice] * 258; break;
//        case SRC_KEYTRACK:    src_val = mod_clamp_q15(((int32_t)sNotePitches[voice] - 60) * 682); break;
//        case SRC_DRIFT_VOICE: src_val = (int32_t)LFO_DRIFT_LEVEL[voice]; break;
//        case SRC_RANDOM_SH:   src_val = (int32_t)random_sh_q15[voice]; break;
//        case SRC_VOICE_ID:    
//          src_val = ((int32_t)voice * 65535) / (NUM_VOICES_TOTAL > 1 ? (NUM_VOICES_TOTAL - 1) : 1) - 32768; 
//          break;
//        default:              src_val = 0; break;
//      }
 
//      if (g_mod_slots[i].dest < MOD_DEST_COUNT) {
//        temp_sums[g_mod_slots[i].dest] += (src_val * current_depth) >> 15;
//      }
//    }
 
//    // Update persistent depth modulation state for the next control frame
//    for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
//      if (DEST_MOD_SLOT0_DEPTH + i < MOD_DEST_COUNT) {
//        prev_depth_mods[voice][i] = temp_sums[DEST_MOD_SLOT0_DEPTH + i];
//      }
//    }
 
//    return mod_matrix_pitch_to_q24(temp_sums[DEST_PITCH]);
//  }
 
//  void mod_matrix_set_source(uint8_t slot, int16_t v) {
//    if (slot >= MOD_SLOT_COUNT) return;
//    g_mod_slots[slot].source = (v < 0 || v >= (int16_t)MOD_SRC_COUNT) ? SRC_OFF : (uint8_t)v;
//    mod_refresh_slot_live(slot);
//  }
 
//  void mod_matrix_set_dest(uint8_t slot, int16_t v) {
//    if (slot >= MOD_SLOT_COUNT) return;
//    g_mod_slots[slot].dest = (v < 0 || v >= (int16_t)MOD_DEST_COUNT) ? MOD_DEST_EMPTY : (uint8_t)v;
//    mod_refresh_slot_live(slot);
//  }
 
//  void mod_matrix_set_depth(uint8_t slot, int16_t v) {
//    if (slot >= MOD_SLOT_COUNT) return;
//    g_mod_slots[slot].depth = v;
//    mod_refresh_slot_live(slot);
//  }
 
//  void mod_matrix_on_note_on(uint8_t voice) {
//    if (voice < NUM_VOICES_TOTAL) {
//      random_sh_q15[voice] = (int16_t)random(-32768, 32767);
//    }
//  }
 
//  void mod_matrix_set_aftertouch(uint8_t pressure) { mod_aftertouch = pressure; }
//  void mod_matrix_set_mod_wheel(uint8_t value)     { mod_wheel = value; }
 
//  static int32_t mod_matrix_read_source_mono_q15(uint8_t src, int16_t lfo1_q15, int16_t lfo2_q15) {
//    switch (src) {
//      case SRC_ENV_DCO:    return (int32_t)ADSR3Level_q15[0];
//      case SRC_ENV_VCA:    return (int32_t)ADSR_VCA_Level_q15[0];
//      case SRC_ENV_VCF:    return (int32_t)ADSR_VCF_Level_q15;
//      case SRC_VELOCITY:   return (int32_t)midi_velocity[0] * 258;
//      case SRC_KEYTRACK: {
//        const uint8_t n = sNotePitches[0];
//        if (n == 0) return 0;
//        return mod_clamp_q15(((int32_t)n - 60) * 682);
//      }
//      case SRC_RANDOM_SH:  return (int32_t)random_sh_q15[0];
//      case SRC_AFTERTC:    return (int32_t)mod_aftertouch * 258;
//      case SRC_LFO1:       return (int32_t)lfo1_q15;
//      case SRC_LFO2:       return (int32_t)lfo2_q15;
//      case SRC_BEND:       return ((int32_t)midi_pitch_bend - 8192) << 2;
//      case SRC_MODWHEEL:   return (int32_t)mod_wheel * 258;
//      case SRC_DRIFT:
//      case SRC_DRIFT_VOICE:return (int32_t)LFO_DRIFT_LEVEL[0];
//      case SRC_VOICE_ID:   return 0;
//      default:             return 0;
//    }
//  }
 
//  void mod_matrix_accumulate(int32_t dest_sums[MOD_DEST_COUNT], int16_t lfo1_q15, int16_t lfo2_q15) {
//    memset(dest_sums, 0, sizeof(int32_t) * MOD_DEST_COUNT);
//    if (g_mod_live_mask == 0) return;
 
//    update_sample_and_hold_voice(0);
 
//    for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
//      if (g_mod_slots[i].source == SRC_OFF) continue;
 
//      int32_t current_depth = (int32_t)g_mod_slots[i].depth + prev_depth_mods[0][i];
//      current_depth = mod_clamp_q15(current_depth);
//      if (current_depth == 0) continue;
 
//      int32_t src_val = mod_matrix_read_source_mono_q15(g_mod_slots[i].source, lfo1_q15, lfo2_q15);
//      if (g_mod_slots[i].dest < MOD_DEST_COUNT) {
//        dest_sums[g_mod_slots[i].dest] += (src_val * current_depth) >> 15;
//      }
//    }
 
//    for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
//      if (DEST_MOD_SLOT0_DEPTH + i < MOD_DEST_COUNT) {
//        prev_depth_mods[0][i] = dest_sums[DEST_MOD_SLOT0_DEPTH + i];
//      }
//    }
//  }
 
//  int32_t mod_matrix_eval_pitch_q24(int16_t lfo1_q15, int16_t lfo2_q15) {
//    return mod_matrix_eval_pitch_voice_q24(0, lfo1_q15, lfo2_q15);
//  }
 
//  int32_t mod_matrix_pitch_to_q24(int32_t pitch_s) {
//    if (pitch_s > MOD_PITCH_DEPTH_FULL) pitch_s = MOD_PITCH_DEPTH_FULL;
//    if (pitch_s < -MOD_PITCH_DEPTH_FULL) pitch_s = -MOD_PITCH_DEPTH_FULL;
//    return (int32_t)(((int64_t)pitch_s * (int64_t)MOD_PITCH_TO_Q24_MUL) >> 15);
//  }
 
//  void mod_matrix_apply_cv(const int32_t dest_sums[MOD_DEST_COUNT], uint16_t* dist_drive_out, uint16_t* dist_mix_out) {
//    auto apply_dir_12b = [](uint16_t base, int32_t mod) -> uint16_t {
//      int32_t calc = (int32_t)base + (mod >> 3);
//      if (calc < 0) return 0;
//      if (calc > 4095) return 4095;
//      return (uint16_t)calc;
//    };
 
//    if (dist_drive_out) *dist_drive_out = apply_dir_12b(DIST_DRIVE, dest_sums[DEST_DIST_DRIVE]);
//    if (dist_mix_out)   *dist_mix_out   = apply_dir_12b(DIST_MIX, dest_sums[DEST_DIST_MIX]);
//  }