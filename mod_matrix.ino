#include "include_all.h"

ModSlot g_mod_slots[MOD_SLOT_COUNT];
volatile uint8_t mod_aftertouch = 0;
float mod_random_snh = 0.0f;

static constexpr int MOD_KEYTRACK_PIVOT = 60;

void mod_matrix_init() {
  for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
    g_mod_slots[i].source = MOD_SRC_EMPTY;
    g_mod_slots[i].dest = MOD_DEST_EMPTY;
    g_mod_slots[i].depth = 0;
  }
  mod_aftertouch = 0;
  mod_random_snh = 0.0f;
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
  // S&H: uniform in [-1, 1]
  mod_random_snh = ((float)random(0, 2001) - 1000.0f) * 0.001f;
}

void mod_matrix_set_aftertouch(uint8_t value) {
  mod_aftertouch = value;
}

static float mod_matrix_read_source(uint8_t src) {
  switch (src) {
    case MOD_SRC_ADSR3:
      // EnvDCO (wire-name ADSR3)
      return (float)ADSR1Level[0] * (1.0f / 4095.0f);
    case MOD_SRC_ADSR4:
    case MOD_SRC_LFO3:
    case MOD_SRC_LFO4:
      return 0.0f;  // stub until engines exist
    case MOD_SRC_VELOCITY:
      return (float)midi_velocity[0] * (1.0f / 127.0f);
    case MOD_SRC_KEYTRACK: {
      float kt = ((float)VOICE_NOTES[0] - (float)MOD_KEYTRACK_PIVOT) * (1.0f / 60.0f);
      if (kt < -1.0f) kt = -1.0f;
      if (kt > 1.0f) kt = 1.0f;
      return kt;
    }
    case MOD_SRC_RANDOM:
      return mod_random_snh;
    case MOD_SRC_AFTERTOUCH:
      return (float)mod_aftertouch * (1.0f / 127.0f);
    default:
      return 0.0f;
  }
}

void mod_matrix_accumulate(int32_t accum[MOD_DEST_COUNT]) {
  for (uint8_t d = 0; d < MOD_DEST_COUNT; d++) {
    accum[d] = 0;
  }
  for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
    const ModSlot& s = g_mod_slots[i];
    if (s.source == MOD_SRC_EMPTY || s.dest == MOD_DEST_EMPTY) continue;
    if (s.dest >= MOD_DEST_COUNT) continue;
    if (s.depth == 0) continue;
    float src = mod_matrix_read_source(s.source);
    accum[s.dest] += (int32_t)(src * (float)s.depth);
  }
}

static uint16_t mod_clamp_u16(int32_t v) {
  if (v < 0) return 0;
  if (v > 4095) return 4095;
  return (uint16_t)v;
}

uint16_t mod_matrix_apply_cv() {
  int32_t accum[MOD_DEST_COUNT];
  mod_matrix_accumulate(accum);

  // Levels: attenuator CV (high = muted). Positive depth + positive source → louder (lower CV).
  uint16_t o1 = mod_clamp_u16((int32_t)OSC1Level - accum[MOD_DEST_OSC1_LEVEL]);
  uint16_t o2 = mod_clamp_u16((int32_t)OSC2Level - accum[MOD_DEST_OSC2_LEVEL]);
  uint16_t o3 = mod_clamp_u16((int32_t)OSC3Level - accum[MOD_DEST_OSC3_LEVEL]);
  uint16_t sub = mod_clamp_u16((int32_t)SubLevel - accum[MOD_DEST_SUB_LEVEL]);

  RESONANCE_PWM[0] = mod_clamp_u16((int32_t)RESONANCE + accum[MOD_DEST_VCF1_RESO]);
  RESONANCE_PWM[1] = mod_clamp_u16((int32_t)RESONANCE + accum[MOD_DEST_VCF2_RESO]);

  uint16_t dist_out = mod_clamp_u16((int32_t)DIST_DRIVE + accum[MOD_DEST_DIST_DRIVE]);

#ifdef ENABLE_CV_OUTS
  write_level_pwm_raw(o1, o2, o3, sub);
#else
  (void)o1;
  (void)o2;
  (void)o3;
  (void)sub;
#endif
  return dist_out;
}
