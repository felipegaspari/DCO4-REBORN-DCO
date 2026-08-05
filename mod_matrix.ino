#include "include_all.h"

static ModSlot g_mod_slots[MOD_SLOT_COUNT];
static float mod_random_snh = 0.0f;
static uint8_t mod_aftertouch = 0;
static uint8_t mod_wheel = 0;

static inline uint16_t mod_clamp_u16(int32_t v) {
  if (v < 0) return 0;
  if (v > 4095) return 4095;
  return (uint16_t)v;
}

void mod_matrix_init() {
  for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
    g_mod_slots[i].source = MOD_SRC_EMPTY;
    g_mod_slots[i].dest = MOD_DEST_EMPTY;
    g_mod_slots[i].depth = 0;
  }
  mod_random_snh = 0.0f;
  mod_aftertouch = 0;
  mod_wheel = 0;
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
  mod_random_snh = ((float)random(0, 2001) - 1000.0f) * 0.001f;
}

void mod_matrix_set_aftertouch(uint8_t pressure) {
  mod_aftertouch = pressure;
}

void mod_matrix_set_mod_wheel(uint8_t value) {
  mod_wheel = value;
}

static float mod_matrix_read_source(uint8_t src) {
  switch (src) {
    case MOD_SRC_ADSR3:
      return (float)ADSR1Level[0] / 4095.0f;
    case MOD_SRC_ADSR4:
    case MOD_SRC_LFO3:
    case MOD_SRC_LFO4:
      return 0.0f;
    case MOD_SRC_VELOCITY:
      return (float)midi_velocity[0] / 127.0f;
    case MOD_SRC_KEYTRACK: {
      const uint8_t note = VOICE_NOTES[0];
      if (note == 0) return 0.0f;
      float kt = ((float)note - 60.0f) / 48.0f;
      if (kt < -1.0f) return -1.0f;
      if (kt > 1.0f) return 1.0f;
      return kt;
    }
    case MOD_SRC_RANDOM:
      return mod_random_snh;
    case MOD_SRC_AFTERTOUCH:
      return (float)mod_aftertouch / 127.0f;
    case MOD_SRC_LFO1:
      return (float)LFO1Level / (float)LFO1_CC_HALF;
    case MOD_SRC_LFO2:
      return (float)LFO2Level / (float)LFO2_CC_HALF;
    case MOD_SRC_PITCH_BEND:
      return ((float)midi_pitch_bend - 8192.0f) / 8192.0f;
    case MOD_SRC_MOD_WHEEL:
      return (float)mod_wheel / 127.0f;
    default:
      return 0.0f;
  }
}

void mod_matrix_accumulate(int32_t dest_sums[MOD_DEST_COUNT]) {
  for (uint8_t d = 0; d < MOD_DEST_COUNT; d++) {
    dest_sums[d] = 0;
  }

  for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
    const ModSlot& s = g_mod_slots[i];
    if (s.source == MOD_SRC_EMPTY || s.dest == MOD_DEST_EMPTY) continue;
    if (s.depth == 0) continue;
    if (s.dest >= MOD_DEST_COUNT) continue;

    const float src_norm = mod_matrix_read_source(s.source);
    dest_sums[s.dest] += (int32_t)(src_norm * (float)s.depth);
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
