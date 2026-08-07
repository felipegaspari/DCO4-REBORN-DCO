#include "include_all.h"
#include <string.h>

static ModSlot g_mod_slots[MOD_SLOT_COUNT];
static int16_t mod_random_snh_q15 = 0;
static uint8_t mod_aftertouch = 0;
static uint8_t mod_wheel = 0;

volatile int32_t matrix_pitch_mod_q24 = 0;

// Q15 ≈ level * 32768 / LFO*_CC_HALF via (level * MUL) >> 16.
static constexpr uint32_t MOD_LFO1_TO_Q15_MUL = (32768u << 16) / (uint32_t)LFO1_CC_HALF;
static constexpr uint32_t MOD_LFO2_TO_Q15_MUL = (32768u << 16) / (uint32_t)LFO2_CC_HALF;

static inline uint16_t mod_clamp_u16(int32_t v) {
  if (v < 0) return 0;
  if (v > 4095) return 4095;
  return (uint16_t)v;
}

static inline int32_t mod_clamp_q15(int32_t v) {
  if (v < -32768) return -32768;
  if (v > 32768) return 32768;
  return v;
}

void mod_matrix_init() {
  for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
    g_mod_slots[i].source = MOD_SRC_EMPTY;
    g_mod_slots[i].dest = MOD_DEST_EMPTY;
    g_mod_slots[i].depth = 0;
  }
  mod_random_snh_q15 = 0;
  mod_aftertouch = 0;
  mod_wheel = 0;
  matrix_pitch_mod_q24 = 0;
}

void mod_matrix_set_source(uint8_t slot, int16_t v) {
  if (slot >= MOD_SLOT_COUNT) return;
  if (v < 0 || v >= (int16_t)MOD_SRC_COUNT) {
    g_mod_slots[slot].source = MOD_SRC_EMPTY;
  } else {
    g_mod_slots[slot].source = (uint8_t)v;
  }
}

void mod_matrix_set_dest(uint8_t slot, int16_t v) {
  if (slot >= MOD_SLOT_COUNT) return;
  if (v < 0 || v >= (int16_t)MOD_DEST_COUNT) {
    g_mod_slots[slot].dest = MOD_DEST_EMPTY;
  } else {
    g_mod_slots[slot].dest = (uint8_t)v;
  }
}

void mod_matrix_set_depth(uint8_t slot, int16_t v) {
  if (slot >= MOD_SLOT_COUNT) return;
  g_mod_slots[slot].depth = v;
}

void mod_matrix_on_note_on() {
  // ±1.0 Q15; random(0..2000)-1000 → scale to ±32768.
  mod_random_snh_q15 = (int16_t)(((int32_t)random(0, 2001) - 1000) * 32);
}

void mod_matrix_set_aftertouch(uint8_t pressure) {
  mod_aftertouch = pressure;
}

void mod_matrix_set_mod_wheel(uint8_t value) {
  mod_wheel = value;
}

// Source as Q15 (±32768 ≈ ±1.0). Mul/shift — no float, avoid hot /4095 on M0+.
static int32_t mod_matrix_read_source_q15(uint8_t src) {
  switch (src) {
    case MOD_SRC_ADSR3:
      // (level * 32768) / 4095 ≈ level << 3 (max error 8 Q15 LSBs at full scale).
      return (int32_t)ADSR1Level[0] << 3;
    case MOD_SRC_ADSR4:
    case MOD_SRC_LFO3:
    case MOD_SRC_LFO4:
      return 0;
    case MOD_SRC_VELOCITY:
      return (int32_t)midi_velocity[0] * 258;
    case MOD_SRC_KEYTRACK: {
      const uint8_t note = VOICE_NOTES[0];
      if (note == 0) return 0;
      // 32768 / 48 ≈ 682
      return mod_clamp_q15(((int32_t)note - 60) * 682);
    }
    case MOD_SRC_RANDOM:
      return (int32_t)mod_random_snh_q15;
    case MOD_SRC_AFTERTOUCH:
      return (int32_t)mod_aftertouch * 258;
    case MOD_SRC_LFO1:
      return (int32_t)(((int64_t)LFO1Level * (int64_t)MOD_LFO1_TO_Q15_MUL) >> 16);
    case MOD_SRC_LFO2:
      return (int32_t)(((int64_t)LFO2Level * (int64_t)MOD_LFO2_TO_Q15_MUL) >> 16);
    case MOD_SRC_PITCH_BEND:
      return ((int32_t)midi_pitch_bend - 8192) << 2;
    case MOD_SRC_MOD_WHEEL:
      return (int32_t)mod_wheel * 258;
    case MOD_SRC_NOISE0:
    case MOD_SRC_NOISE1:
    case MOD_SRC_NOISE2:
    case MOD_SRC_NOISE3: {
      const uint8_t i = (uint8_t)(src - MOD_SRC_NOISE0);
      if (i >= NUM_NOISE_GENS) return 0;
      // Already Q15 from noise engines.
      return (int32_t)noiseLevel[i];
    }
    default:
      return 0;
  }
}

void mod_matrix_accumulate(int32_t dest_sums[MOD_DEST_COUNT]) {
  memset(dest_sums, 0, sizeof(int32_t) * MOD_DEST_COUNT);

  for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
    const ModSlot& s = g_mod_slots[i];
    if (s.source == MOD_SRC_EMPTY || s.dest == MOD_DEST_EMPTY || s.depth == 0) continue;
    if (s.dest >= MOD_DEST_COUNT) continue;

    const int32_t src_q15 = mod_matrix_read_source_q15(s.source);
    dest_sums[s.dest] += (int32_t)(((int64_t)src_q15 * (int64_t)s.depth) >> 15);
  }
}

void mod_matrix_apply_cv(const int32_t dest_sums[MOD_DEST_COUNT], uint16_t* dist_drive_out,
                         uint16_t* dist_mix_out) {
#ifdef ENABLE_CV_OUTS
  write_level_pwm_raw(
    mod_clamp_u16((int32_t)OSC1Level - dest_sums[MOD_DEST_OSC1_LEVEL]),
    mod_clamp_u16((int32_t)OSC2Level - dest_sums[MOD_DEST_OSC2_LEVEL]),
    mod_clamp_u16((int32_t)OSC3Level - dest_sums[MOD_DEST_OSC3_LEVEL]),
    mod_clamp_u16((int32_t)SubLevel - dest_sums[MOD_DEST_SUB_LEVEL]));

  RESONANCE_PWM[0] = mod_clamp_u16((int32_t)RESONANCE + dest_sums[MOD_DEST_VCF1_RESO]);
  RESONANCE_PWM[1] = mod_clamp_u16((int32_t)RESONANCE + dest_sums[MOD_DEST_VCF2_RESO]);
#else
  (void)dest_sums;
#endif

  if (dist_drive_out) {
    *dist_drive_out = mod_clamp_u16((int32_t)DIST_DRIVE + dest_sums[MOD_DEST_DIST_DRIVE]);
  }
  if (dist_mix_out) {
    *dist_mix_out = mod_clamp_u16((int32_t)DIST_MIX + dest_sums[MOD_DEST_DIST_MIX]);
  }
}
