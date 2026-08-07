#ifndef DCO_MOD_MATRIX_H
#define DCO_MOD_MATRIX_H

#include <stdint.h>

// Sparse mod matrix (8 slots). See docs/MOD_MATRIX.md.

static constexpr uint8_t MOD_SLOT_COUNT = 8;
static constexpr uint8_t MOD_SRC_EMPTY = 0xFF;
static constexpr uint8_t MOD_DEST_EMPTY = 0xFF;

enum ModSource : uint8_t {
  MOD_SRC_ADSR3 = 0,
  MOD_SRC_ADSR4 = 1,
  MOD_SRC_LFO3 = 2,
  MOD_SRC_LFO4 = 3,
  MOD_SRC_VELOCITY = 4,
  MOD_SRC_KEYTRACK = 5,
  MOD_SRC_RANDOM = 6,
  MOD_SRC_AFTERTOUCH = 7,
  MOD_SRC_LFO1 = 8,
  MOD_SRC_LFO2 = 9,
  MOD_SRC_PITCH_BEND = 10,
  MOD_SRC_MOD_WHEEL = 11,
  MOD_SRC_NOISE0 = 12,
  MOD_SRC_NOISE1 = 13,
  // Reserved: fleet is two gens (noise0/1). IDs kept for panel/protocol stability → read as 0.
  MOD_SRC_NOISE2 = 14,
  MOD_SRC_NOISE3 = 15,
  MOD_SRC_COUNT = 16
};

enum ModDest : uint8_t {
  MOD_DEST_OSC1_LEVEL = 0,
  MOD_DEST_OSC2_LEVEL = 1,
  MOD_DEST_OSC3_LEVEL = 2,
  MOD_DEST_SUB_LEVEL = 3,
  MOD_DEST_VCF1_RESO = 4,
  MOD_DEST_VCF2_RESO = 5,
  MOD_DEST_DIST_DRIVE = 6,
  MOD_DEST_VCF_CUTOFF = 7,
  MOD_DEST_DIST_MIX = 8,
  MOD_DEST_PITCH = 9,
  MOD_DEST_COUNT = 10
};

// Shared pitch dest: ±1023 depth → ±1.0 octave (Q24 octave-fraction). Latched ~10 kHz.
extern volatile int32_t matrix_pitch_mod_q24;
static constexpr int32_t MOD_PITCH_DEPTH_FULL = 1023;

struct ModSlot {
  uint8_t source;
  uint8_t dest;
  int16_t depth;
};

void mod_matrix_init();
void mod_matrix_set_source(uint8_t slot, int16_t v);
void mod_matrix_set_dest(uint8_t slot, int16_t v);
void mod_matrix_set_depth(uint8_t slot, int16_t v);
void mod_matrix_on_note_on();
void mod_matrix_set_aftertouch(uint8_t pressure);
void mod_matrix_set_mod_wheel(uint8_t value);

// Sum active slots into dest_sums[0..MOD_DEST_COUNT-1].
void mod_matrix_accumulate(int32_t dest_sums[MOD_DEST_COUNT]);

// Apply level/reso/dist PWM from dest_sums; returns panel + matrix dist outputs.
void mod_matrix_apply_cv(const int32_t dest_sums[MOD_DEST_COUNT], uint16_t* dist_drive_out,
                         uint16_t* dist_mix_out);

#endif
