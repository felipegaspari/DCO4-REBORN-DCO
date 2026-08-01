#ifndef __MOD_MATRIX_H__
#define __MOD_MATRIX_H__

#include <stdint.h>

// Sparse control-rate mod matrix. See docs/MOD_MATRIX.md.
// Never destinations: main VCA, VCF1 cutoff (fixed buses only).

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
  MOD_SRC_COUNT = 8
};

enum ModDest : uint8_t {
  MOD_DEST_OSC1_LEVEL = 0,
  MOD_DEST_OSC2_LEVEL = 1,
  MOD_DEST_OSC3_LEVEL = 2,
  MOD_DEST_SUB_LEVEL = 3,
  MOD_DEST_VCF1_RESO = 4,
  MOD_DEST_VCF2_RESO = 5,
  MOD_DEST_DIST_DRIVE = 6,
  MOD_DEST_COUNT = 7
};

struct ModSlot {
  uint8_t source;  // ModSource or MOD_SRC_EMPTY
  uint8_t dest;    // ModDest or MOD_DEST_EMPTY
  int16_t depth;   // bipolar; typically ±4095 full-scale contribution
};

extern ModSlot g_mod_slots[MOD_SLOT_COUNT];
extern volatile uint8_t mod_aftertouch;
extern float mod_random_snh;

void mod_matrix_init();
void mod_matrix_set_source(uint8_t slot, int16_t v);
void mod_matrix_set_dest(uint8_t slot, int16_t v);
void mod_matrix_set_depth(uint8_t slot, int16_t v);
void mod_matrix_on_note_on();
void mod_matrix_set_aftertouch(uint8_t value);

// Fill accum[MOD_DEST_COUNT] with summed source*depth (int CV units).
void mod_matrix_accumulate(int32_t accum[MOD_DEST_COUNT]);

// Apply matrix onto level / reso / dist outs (skips when manualCalibrationFlag).
// Writes level PWM when ENABLE_CV_OUTS; sets RESONANCE_PWM[] and returns Dist Drive out.
uint16_t mod_matrix_apply_cv();

#endif
