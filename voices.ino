#include "include_all.h"

// Enable/disable detailed DCO debug report (including OSC1 frequency stages)
#define DCO_DEBUG_REPORT 0


#ifdef CLKDIV_BENCHMARK
// Float vs double comparison for the clock-divider math. Both candidates run on the same
// inputs every frame, so this roughly triples the cost of that stage — bench flag only.
// What is compared is the divider that actually reaches the state machine (post
// pio_clk_div_for_y), not the intermediate cycle count, plus the frequency each one
// reconstructs through the PIO period model.
uint32_t clkdiv_bench_count = 0;
double clkdiv_time_float_sum_us = 0.0;
double clkdiv_time_double_sum_us = 0.0;
int32_t clkdiv_delta1_sum = 0;
int32_t clkdiv_delta2_sum = 0;
uint32_t clkdiv_delta1_max = 0;
uint32_t clkdiv_delta2_max = 0;
double clkdiv_freq1_diff_sum = 0.0;
double clkdiv_freq2_diff_sum = 0.0;
double clkdiv_freq1_diff_max_abs = 0.0;
double clkdiv_freq2_diff_max_abs = 0.0;
double clkdiv_last_target1_Hz = 0.0;
double clkdiv_last_out1_float_Hz = 0.0;
double clkdiv_last_out1_double_Hz = 0.0;
double clkdiv_last_target2_Hz = 0.0;
double clkdiv_last_out2_float_Hz = 0.0;
double clkdiv_last_out2_double_Hz = 0.0;
#endif

// Run both candidate divider implementations on this frame's OSC1/OSC2 frequencies and
// accumulate the timing and accuracy difference. Compiles away entirely without
// CLKDIV_BENCHMARK. Called from voice_task_float() just before the live divider math.
static inline void clkdiv_bench_sample(float freqA_Hz, float freqB_Hz) {
#ifdef CLKDIV_BENCHMARK
  if (!(freqA_Hz > 0.0f) || !(freqB_Hz > 0.0f)) return;

  const uint32_t wA = osc_ramp_weight(0), kA = osc_period_overhead(0), yA = osc_last_y[0];
  const uint32_t wB = osc_ramp_weight(1), kB = osc_period_overhead(1), yB = osc_last_y[1];

  const uint32_t t0 = bench_now();
  const uint32_t cyc_f1 = (uint32_t)fminf((float)sysClock_Hz / freqA_Hz + 0.5f, 4.0e9f);
  const uint32_t cyc_f2 = (uint32_t)fminf((float)sysClock_Hz / freqB_Hz + 0.5f, 4.0e9f);
  const uint32_t t1 = bench_now();
  const uint32_t cyc_d1 = (uint32_t)fmin((double)sysClock_Hz / (double)freqA_Hz + 0.5, 4.0e9);
  const uint32_t cyc_d2 = (uint32_t)fmin((double)sysClock_Hz / (double)freqB_Hz + 0.5, 4.0e9);
  const uint32_t t2 = bench_now();

  const double us_per_cycle = 1.0 / (double)bench_cycles_per_us;
  clkdiv_time_float_sum_us += (double)bench_span(t0, t1) * us_per_cycle;
  clkdiv_time_double_sum_us += (double)bench_span(t1, t2) * us_per_cycle;

  // Compare the divider that actually reaches the state machine, not the intermediate
  // cycle count: rounding inside pio_clk_div_for_y can absorb a difference, or create one.
  const uint32_t div_f1 = pio_clk_div_for_y(cyc_f1, yA, wA, kA);
  const uint32_t div_d1 = pio_clk_div_for_y(cyc_d1, yA, wA, kA);
  const uint32_t div_f2 = pio_clk_div_for_y(cyc_f2, yB, wB, kB);
  const uint32_t div_d2 = pio_clk_div_for_y(cyc_d2, yB, wB, kB);

  const int32_t d1 = (int32_t)div_f1 - (int32_t)div_d1;
  const int32_t d2 = (int32_t)div_f2 - (int32_t)div_d2;
  clkdiv_delta1_sum += d1;
  clkdiv_delta2_sum += d2;
  const uint32_t abs1 = (uint32_t)((d1 < 0) ? -d1 : d1);
  const uint32_t abs2 = (uint32_t)((d2 < 0) ? -d2 : d2);
  if (abs1 > clkdiv_delta1_max) clkdiv_delta1_max = abs1;
  if (abs2 > clkdiv_delta2_max) clkdiv_delta2_max = abs2;

  // What each divider is actually worth in Hz, through the same period model the PIO uses.
  const double out_f1 = (double)sysClock_Hz / (double)(yA + (uint64_t)div_f1 * wA + kA);
  const double out_d1 = (double)sysClock_Hz / (double)(yA + (uint64_t)div_d1 * wA + kA);
  const double out_f2 = (double)sysClock_Hz / (double)(yB + (uint64_t)div_f2 * wB + kB);
  const double out_d2 = (double)sysClock_Hz / (double)(yB + (uint64_t)div_d2 * wB + kB);

  const double fd1 = out_f1 - out_d1;
  const double fd2 = out_f2 - out_d2;
  clkdiv_freq1_diff_sum += fd1;
  clkdiv_freq2_diff_sum += fd2;
  if (fabs(fd1) > clkdiv_freq1_diff_max_abs) clkdiv_freq1_diff_max_abs = fabs(fd1);
  if (fabs(fd2) > clkdiv_freq2_diff_max_abs) clkdiv_freq2_diff_max_abs = fabs(fd2);

  clkdiv_last_target1_Hz = (double)freqA_Hz;
  clkdiv_last_out1_float_Hz = out_f1;
  clkdiv_last_out1_double_Hz = out_d1;
  clkdiv_last_target2_Hz = (double)freqB_Hz;
  clkdiv_last_out2_float_Hz = out_f2;
  clkdiv_last_out2_double_Hz = out_d2;

  clkdiv_bench_count++;
#else
  (void)freqA_Hz;
  (void)freqB_Hz;
#endif
}

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

// Inverse of noteQ16_to_freqQ24: Q24 Hz → Q16 note via binary search + linear frac.
static inline int32_t freqQ24_to_noteQ16(int64_t freq_q24) {
  const size_t NOTE_TABLE_LEN = sizeof(sNotePitches_q24) / sizeof(sNotePitches_q24[0]);
  if (NOTE_TABLE_LEN == 0) return 0;
  if (NOTE_TABLE_LEN == 1) return 0;
  if (freq_q24 <= sNotePitches_q24[0]) return 0;
  if (freq_q24 >= sNotePitches_q24[NOTE_TABLE_LEN - 1]) {
    return ((int32_t)(NOTE_TABLE_LEN - 1)) << 16;
  }

  size_t lo = 0;
  size_t hi = NOTE_TABLE_LEN - 1;
  while (hi - lo > 1) {
    size_t mid = lo + ((hi - lo) >> 1);
    if (sNotePitches_q24[mid] <= freq_q24) {
      lo = mid;
    } else {
      hi = mid;
    }
  }

  int64_t f0 = sNotePitches_q24[lo];
  int64_t f1 = sNotePitches_q24[hi];
  int64_t df = f1 - f0;
  if (df <= 0) return ((int32_t)lo) << 16;

  int64_t num = (freq_q24 - f0) << 16;
  int32_t frac = (int32_t)(num / df);
  if (frac < 0) frac = 0;
  if (frac > 0xFFFF) frac = 0xFFFF;
  return (((int32_t)lo) << 16) + frac;
}

// Helper: convert float Hz to Q24 fixed-point (Hz * 2^24)
static inline int64_t float_to_q24(float f) {
  return (int64_t)lrintf(f * (float)(1 << 24));
}

// midi/base + offset → table index using signed math (no uint8 wrap-to-top).
// High notes still fold down by octaves to stay within highestNote, then clamp.
static inline uint8_t midi_offset_to_table_index(int midi_or_base, int offset, size_t table_len) {
  int n = midi_or_base - 36 + offset;
  if (n < 0) n = 0;
  while (n > (int)highestNote) n -= 12;
  if (table_len == 0) return 0;
  if (n >= (int)table_len) n = (int)table_len - 1;
  return (uint8_t)n;
}

// Resolve porta start note (Q16). First edge snaps; later edges use cur note (incl. index 0).
static inline int32_t porta_resolve_start_note_q16(uint8_t osc, int32_t target_q16) {
  if (porta_note_valid[osc]) {
    return porta_note_cur_q16[osc];
  }
  if (portamento_cur_freq_q24[osc] > 0) {
    porta_note_valid[osc] = true;
    return freqQ24_to_noteQ16(portamento_cur_freq_q24[osc]);
  }
  porta_note_valid[osc] = true;
  return target_q16;
}

// Common endpoint latch for note-space porta setup.
static inline void porta_latch_endpoints_q16(uint8_t osc, int32_t start_q16, int32_t target_q16) {
  porta_note_start_q16[osc] = start_q16;
  porta_note_stop_q16[osc] = target_q16;
  porta_note_cur_q16[osc] = start_q16;
  porta_note_valid[osc] = true;
  int64_t freq_q24 = noteQ16_to_freqQ24(start_q16);
  portamento_start_q24[osc] = freq_q24;
  portamento_stop_q24[osc] = noteQ16_to_freqQ24(target_q16);
  portamento_cur_freq_q24[osc] = freq_q24;
}

// TIME: fixed duration T_fixed µs for any interval; linear in semitones.
static inline void porta_setup_time_q16(uint8_t osc, int32_t start_q16, int32_t target_q16,
                                       int32_t T_fixed) {
  if (T_fixed < 1) T_fixed = 1;
  porta_latch_endpoints_q16(osc, start_q16, target_q16);

  int32_t dNote_q16 = target_q16 - start_q16;
  int64_t halfT = (int64_t)T_fixed >> 1;
  int64_t num = (dNote_q16 >= 0) ? ((int64_t)dNote_q16 + halfT) : ((int64_t)dNote_q16 - halfT);
  porta_note_step_q16[osc] = (int32_t)(num / (int64_t)T_fixed);
  if (dNote_q16 != 0 && porta_note_step_q16[osc] == 0) {
    porta_note_step_q16[osc] = (dNote_q16 > 0) ? 1 : -1;
  }
}

// SLEW: constant rate = 12 semitones / T_slew (one octave takes the slew-time knob).
static inline void porta_setup_slew_q16(uint8_t osc, int32_t start_q16, int32_t target_q16,
                                       int32_t T_slew) {
  if (T_slew < 1) T_slew = 1;
  porta_latch_endpoints_q16(osc, start_q16, target_q16);

  int32_t dNote_q16 = target_q16 - start_q16;
  if (dNote_q16 == 0) {
    porta_note_step_q16[osc] = 0;
  } else {
    int32_t rate = (int32_t)(((int64_t)12 << 16) / (int64_t)T_slew);
    if (rate == 0) rate = 1;
    porta_note_step_q16[osc] = (dNote_q16 > 0) ? rate : -rate;
  }
}

static inline void porta_setup_glide_q16(uint8_t osc, int32_t start_q16, int32_t target_q16,
                                        uint8_t mode) {
  if (mode == PORTA_MODE_TIME) {
    int32_t T = (portamento_time_fixed == 0) ? 1 : (int32_t)portamento_time_fixed;
    porta_setup_time_q16(osc, start_q16, target_q16, T);
  } else {
    int32_t T = (portamento_time_slew == 0) ? 1 : (int32_t)portamento_time_slew;
    porta_setup_slew_q16(osc, start_q16, target_q16, T);
  }
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

// Inverse of noteIndex_to_freqFloat: Hz → semitone index via binary search + linear frac.
static inline float freqFloat_to_noteIndex(float hz) {
  const size_t LEN = sizeof(sNotePitches) / sizeof(sNotePitches[0]);
  if (LEN == 0) return 0.0f;
  if (LEN == 1) return 0.0f;
  if (hz <= sNotePitches[0]) return 0.0f;
  if (hz >= sNotePitches[LEN - 1]) return (float)(LEN - 1);

  size_t lo = 0;
  size_t hi = LEN - 1;
  while (hi - lo > 1) {
    size_t mid = lo + ((hi - lo) >> 1);
    if (sNotePitches[mid] <= hz) {
      lo = mid;
    } else {
      hi = mid;
    }
  }

  float f0 = sNotePitches[lo];
  float f1 = sNotePitches[hi];
  float df = f1 - f0;
  if (df <= 0.0f) return (float)lo;
  return (float)lo + (hz - f0) / df;
}

static inline float porta_resolve_start_note_f(uint8_t osc, float target) {
  if (porta_note_valid[osc]) {
    return porta_note_cur_f[osc];
  }
  if (porta_freq_cur_f[osc] > 0.0f) {
    porta_note_valid[osc] = true;
    return freqFloat_to_noteIndex(porta_freq_cur_f[osc]);
  }
  porta_note_valid[osc] = true;
  return target;
}

static inline void porta_latch_endpoints_f(uint8_t osc, float startNote, float targetNote) {
  porta_note_start_f[osc] = startNote;
  porta_note_stop_f[osc] = targetNote;
  porta_note_cur_f[osc] = startNote;
  porta_note_valid[osc] = true;
  float startHz = noteIndex_to_freqFloat(startNote);
  float stopHz = noteIndex_to_freqFloat(targetNote);
  porta_freq_start_f[osc] = startHz;
  porta_freq_stop_f[osc] = stopHz;
  porta_freq_cur_f[osc] = startHz;
}

// TIME: fixed duration T_fixed µs for any interval; linear in semitones.
static inline void porta_setup_time_f(uint8_t osc, float startNote, float targetNote, float T_fixed) {
  if (T_fixed < 1.0f) T_fixed = 1.0f;
  porta_latch_endpoints_f(osc, startNote, targetNote);

  float dNote = targetNote - startNote;
  const float SCALE = 65536.0f;
  float halfT = 0.5f * T_fixed;
  float d_q16 = dNote * SCALE;
  float num = (d_q16 >= 0.0f) ? (d_q16 + halfT) : (d_q16 - halfT);
  float stepNote = (num / T_fixed) / SCALE;
  if (dNote != 0.0f && stepNote == 0.0f) {
    stepNote = (dNote > 0.0f) ? (1.0f / SCALE) : (-1.0f / SCALE);
  }
  porta_note_step_f[osc] = stepNote;
}

// SLEW: constant rate = 12 semitones / T_slew (one octave takes the slew-time knob).
static inline void porta_setup_slew_f(uint8_t osc, float startNote, float targetNote, float T_slew) {
  if (T_slew < 1.0f) T_slew = 1.0f;
  porta_latch_endpoints_f(osc, startNote, targetNote);

  float dNote = targetNote - startNote;
  if (dNote == 0.0f) {
    porta_note_step_f[osc] = 0.0f;
  } else {
    float rate = 12.0f / T_slew;
    if (rate == 0.0f) rate = 1.0f / 65536.0f;
    porta_note_step_f[osc] = (dNote > 0.0f) ? rate : -rate;
  }
}

static inline void porta_setup_glide_f(uint8_t osc, float startNote, float targetNote, uint8_t mode) {
  if (mode == PORTA_MODE_TIME) {
    float T = (portamento_time_fixed == 0) ? 1.0f : (float)portamento_time_fixed;
    porta_setup_time_f(osc, startNote, targetNote, T);
  } else {
    float T = (portamento_time_slew == 0) ? 1.0f : (float)portamento_time_slew;
    porta_setup_slew_f(osc, startNote, targetNote, T);
  }
}

// Q24 → float modifier/Hz scale (multiply avoids per-sample divide by 2^24).
static constexpr float Q24_TO_FLOAT = 1.0f / 16777216.0f;

static inline float q24_to_float(int32_t q) {
  return (float)q * Q24_TO_FLOAT;
}
#endif

#ifndef USE_FLOAT_VOICE_TASK
// Fixed-point realtime voice engine (portamento, modifiers, clkdiv, amp, PIO/PWM/PW).
// Selected by voice_task_main() when USE_FLOAT_VOICE_TASK is not defined.
inline void voice_task_fixed_point() {
  // Track portamento-time and mode changes between calls so we can smoothly
  // retime the glide without introducing pitch discontinuities.
  static uint32_t last_portamento_time = 0;
  static uint8_t last_portamento_mode = PORTA_MODE_TIME;
  uint32_t portaTime = portamento_time;
  uint8_t portaMode = portamento_mode;
  bool portaTimeChanged = (portaTime != last_portamento_time);
  bool portaModeChanged = (portaMode != last_portamento_mode);

  // Pre-calculate pitch bend as a Q24 value. This is done once per voice_task_fixed_point call.
  int32_t calcPitchbend_q24;

  BENCH_BEGIN(vt_pitchbend);
  // Optimized: Perform pitch bend calculation entirely in fixed-point Q24.
  // ((bend / 8192.0) - 1.0) * pitchBendMultiplier
  // This avoids float conversions and multiplications in the hot path.
  int32_t bend_normalized_q24 = ((int32_t)midi_pitch_bend << 11) - (1 << 24);
  calcPitchbend_q24 = (int32_t)(((int64_t)bend_normalized_q24 * pitchBendMultiplier_q24) >> 24);
  BENCH_END(vt_pitchbend);

  last_midi_pitch_bend = midi_pitch_bend;

  // Hoist volatile PW depth once per task (ADSR uses precomputed ADSR1toPWM_scale).
  const int16_t local_LFO2toPW = LFO2toPW;

  // Latch note-on edges for every voice slot (para may set flags on 1/2).
  for (int k = 0; k < NUM_VOICES; k++) {
    if (note_on_flag[k] == 1) {
      note_on_flag_flag[k] = true;
      note_on_flag[k] = 0;
    }
  }

  for (int i = 0; i < NUM_VOICES_VOICE_TASK; i++) {

#if DCO_DEBUG_REPORT
    // Debug: track OSC1 frequency at key stages of the pipeline for DCO report.
    float dbg_freq_base_Hz = 0.0f;       // After portamento, before modifiers
    float dbg_freq_after_mod_Hz = 0.0f;  // After all modifiers applied (freq_q24_A)
#endif

    // note_off keeps VOICE_NOTES (release pitch); free/steal uses VOICES[].
    // Never-played slots stay 0 — gate midi→table math (uint8 underflow → shriek).

    // Note index convention: table_index = midi - 36 + offset (36 ⇒ unison).
    //   Mono: octave_shift = global octave; OSC2/3 = relative to note1 (36 ⇒ unison with OSC1).
    //   Poly/stack: every osc uses octave_shift only (shared octave); not OSC2/3.
    const size_t NOTE_TABLE_LEN = sizeof(sNotePitches_q24) / sizeof(sNotePitches_q24[0]);
    uint8_t note1, note2, note3;
    if (voiceMode == 0) {
      const uint8_t vn = VOICE_NOTES[i];
      if (vn == 0) {
        note1 = note2 = note3 = 0;
      } else {
        note1 = midi_offset_to_table_index((int)vn, (int)octave_shift, NOTE_TABLE_LEN);
        note2 = midi_offset_to_table_index((int)note1, (int)OSC2_interval, NOTE_TABLE_LEN);
        note3 = midi_offset_to_table_index((int)note1, (int)OSC3_interval, NOTE_TABLE_LEN);
      }
    } else {
      const uint8_t vn0 = VOICE_NOTES[0];
      const uint8_t vn1 = VOICE_NOTES[1];
      const uint8_t vn2 = VOICE_NOTES[2];
      note1 = (vn0 == 0) ? 0 : midi_offset_to_table_index((int)vn0, (int)octave_shift, NOTE_TABLE_LEN);
      note2 = (vn1 == 0) ? 0 : midi_offset_to_table_index((int)vn1, (int)octave_shift, NOTE_TABLE_LEN);
      note3 = (vn2 == 0) ? 0 : midi_offset_to_table_index((int)vn2, (int)octave_shift, NOTE_TABLE_LEN);
    }

      BENCH_BEGIN(vt_osc_detune);
      // Optimized: Calculate OSC2/OSC3 detune in Q24 and keep it there.
      // The float conversion has been removed as it is no longer needed.
      // detune = 1.0 + 0.0002 * (256 - val)
      static constexpr int32_t DETUNE_SCALE_Q24 = (int32_t)(0.0002f * (float)(1 << 24) + 0.5f);
      int32_t detune_steps = ((int)256 - OSC2DetuneVal);
      int32_t detune_q24 = (1 << 24) + (detune_steps * DETUNE_SCALE_Q24);
      int32_t detune3_steps = ((int)256 - OSC3DetuneVal);
      int32_t detune3_q24 = (1 << 24) + (detune3_steps * DETUNE_SCALE_Q24);
      BENCH_END(vt_osc_detune);

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
      BENCH_BEGIN(vt_portamento);
      if (portaTime > 0 /*&& portamento_start != 0 && portamento_stop != 0*/) {
        uint32_t now_us = micros();
        if (voiceMode == 0) {
          portamentoTimer[i] = now_us - portamentoStartMicros[i];
        } else {
          for (int k = 0; k < NUM_VOICES; k++) {
            portamentoTimer[k] = now_us - portamentoStartMicros[k];
          }
        }

        // Mono: one edge restarts all three oscs. Para: each note_on_flag_flag[k] restarts osc k only.
        // TIME = fixed duration any interval; SLEW = constant semitone rate (time ∝ interval).
        if (voiceMode == 0) {
        if (note_on_flag_flag[i]) {
          portamentoStartMicros[i] = now_us;
          portamentoTimer[i] = 0;

          int32_t targetNoteA_q16 = ((int32_t)note1) << 16;
          int32_t targetNoteB_q16 = ((int32_t)note2) << 16;
          int32_t targetNoteC_q16 = ((int32_t)note3) << 16;
          porta_setup_glide_q16(DCO_A, porta_resolve_start_note_q16(DCO_A, targetNoteA_q16),
                                targetNoteA_q16, portaMode);
          porta_setup_glide_q16(DCO_B, porta_resolve_start_note_q16(DCO_B, targetNoteB_q16),
                                targetNoteB_q16, portaMode);
          porta_setup_glide_q16(DCO_C, porta_resolve_start_note_q16(DCO_C, targetNoteC_q16),
                                targetNoteC_q16, portaMode);
        }
        } else {
          // Paraphonic: restart porta per voice slot → osc k (1:1).
          const uint8_t targetNotes[3] = { note1, note2, note3 };
          for (int k = 0; k < NUM_VOICES; k++) {
            if (!note_on_flag_flag[k]) continue;
            const uint8_t osc = (uint8_t)k;
            portamentoStartMicros[k] = now_us;
            portamentoTimer[k] = 0;
            int32_t target_q16 = ((int32_t)targetNotes[k]) << 16;
            porta_setup_glide_q16(osc, porta_resolve_start_note_q16(osc, target_q16),
                                  target_q16, portaMode);
          }
        }

        // Control-change: skip steady (stale start/step + huge elapsed → pitch jump).
        const bool portaNoteOnEdge =
            (voiceMode == 0) ? note_on_flag_flag[i]
                             : (note_on_flag_flag[0] || note_on_flag_flag[1] || note_on_flag_flag[2]);
        const bool portaDoRetime =
            (portaTimeChanged || portaModeChanged) && !portaNoteOnEdge;

        int64_t curA;
        int64_t curB;
        int64_t curC;

        if (portaDoRetime) {
          portamentoStartMicros[i] = now_us;
          portamentoTimer[i] = 0;
          if (voiceMode != 0) {
            for (int k = 0; k < NUM_VOICES; k++) {
              portamentoStartMicros[k] = now_us;
              portamentoTimer[k] = 0;
            }
          }

          int32_t targetNoteA_q16 = ((int32_t)note1) << 16;
          int32_t targetNoteB_q16 = ((int32_t)note2) << 16;
          int32_t targetNoteC_q16 = ((int32_t)note3) << 16;
          // Retime from true current Hz so a mid-glide CC change stays continuous.
          int32_t curNoteA_q16 = freqQ24_to_noteQ16(portamento_cur_freq_q24[DCO_A]);
          int32_t curNoteB_q16 = freqQ24_to_noteQ16(portamento_cur_freq_q24[DCO_B]);
          int32_t curNoteC_q16 = freqQ24_to_noteQ16(portamento_cur_freq_q24[DCO_C]);
          porta_setup_glide_q16(DCO_A, curNoteA_q16, targetNoteA_q16, portaMode);
          porta_setup_glide_q16(DCO_B, curNoteB_q16, targetNoteB_q16, portaMode);
          porta_setup_glide_q16(DCO_C, curNoteC_q16, targetNoteC_q16, portaMode);
          curA = portamento_cur_freq_q24[DCO_A];
          curB = portamento_cur_freq_q24[DCO_B];
          curC = portamento_cur_freq_q24[DCO_C];
        } else {
          int32_t elapsedA = (voiceMode == 0) ? (int32_t)portamentoTimer[i] : (int32_t)portamentoTimer[0];
          int32_t elapsedB = (voiceMode == 0) ? elapsedA : (int32_t)portamentoTimer[1];
          int32_t elapsedC = (voiceMode == 0) ? elapsedA : (int32_t)portamentoTimer[2];

          int32_t dNoteA_q16 = porta_note_stop_q16[DCO_A] - porta_note_start_q16[DCO_A];
          int32_t dNoteB_q16 = porta_note_stop_q16[DCO_B] - porta_note_start_q16[DCO_B];
          int32_t dNoteC_q16 = porta_note_stop_q16[DCO_C] - porta_note_start_q16[DCO_C];

          int64_t curNoteA_q16 = (int64_t)porta_note_start_q16[DCO_A] + (int64_t)porta_note_step_q16[DCO_A] * (int64_t)elapsedA;
          int64_t curNoteB_q16 = (int64_t)porta_note_start_q16[DCO_B] + (int64_t)porta_note_step_q16[DCO_B] * (int64_t)elapsedB;
          int64_t curNoteC_q16 = (int64_t)porta_note_start_q16[DCO_C] + (int64_t)porta_note_step_q16[DCO_C] * (int64_t)elapsedC;

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

          // At stop: use stop Hz (never live note1 — that raced with note_on_flag).
          if (curNoteA_q16 == (int64_t)porta_note_stop_q16[DCO_A]) {
            curA = portamento_stop_q24[DCO_A];
          } else {
            curA = noteQ16_to_freqQ24(porta_note_cur_q16[DCO_A]);
          }
          if (curNoteB_q16 == (int64_t)porta_note_stop_q16[DCO_B]) {
            curB = portamento_stop_q24[DCO_B];
          } else {
            curB = noteQ16_to_freqQ24(porta_note_cur_q16[DCO_B]);
          }
          if (curNoteC_q16 == (int64_t)porta_note_stop_q16[DCO_C]) {
            curC = portamento_stop_q24[DCO_C];
          } else {
            curC = noteQ16_to_freqQ24(porta_note_cur_q16[DCO_C]);
          }
        }

        portamento_cur_freq_q24[DCO_A] = curA;
        portamento_cur_freq_q24[DCO_B] = curB;
        portamento_cur_freq_q24[DCO_C] = curC;
      } else {
        portamento_cur_freq_q24[DCO_A] = sNotePitches_q24[note1];
        portamento_start_q24[DCO_A] = portamento_cur_freq_q24[DCO_A];
        portamento_stop_q24[DCO_A] = portamento_cur_freq_q24[DCO_A];
        porta_note_cur_q16[DCO_A] = ((int32_t)note1) << 16;
        porta_note_stop_q16[DCO_A] = porta_note_cur_q16[DCO_A];
        porta_note_valid[DCO_A] = true;

        portamento_cur_freq_q24[DCO_B] = sNotePitches_q24[note2];
        portamento_start_q24[DCO_B] = portamento_cur_freq_q24[DCO_B];
        portamento_stop_q24[DCO_B] = portamento_cur_freq_q24[DCO_B];
        porta_note_cur_q16[DCO_B] = ((int32_t)note2) << 16;
        porta_note_stop_q16[DCO_B] = porta_note_cur_q16[DCO_B];
        porta_note_valid[DCO_B] = true;

        portamento_cur_freq_q24[DCO_C] = sNotePitches_q24[note3];
        portamento_start_q24[DCO_C] = portamento_cur_freq_q24[DCO_C];
        portamento_stop_q24[DCO_C] = portamento_cur_freq_q24[DCO_C];
        porta_note_cur_q16[DCO_C] = ((int32_t)note3) << 16;
        porta_note_stop_q16[DCO_C] = porta_note_cur_q16[DCO_C];
        porta_note_valid[DCO_C] = true;
      }

      // Exclusive path tag for jitter attribution (note_on > retime > steady > off).
      if (portaTime == 0) {
        BENCH_PATH_INC(porta_off);
      } else if (note_on_flag_flag[i]) {
        BENCH_PATH_INC(porta_note_on);
      } else if (portaTimeChanged || portaModeChanged) {
        BENCH_PATH_INC(porta_retime);
      } else if (portaMode == PORTA_MODE_TIME) {
        BENCH_PATH_INC(porta_steady_time);
      } else {
        BENCH_PATH_INC(porta_steady_slew);
      }

#if DCO_DEBUG_REPORT
      // Debug: OSC1 base frequency after portamento, before modifiers (in Hz)
      dbg_freq_base_Hz = (float)portamento_cur_freq_q24[DCO_A] / (float)(1 << 24);
#endif
      BENCH_END(vt_portamento);
      ////***********************    PORTAMENTO CODE  END    ****************************************/////

      BENCH_BEGIN(vt_adsr_mod);
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
      BENCH_END(vt_adsr_mod);

      BENCH_BEGIN(vt_unison_mod);
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
      BENCH_END(vt_unison_mod);

      BENCH_BEGIN(vt_drift_mod);
      // Drift: (LFO_DRIFT_LEVEL_q15 * scale) >> 15; scale preserves legacy CC-peak travel.
      static constexpr int32_t DRIFT_UNIT_Q24 = (int32_t)(0.0000005f * (float)(1 << 24) + 0.5f);
      const int32_t driftScale_q24 =
        (int32_t)((int32_t)analogDrift * DRIFT_UNIT_Q24 * (int32_t)LFO_DRIFT_CC_HALF);
      int64_t DETUNE_DRIFT_OSC1_q24 =
        (analogDrift != 0) ? (((int64_t)LFO_DRIFT_LEVEL[DCO_A] * (int64_t)driftScale_q24) >> 15) : 0;
      int64_t DETUNE_DRIFT_OSC2_q24 =
        (analogDrift != 0) ? (((int64_t)LFO_DRIFT_LEVEL[DCO_B] * (int64_t)driftScale_q24) >> 15) : 0;
      int64_t DETUNE_DRIFT_OSC3_q24 =
        (analogDrift != 0) ? (((int64_t)LFO_DRIFT_LEVEL[DCO_C] * (int64_t)driftScale_q24) >> 15) : 0;
      BENCH_END(vt_drift_mod);

      BENCH_BEGIN(vt_modifiers);
      // Combine modifiers in Q24 (faithful to original float path)
      // Unison is applied per-osc below; shared part is pitchbend + epsilon only (LFO1 per osc).
      // Character pitch jitter stacks after matrix when scale nonzero.
      int64_t modifiersBase_q24 =
        (int64_t)calcPitchbend_q24 + (int64_t)Q24_ONE_EPS + (int64_t)matrix_pitch_mod_q24;
      if (char_pitch_scale_q15) {
        modifiersBase_q24 += (int64_t)character_pitch_delta_q24();
      }
      int64_t freqModifiers_q24 = ADSRModifierOSC1_q24 + DETUNE_DRIFT_OSC1_q24 + modifiersBase_q24 + unisonMODIFIER_OSC1_q24 + (int64_t)lfo1_pitch_mod_q24[LFO1_PITCH_OSC1];
      int64_t freq2Modifiers_q24 = ADSRModifierOSC2_q24 + DETUNE_DRIFT_OSC2_q24 + modifiersBase_q24 + unisonMODIFIER_OSC2_q24 + (int64_t)lfo1_pitch_mod_q24[LFO1_PITCH_OSC2] + (int64_t)lfo2_pitch_mod_q24[LFO2_PITCH_OSC2];
      int64_t freq3Modifiers_q24 = ADSRModifierOSC3_q24 + DETUNE_DRIFT_OSC3_q24 + modifiersBase_q24 + unisonMODIFIER_OSC3_q24 + (int64_t)lfo1_pitch_mod_q24[LFO1_PITCH_OSC3] + (int64_t)lfo2_pitch_mod_q24[LFO2_PITCH_OSC3];
      BENCH_END(vt_modifiers);

      BENCH_BEGIN(vt_freq_scale_x);


      // Fast fixed-point equivalent of:
      //   freq  *= interpolatePitchMultiplier(freqModifiers)/multiplierTableScale;
      //   freq2 *= OSC2_detune * interpolatePitchMultiplier(freq2Modifiers)/multiplierTableScale;
      //   freq3 *= OSC3_detune * interpolatePitchMultiplier(freq3Modifiers)/multiplierTableScale;
#if PITCH_INTERP_MODE == PITCH_INTERP_RATIO_Q16
      // Native Q16 modifier domain: xQ16 = trunc(modifiers_q24 / 2^8) (1.0 → 65536).
      int32_t xScaled1_Q16 =
        (freqModifiers_q24 >= 0) ? (int32_t)(freqModifiers_q24 >> 8)
                                 : (int32_t)(-((-freqModifiers_q24) >> 8));
      int32_t xScaled2_Q16 =
        (freq2Modifiers_q24 >= 0) ? (int32_t)(freq2Modifiers_q24 >> 8)
                                  : (int32_t)(-((-freq2Modifiers_q24) >> 8));
      int32_t xScaled3_Q16 =
        (freq3Modifiers_q24 >= 0) ? (int32_t)(freq3Modifiers_q24 >> 8)
                                  : (int32_t)(-((-freq3Modifiers_q24) >> 8));
#else
      // Q12 A/B: legacy ×10000 table-units as Q16.
      int64_t x1_q24s = (freqModifiers_q24 * (int64_t)multiplierTableScale);
      int64_t x2_q24s = (freq2Modifiers_q24 * (int64_t)multiplierTableScale);
      int64_t x3_q24s = (freq3Modifiers_q24 * (int64_t)multiplierTableScale);
      int32_t xScaled1_Q16 = (x1_q24s >= 0) ? (int32_t)(x1_q24s >> 8) : (int32_t)(-((-x1_q24s) >> 8));
      int32_t xScaled2_Q16 = (x2_q24s >= 0) ? (int32_t)(x2_q24s >> 8) : (int32_t)(-((-x2_q24s) >> 8));
      int32_t xScaled3_Q16 = (x3_q24s >= 0) ? (int32_t)(x3_q24s >> 8) : (int32_t)(-((-x3_q24s) >> 8));
#endif
      BENCH_END(vt_freq_scale_x);

      BENCH_BEGIN(vt_ratio_interp);
#if PITCH_INTERP_MODE == PITCH_INTERP_RATIO_Q16
      int32_t ratio1_Q16 = interpolateRatioQ16_cached(xScaled1_Q16, DCO_A);
      int32_t ratio2_Q16 = interpolateRatioQ16_cached(xScaled2_Q16, DCO_B);
      int32_t ratio3_Q16 = interpolateRatioQ16_cached(xScaled3_Q16, DCO_C);
      BENCH_END(vt_ratio_interp);

      BENCH_BEGIN(vt_freq_scale_post);
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
      // PITCH_INTERP_Q12
      int32_t yTab1 = interpolatePitchMultiplierIntQ16_cached(xScaled1_Q16, DCO_A);
      int32_t yTab2 = interpolatePitchMultiplierIntQ16_cached(xScaled2_Q16, DCO_B);
      int32_t yTab3 = interpolatePitchMultiplierIntQ16_cached(xScaled3_Q16, DCO_C);
      BENCH_END(vt_ratio_interp);

      BENCH_BEGIN(vt_freq_scale_post);
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

      BENCH_END(vt_freq_scale_post);
      // Convert from Q24 fixed-point to a compact Q4 (Hz * 2^4) representation.
      // freq_q24_X is Hz * 2^24, so shifting right by 20 yields Hz * 2^4.
      uint32_t freqA_Q4 = (uint32_t)((freq_q24_A + (1LL << 19)) >> 20);  // round to nearest
      uint32_t freqB_Q4 = (uint32_t)((freq_q24_B + (1LL << 19)) >> 20);  // round to nearest
      uint32_t freqC_Q4 = (uint32_t)((freq_q24_C + (1LL << 19)) >> 20);
      if (freqA_Q4 == 0) freqA_Q4 = 1;
      if (freqB_Q4 == 0) freqB_Q4 = 1;
      if (freqC_Q4 == 0) freqC_Q4 = 1;

      PIO pioN_A = pio[VOICE_TO_PIO[DCO_A]];
      PIO pioN_B = pio[VOICE_TO_PIO[DCO_B]];
      PIO pioN_C = pio[VOICE_TO_PIO[DCO_C]];
      uint8_t smAN = VOICE_TO_SM[DCO_A];
      uint8_t smBN = VOICE_TO_SM[DCO_B];
      uint8_t smCN = VOICE_TO_SM[DCO_C];

      // voice_task_3_time = micros() - voice_task_start_time;

      BENCH_BEGIN(vt_clk_div);

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

      // Period model per oscillator: period = Y + weight*clk_div + overhead. The weight
      // and overhead depend on which program the SM runs (the polled sync program spends
      // two cycles per iteration in its final chunk).
      const uint32_t wA = osc_ramp_weight(DCO_A), kA = osc_period_overhead(DCO_A);
      const uint32_t wB = osc_ramp_weight(DCO_B), kB = osc_period_overhead(DCO_B);
      const uint32_t wC = osc_ramp_weight(DCO_C), kC = osc_period_overhead(DCO_C);

      total_cycles1 += arbitrary_measured_correction_value;
      total_cycles2 += arbitrary_measured_correction_value;
      total_cycles3 += arbitrary_measured_correction_value;

      clk_div1 = pio_clk_div_for_y(total_cycles1, osc_last_y[DCO_A], wA, kA);
      clk_div2 = pio_clk_div_for_y(total_cycles2, osc_last_y[DCO_B], wB, kB);
      clk_div3 = pio_clk_div_for_y(total_cycles3, osc_last_y[DCO_C], wC, kC);
      BENCH_END(vt_clk_div);

      // Measured apart from the divider math because it only runs with sync on and a
      // non-zero phase offset; averaging the two together hides both.
      BENCH_BEGIN(vt_phase_align);
      // Phase align applies to OSC2 only (OSC1<->OSC2 sync); OSC3 is free-running.
      // The coarse part of the offset is a jump into a later ramp chunk at note-on,
      // which costs no waveform distortion at all. Only the sub-quarter residual widens
      // the reset pulse, so the held-reset distortion is capped at 25% of a period
      // instead of the old worst case of nearly a whole period at 180 degrees.
      uint8_t phaseQuarters = 0;
      // Phase-align / oscSync offset is mono-only (para has no coupled A+B sync).
      if (voiceMode == 0 && oscSync > 1 && phaseAlignOSC2 != 0) {
        uint16_t deg = (uint16_t)(phaseAlignOSC2 % 360u);
        phaseQuarters = (uint8_t)(deg / 90u);
        uint16_t residualDeg = (uint16_t)(deg - (uint16_t)phaseQuarters * 90u);
        uint64_t phase_num = (uint64_t)total_cycles2 * (uint64_t)residualDeg;
        phaseDelay = (uint32_t)((phase_num + 180u) / 360u);
      } else {
        phaseDelay = 0;
      }
      BENCH_END(vt_phase_align);

      // Exact period split on note-on+EXACT_Y frames, next to hot phase_align (I-cache).
      // Stash for the note-on block; OSC3 stays on the rounded clk_div path.
      PioPeriod retrig_p1{};
      PioPeriod retrig_p2{};
      uint32_t retrig_y2 = 0;
      // Mono EXACT_Y A+B stash only. Para splits inside its per-osc note-on loop.
      if (voiceMode == 0 && note_on_flag_flag[i] && oscSync >= 1 &&
          note_retrig_mode != NOTE_RETRIG_SYNC_JMP) {
        BENCH_BEGIN(vt_retrig_split);
        retrig_p1 = pio_period_split(total_cycles1, wA, kA);
        retrig_p2 = pio_period_split(total_cycles2 - phaseDelay, wB, kB);
        retrig_y2 = retrig_p2.y + phaseDelay;
        BENCH_END(vt_retrig_split);
      }

      BENCH_BEGIN(vt_chan_level);

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
      BENCH_END(vt_chan_level);

      BENCH_BEGIN(vt_pio_write);
      pio_sm_put(pioN_A, smAN, clk_div1);
      pio_sm_put(pioN_B, smBN, clk_div2);
      pio_sm_put(pioN_C, smCN, clk_div3);
      pio_sm_exec(pioN_A, smAN, pio_encode_pull(false, false));
      pio_sm_exec(pioN_B, smBN, pio_encode_pull(false, false));
      pio_sm_exec(pioN_C, smCN, pio_encode_pull(false, false));
      osc_last_clk_div[DCO_A] = clk_div1;
      osc_last_clk_div[DCO_B] = clk_div2;
      osc_last_clk_div[DCO_C] = clk_div3;
      BENCH_END(vt_pio_write);

      // Mono: A+B oscSync retrig + RANGE A/B/C. Para: osc k only, no oscSync.
      if (voiceMode == 0) {
      if (note_on_flag_flag[i]) {
        BENCH_BEGIN(vt_note_retrig);
        // --- Reverse Calculation to find the expected output frequency ---
        uint32_t actual_total_osr_val = clk_div1 * wA;
        uint32_t actual_total_period = osc_last_y[DCO_A] + actual_total_osr_val + kA;
        float expected_freq = (double)sysClock_Hz / (double)actual_total_period;

#if DCO_DEBUG_REPORT
        // Exact split for the report only (live path splits inside oscSync below).
        PioPeriod p1dbg = pio_period_split(total_cycles1, wA, kA);
        // --- Print Diagnostic Report ---
        Serial.println("----------------[ DCO DEBUG REPORT ]----------------");
        Serial.printf("Target Freq In:   %.2f Hz\n", (float)freq_q24_A / (float)(1 << 24));
        Serial.printf("Total Cycles Calc:  %lu (Target for the whole period)\n", total_cycles1);
        Serial.printf("Reset pulse (Y):    %lu cycles (incl. period remainder)\n", p1dbg.y);
        Serial.printf("Period Overhead:    %lu cycles (program constant)\n", kA);
        Serial.printf("Total OSR Delay:    %lu cycles (Remaining for loops)\n", p1dbg.clk_div * wA);
        Serial.printf("clk_div (Exact):    %lu (Value sent to PIO)\n", p1dbg.clk_div);
        Serial.println("---");
        Serial.printf("Actual Period Gen:  %lu cycles (Y + (clk_div*%u) + overhead)\n",
                      actual_total_period, (unsigned)wA);
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
        Serial.printf("  lfo1_pitch_mod OSC1:      %.6f\n", (double)lfo1_pitch_mod_q24[LFO1_PITCH_OSC1] / (double)(1 << 24));
        Serial.printf("  unisonMODIFIER_q24:        %.6f\n", (double)unisonMODIFIER_OSC1_q24 / (double)(1 << 24));
        Serial.printf("  pitchbend_q24:             %.6f\n", (double)calcPitchbend_q24 / (double)(1 << 24));
        Serial.printf("  Q24_ONE_EPS:               %.6f\n", (double)Q24_ONE_EPS / (double)(1 << 24));
        Serial.printf("  modifiersBase_q24:         %.6f\n", (double)modifiersBase_q24 / (double)(1 << 24));
        Serial.println("OSC2 LFO2 pitch (fine + coarse):");
        Serial.printf("  lfo2_fine OSC2:            %.6f\n",
                      (double)lfo::applyDepthQ24(LFO2Level, LFO2toOSC2_q24) / (double)(1 << 24));
        Serial.printf("  lfo2_coarse OSC2:          %.6f\n",
                      (double)lfo::applyDepthQ24(LFO2Level, LFO2toOSC2_coarse_q24) / (double)(1 << 24));
        Serial.printf("  freq2Modifiers_q24:        %.6f\n", (double)freq2Modifiers_q24 / (double)(1 << 24));
        Serial.println("---");

        Serial.println("OSC1 Multiplier Table Inputs:");
        Serial.printf("  xScaled1_Q16 (natural):    %.6f\n", (double)xScaled1_Q16 / 65536.0);
        Serial.printf("  ratio1_Q16:                %.6f\n", (double)ratio1_Q16 / (double)(1 << 16));
        Serial.println("----------------------------------------------------\n");

#endif

        if (oscSync >= 1) {
          // One probe for the whole SM apply path — fine MAIN slices mis-attribute cold XIP.
          BENCH_BEGIN(vt_retrig_sm_apply);
          if (note_retrig_mode != NOTE_RETRIG_SYNC_JMP) {
            // Exact split was computed after phase_align (retrig_p1 / retrig_y2).
            // OSC3 is deliberately absent: never retriggered; Y stays at pioPulseLength.
            uint32_t maskAB = (1u << smAN) | (1u << smBN);
            pio_set_sm_mask_enabled(pio[0], maskAB, false);

            // Note-on is the one point where Y can be rewritten safely. Frame already did
            // put+pull, so TX is empty — fused noclear load (no FJOIN).
            osc_load_periods_stopped_noclear(DCO_A, retrig_p1.y, retrig_p1.clk_div,
                                             DCO_B, retrig_y2, retrig_p2.clk_div);

            pio_sm_exec(pioN_A, smAN, pio_encode_jmp(osc_restart_target(DCO_A)));

            if (phaseQuarters != 0) {
              // Coarse phase advance: enter a later ramp chunk so OSC2's first cycle starts
              // partway up the ramp. That entry point skips `set pins, 0`, so the reset pin
              // has to be driven low explicitly first.
              pio_sm_exec(pioN_B, smBN, pio_encode_set(pio_pins, 0));
              pio_sm_exec(pioN_B, smBN,
                          pio_encode_jmp(osc_ramp_entry_target(DCO_B, phaseQuarters)));
            } else {
              pio_sm_exec(pioN_B, smBN, pio_encode_jmp(osc_restart_target(DCO_B)));
            }

            pio_enable_sm_mask_in_sync(pio[0], maskAB);
          } else {
            // SYNC_JMP: phase resync only — no disable / Y load / enable_in_sync.
            pio_sm_exec(pioN_A, smAN, pio_encode_jmp(osc_restart_target(DCO_A)));
            if (phaseQuarters != 0) {
              // Coarse phase advance: enter a later ramp chunk so OSC2's first cycle starts
              // partway up the ramp. That entry point skips `set pins, 0`, so the reset pin
              // has to be driven low explicitly first.
              pio_sm_exec(pioN_B, smBN, pio_encode_set(pio_pins, 0));
              pio_sm_exec(pioN_B, smBN,
                          pio_encode_jmp(osc_ramp_entry_target(DCO_B, phaseQuarters)));
            } else {
              pio_sm_exec(pioN_B, smBN, pio_encode_jmp(osc_restart_target(DCO_B)));
            }
          }
          BENCH_END(vt_retrig_sm_apply);
        }

        BENCH_BEGIN(vt_retrig_pwm);
        if (char_amp_scale_q15) {
          const int32_t amp_j = character_amp_delta();
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], RANGE_PWM_CHANNELS[DCO_A],
                             character_clamp_amp((int32_t)chanLevel + amp_j));
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], RANGE_PWM_CHANNELS[DCO_B],
                             character_clamp_amp((int32_t)chanLevel2 + amp_j));
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], RANGE_PWM_CHANNELS[DCO_C],
                             character_clamp_amp((int32_t)chanLevel3 + amp_j));
        } else {
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], RANGE_PWM_CHANNELS[DCO_A], chanLevel);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], RANGE_PWM_CHANNELS[DCO_B], chanLevel2);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], RANGE_PWM_CHANNELS[DCO_C], chanLevel3);
        }
        BENCH_END(vt_retrig_pwm);
        BENCH_END(vt_note_retrig);
      }

      } else {
        // Paraphonic: edge k → RANGE osc k only (same as mono with oscSync off).
        // No SM stop/restart — that phase-resets and clicks when porta keeps the amp open.
        // Pitch continues via the per-frame put/pull above. No oscSync / phase-align.
        uint16_t chanLevels[3];
        if (char_amp_scale_q15) {
          const int32_t amp_j = character_amp_delta();
          chanLevels[0] = character_clamp_amp((int32_t)chanLevel + amp_j);
          chanLevels[1] = character_clamp_amp((int32_t)chanLevel2 + amp_j);
          chanLevels[2] = character_clamp_amp((int32_t)chanLevel3 + amp_j);
        } else {
          chanLevels[0] = chanLevel;
          chanLevels[1] = chanLevel2;
          chanLevels[2] = chanLevel3;
        }

        for (int k = 0; k < NUM_VOICES; k++) {
          if (!note_on_flag_flag[k]) continue;
          BENCH_BEGIN(vt_note_retrig);
          const uint8_t osc = (uint8_t)k;
          BENCH_BEGIN(vt_retrig_pwm);
          pwm_set_chan_level(RANGE_PWM_SLICES[osc], RANGE_PWM_CHANNELS[osc], chanLevels[k]);
          BENCH_END(vt_retrig_pwm);
          BENCH_END(vt_note_retrig);
        }
      }

      if (timer99microsFlag) {
        BENCH_BEGIN(vt_range_pwm);
        if (char_amp_scale_q15) {
          const int32_t amp_j = character_amp_delta();
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], RANGE_PWM_CHANNELS[DCO_A],
                             character_clamp_amp((int32_t)chanLevel + amp_j));
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], RANGE_PWM_CHANNELS[DCO_B],
                             character_clamp_amp((int32_t)chanLevel2 + amp_j));
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], RANGE_PWM_CHANNELS[DCO_C],
                             character_clamp_amp((int32_t)chanLevel3 + amp_j));
        } else {
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], RANGE_PWM_CHANNELS[DCO_A], chanLevel);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], RANGE_PWM_CHANNELS[DCO_B], chanLevel2);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], RANGE_PWM_CHANNELS[DCO_C], chanLevel3);
        }
        BENCH_END(vt_range_pwm);

        // Shared PW PWM when any oscillator has analog Pulse enabled (DG411).
        const bool pulseOn = waveEnable[0][1] || waveEnable[1][1] || waveEnable[2][1];
        if (pulseOn) {
          BENCH_FBEGIN(vt_pwm_calc);
          // Optimized: This version avoids storing large intermediate products.
          // The multiplication and shift are combined into one expression per modulator,
          // allowing the compiler to make better use of registers.
          int32_t adsr1_delta =
            (int32_t)(((int64_t)ADSR1Level_q15[i] * (int64_t)ADSR1toPWM_scale) >> 15);
          int32_t lfo2_delta =
            (int32_t)(((int64_t)LFO2Level * (int64_t)local_LFO2toPW) >> 15);
          int32_t pw_calc = (int32_t)DIV_COUNTER_PW - 1 - lfo2_delta - PW[0] + adsr1_delta
                            + character_pw_delta();

          if (pw_calc < 0) pw_calc = 0;
          if (pw_calc > (int32_t)DIV_COUNTER_PW - 1) pw_calc = (int32_t)DIV_COUNTER_PW - 1;
          PW_PWM[i] = (uint16_t)pw_calc;
          BENCH_FEND(vt_pwm_calc);

          BENCH_BEGIN(vt_pw_update);
          // PW_PWM[i] = (uint16_t)constrain(DIV_COUNTER_PW - 1 - /*((float)ADSR3Level[i] * ADSR3toPWM_formula)*/ - ((float)LFO2Level * LFO2toPWM_formula) - PW /*+ RANDOMNESS1 + RANDOMNESS2*/, 0, DIV_COUNTER_PW-1);
          pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), get_PW_level_interpolated(PW_PWM[i], i));
          BENCH_END(vt_pw_update);

        } else {
          BENCH_BEGIN(vt_pw_update);
          pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), 0);
          BENCH_END(vt_pw_update);
        }
      }
  }

  for (int k = 0; k < NUM_VOICES; k++) {
    note_on_flag_flag[k] = false;
  }

  // Whole-task timing is taken by the BENCH_voice_task probe around voice_task_main() in
  // loop1(), so both engines are measured at the same boundary.

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
  voice_task_fixed_point();
#endif
}

#ifdef USE_FLOAT_VOICE_TASK
// Float realtime voice engine (same stages as voice_task_fixed_point, in Hz). Board default on RP2350.
inline void voice_task_float() {
    // --- Track portamento control changes exactly as in original ---
    static uint32_t last_portamento_time = 0;
    static uint8_t  last_portamento_mode = PORTA_MODE_SLEW;
    uint32_t portaTime = portamento_time;
    uint8_t  portaMode = portamento_mode;
    bool portaTimeChanged = (portaTime != last_portamento_time);
    bool portaModeChanged = (portaMode != last_portamento_mode);
  
    // --- 1. Pitch bend as float, equivalent to Q24 math ---
    BENCH_BEGIN(vt_pitchbend);
    // Use original float pitch bend behaviour, but derive multiplier from Q24
    float pitchBendMultiplier = q24_to_float(pitchBendMultiplier_q24);
    float calcPitchbend;

    if (midi_pitch_bend == 8192) {
      calcPitchbend = 0.0f;
    } else if (midi_pitch_bend < 8192) {
      calcPitchbend = (((float)midi_pitch_bend / 8190.99f) - 1.0f) * pitchBendMultiplier;
    } else {  // midi_pitch_bend > 8192
      calcPitchbend = (((float)midi_pitch_bend / 8192.99f) - 1.0f) * pitchBendMultiplier;
    }
    BENCH_END(vt_pitchbend);

    last_midi_pitch_bend = midi_pitch_bend;
  
    // Cache volatile PW depth (ADSR uses ADSR1toPWM_scale).
    const int16_t local_LFO2toPW   = LFO2toPW;

    // Latch note-on edges for every voice slot (para may set flags on 1/2).
    for (int k = 0; k < NUM_VOICES; k++) {
      if (note_on_flag[k] == 1) {
        note_on_flag_flag[k] = true;
        note_on_flag[k] = 0;
      }
    }
  
    // --- 2. Per-voice loop (mirror original structure) ---
    for (int i = 0; i < NUM_VOICES_VOICE_TASK; ++i) {
  
  #if DCO_DEBUG_REPORT
      float dbg_freq_base_Hz      = 0.0f;
      float dbg_freq_after_mod_Hz = 0.0f;
  #endif

      // note_off keeps VOICE_NOTES (release pitch); free/steal uses VOICES[].
      // Never-played slots stay 0 — gate midi→table math (uint8 underflow → shriek).

      // --- 2.1 Note indices ---
      // Note index convention: table_index = midi - 36 + offset (36 ⇒ unison).
      //   Mono: octave_shift = global octave; OSC2/3 = relative to note1 (36 ⇒ unison with OSC1).
      //   Poly/stack: every osc uses octave_shift only (shared octave); not OSC2/3.
      const size_t NOTE_TABLE_LEN = sizeof(sNotePitches) / sizeof(sNotePitches[0]);
      uint8_t note1, note2, note3;
      if (voiceMode == 0) {
        const uint8_t vn = VOICE_NOTES[i];
        if (vn == 0) {
          note1 = note2 = note3 = 0;
        } else {
          note1 = midi_offset_to_table_index((int)vn, (int)octave_shift, NOTE_TABLE_LEN);
          note2 = midi_offset_to_table_index((int)note1, (int)OSC2_interval, NOTE_TABLE_LEN);
          note3 = midi_offset_to_table_index((int)note1, (int)OSC3_interval, NOTE_TABLE_LEN);
        }
      } else {
        const uint8_t vn0 = VOICE_NOTES[0];
        const uint8_t vn1 = VOICE_NOTES[1];
        const uint8_t vn2 = VOICE_NOTES[2];
        note1 = (vn0 == 0) ? 0 : midi_offset_to_table_index((int)vn0, (int)octave_shift, NOTE_TABLE_LEN);
        note2 = (vn1 == 0) ? 0 : midi_offset_to_table_index((int)vn1, (int)octave_shift, NOTE_TABLE_LEN);
        note3 = (vn2 == 0) ? 0 : midi_offset_to_table_index((int)vn2, (int)octave_shift, NOTE_TABLE_LEN);
      }
  
      BENCH_BEGIN(vt_osc_detune);
      // --- 2.2 OSC2/OSC3 detune (float equivalent of Q24) ---
      float detuneSteps = (float)((int)256 - OSC2DetuneVal);
      float osc2DetuneRatio = 1.0f + 0.0002f * detuneSteps;
      float detune3Steps = (float)((int)256 - OSC3DetuneVal);
      float osc3DetuneRatio = 1.0f + 0.0002f * detune3Steps;
      BENCH_END(vt_osc_detune);
  
      // base note frequencies from float table
      float noteFreq1 = sNotePitches[note1];
      float noteFreq2 = sNotePitches[note2];
      float noteFreq3 = sNotePitches[note3];
  
      float freqA, freqB, freqC;      // will hold the portamento-processed base freqs
  
      const uint8_t DCO_A = 0;
      const uint8_t DCO_B = 1;
      const uint8_t DCO_C = 2;
  
      // --- 2.3 Portamento (time mode & slew mode), float-only implementation ---
      BENCH_BEGIN(vt_portamento);

      if (portaTime > 0) {
        uint32_t now_us = micros();
        if (voiceMode == 0) {
          portamentoTimer[i] = now_us - portamentoStartMicros[i];
        } else {
          for (int k = 0; k < NUM_VOICES; k++) {
            portamentoTimer[k] = now_us - portamentoStartMicros[k];
          }
        }

        // Mono: one edge restarts all three oscs. Para: each note_on_flag_flag[k] restarts osc k only.
        // TIME = fixed duration any interval; SLEW = constant semitone rate (time ∝ interval).
        if (voiceMode == 0) {
        if (note_on_flag_flag[i]) {
          portamentoStartMicros[i] = now_us;
          portamentoTimer[i]       = 0;

          float targetNoteA = (float)note1;
          float targetNoteB = (float)note2;
          float targetNoteC = (float)note3;
          porta_setup_glide_f(DCO_A, porta_resolve_start_note_f(DCO_A, targetNoteA),
                              targetNoteA, portaMode);
          porta_setup_glide_f(DCO_B, porta_resolve_start_note_f(DCO_B, targetNoteB),
                              targetNoteB, portaMode);
          porta_setup_glide_f(DCO_C, porta_resolve_start_note_f(DCO_C, targetNoteC),
                              targetNoteC, portaMode);
        }
        } else {
          // Paraphonic: restart porta per voice slot → osc k (1:1).
          const int targetNotes[3] = { note1, note2, note3 };
          for (int k = 0; k < NUM_VOICES; k++) {
            if (!note_on_flag_flag[k]) continue;
            const uint8_t osc = (uint8_t)k;
            portamentoStartMicros[k] = now_us;
            portamentoTimer[k] = 0;
            float targetNote = (float)targetNotes[k];
            porta_setup_glide_f(osc, porta_resolve_start_note_f(osc, targetNote),
                                targetNote, portaMode);
          }
        }

        // Control-change frames: skip steady (stale start/step + huge elapsed → pitch jump).
        const bool portaNoteOnEdge =
            (voiceMode == 0) ? note_on_flag_flag[i]
                             : (note_on_flag_flag[0] || note_on_flag_flag[1] || note_on_flag_flag[2]);
        const bool portaDoRetime =
            (portaTimeChanged || portaModeChanged) && !portaNoteOnEdge;

        float curA, curB, curC;
        if (portaDoRetime) {
          portamentoStartMicros[i] = now_us;
          portamentoTimer[i]       = 0;
          if (voiceMode != 0) {
            for (int k = 0; k < NUM_VOICES; k++) {
              portamentoStartMicros[k] = now_us;
              portamentoTimer[k]       = 0;
            }
          }

          float curNoteA = freqFloat_to_noteIndex(porta_freq_cur_f[DCO_A]);
          float curNoteB = freqFloat_to_noteIndex(porta_freq_cur_f[DCO_B]);
          float curNoteC = freqFloat_to_noteIndex(porta_freq_cur_f[DCO_C]);
          porta_setup_glide_f(DCO_A, curNoteA, (float)note1, portaMode);
          porta_setup_glide_f(DCO_B, curNoteB, (float)note2, portaMode);
          porta_setup_glide_f(DCO_C, curNoteC, (float)note3, portaMode);
          curA = porta_freq_cur_f[DCO_A];
          curB = porta_freq_cur_f[DCO_B];
          curC = porta_freq_cur_f[DCO_C];
        } else {
          int32_t elapsedA = (voiceMode == 0) ? (int32_t)portamentoTimer[i] : (int32_t)portamentoTimer[0];
          int32_t elapsedB = (voiceMode == 0) ? elapsedA : (int32_t)portamentoTimer[1];
          int32_t elapsedC = (voiceMode == 0) ? elapsedA : (int32_t)portamentoTimer[2];

          float startNoteA = porta_note_start_f[DCO_A];
          float startNoteB = porta_note_start_f[DCO_B];
          float startNoteC = porta_note_start_f[DCO_C];
          float stopNoteA  = porta_note_stop_f [DCO_A];
          float stopNoteB  = porta_note_stop_f [DCO_B];
          float stopNoteC  = porta_note_stop_f [DCO_C];

          float dNoteA = stopNoteA - startNoteA;
          float dNoteB = stopNoteB - startNoteB;
          float dNoteC = stopNoteC - startNoteC;

          float curNoteA = startNoteA + porta_note_step_f[DCO_A] * (float)elapsedA;
          float curNoteB = startNoteB + porta_note_step_f[DCO_B] * (float)elapsedB;
          float curNoteC = startNoteC + porta_note_step_f[DCO_C] * (float)elapsedC;

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

          // At stop: use stop Hz (never live noteFreq* — that raced with note_on_flag).
          curA = (curNoteA == stopNoteA) ? porta_freq_stop_f[DCO_A]
                                         : noteIndex_to_freqFloat(curNoteA);
          curB = (curNoteB == stopNoteB) ? porta_freq_stop_f[DCO_B]
                                         : noteIndex_to_freqFloat(curNoteB);
          curC = (curNoteC == stopNoteC) ? porta_freq_stop_f[DCO_C]
                                         : noteIndex_to_freqFloat(curNoteC);

          porta_freq_cur_f[DCO_A] = curA;
          porta_freq_cur_f[DCO_B] = curB;
          porta_freq_cur_f[DCO_C] = curC;
        }

        freqA = curA;
        freqB = curB;
        freqC = curC;

      } else {
        // No portamento
        freqA = noteFreq1;
        freqB = noteFreq2;
        freqC = noteFreq3;

        // Keep float portamento state coherent when portamento is off.
        porta_freq_cur_f[DCO_A] = freqA;
        porta_freq_cur_f[DCO_B] = freqB;
        porta_freq_cur_f[DCO_C] = freqC;
        porta_freq_stop_f[DCO_A] = freqA;
        porta_freq_stop_f[DCO_B] = freqB;
        porta_freq_stop_f[DCO_C] = freqC;
        porta_note_cur_f[DCO_A] = (float)note1;
        porta_note_cur_f[DCO_B] = (float)note2;
        porta_note_cur_f[DCO_C] = (float)note3;
        porta_note_stop_f[DCO_A] = (float)note1;
        porta_note_stop_f[DCO_B] = (float)note2;
        porta_note_stop_f[DCO_C] = (float)note3;
        porta_note_valid[DCO_A] = true;
        porta_note_valid[DCO_B] = true;
        porta_note_valid[DCO_C] = true;
      }

      // Exclusive path tag for jitter attribution (note_on > retime > steady > off).
      if (portaTime == 0) {
        BENCH_PATH_INC(porta_off);
      } else if (note_on_flag_flag[i]) {
        BENCH_PATH_INC(porta_note_on);
      } else if (portaTimeChanged || portaModeChanged) {
        BENCH_PATH_INC(porta_retime);
      } else if (portaMode == PORTA_MODE_TIME) {
        BENCH_PATH_INC(porta_steady_time);
      } else {
        BENCH_PATH_INC(porta_steady_slew);
      }
  
  #if DCO_DEBUG_REPORT
      dbg_freq_base_Hz = freqA;
  #endif
  
      BENCH_END(vt_portamento);

      // --- 2.4 ADSR detune (float equivalent of Q24) ---
      BENCH_BEGIN(vt_adsr_mod);
      float ADSRModifier = 0.0f;
      if (ADSR1toDETUNE1 != 0) {
        float env = (float)linToLogLookup[ADSR1Level[i]];  // original int table
        float scale = q24_to_float(ADSR1toDETUNE1_scale_q24);
        ADSRModifier = env * scale;
      }
      float ADSRModifierOSC1 = (ADSR3ToOscSelect == 0 || ADSR3ToOscSelect == 2 || ADSR3ToOscSelect == 4) ? ADSRModifier : 0.0f;
      float ADSRModifierOSC2 = (ADSR3ToOscSelect == 1 || ADSR3ToOscSelect == 2 || ADSR3ToOscSelect == 4) ? ADSRModifier : 0.0f;
      float ADSRModifierOSC3 = (ADSR3ToOscSelect == 3 || ADSR3ToOscSelect == 4) ? ADSRModifier : 0.0f;
      BENCH_END(vt_adsr_mod);

      BENCH_BEGIN(vt_unison_mod);
      // --- 2.5 Unison modifier (float equivalent) ---
      static constexpr float UNISON_SCALE = 0.0001f; // from original Q24 constant
      static constexpr int32_t OSC_UNISON_STEP[3] = { 0, 1, -1 };
      float unisonMODIFIER_OSC1 = (float)unisonDetune * UNISON_SCALE * (float)OSC_UNISON_STEP[0];
      float unisonMODIFIER_OSC2 = (float)unisonDetune * UNISON_SCALE * (float)OSC_UNISON_STEP[1];
      float unisonMODIFIER_OSC3 = (float)unisonDetune * UNISON_SCALE * (float)OSC_UNISON_STEP[2];
      BENCH_END(vt_unison_mod);

      BENCH_BEGIN(vt_drift_mod);
      // --- 2.6 Drift modifiers (float); LFO_DRIFT_LEVEL is Q15 ---
      static constexpr float DRIFT_UNIT = 0.0000005f;
      float driftScale =
        (float)analogDrift * DRIFT_UNIT * (float)LFO_DRIFT_CC_HALF * (1.0f / 32768.0f);
      float DETUNE_DRIFT_OSC1 = (analogDrift != 0) ? (float)LFO_DRIFT_LEVEL[DCO_A] * driftScale : 0.0f;
      float DETUNE_DRIFT_OSC2 = (analogDrift != 0) ? (float)LFO_DRIFT_LEVEL[DCO_B] * driftScale : 0.0f;
      float DETUNE_DRIFT_OSC3 = (analogDrift != 0) ? (float)LFO_DRIFT_LEVEL[DCO_C] * driftScale : 0.0f;
      BENCH_END(vt_drift_mod);

      BENCH_BEGIN(vt_modifiers);
      float lfo1_osc1        = q24_to_float(lfo1_pitch_mod_q24[LFO1_PITCH_OSC1]);
      float lfo1_osc2        = q24_to_float(lfo1_pitch_mod_q24[LFO1_PITCH_OSC2]);
      float lfo1_osc3        = q24_to_float(lfo1_pitch_mod_q24[LFO1_PITCH_OSC3]);
      float lfo2_osc2        = q24_to_float(lfo2_pitch_mod_q24[LFO2_PITCH_OSC2]);
      float lfo2_osc3        = q24_to_float(lfo2_pitch_mod_q24[LFO2_PITCH_OSC3]);
      float eps              = q24_to_float(Q24_ONE_EPS);
      float pitchBendF   = calcPitchbend;
  
      // Character pitch jitter stacks after matrix when scale nonzero.
      float modifiersBase = pitchBendF + eps + q24_to_float(matrix_pitch_mod_q24);
      if (char_pitch_scale_q15) {
        modifiersBase += q24_to_float(character_pitch_delta_q24());
      }
      float freqModifiers1 = ADSRModifierOSC1 + DETUNE_DRIFT_OSC1 + modifiersBase + unisonMODIFIER_OSC1 + lfo1_osc1;
      float freqModifiers2 = ADSRModifierOSC2 + DETUNE_DRIFT_OSC2 + modifiersBase + unisonMODIFIER_OSC2 + lfo1_osc2 + lfo2_osc2;
      float freqModifiers3 = ADSRModifierOSC3 + DETUNE_DRIFT_OSC3 + modifiersBase + unisonMODIFIER_OSC3 + lfo1_osc3 + lfo2_osc3;
      BENCH_END(vt_modifiers);

      BENCH_BEGIN(vt_freq_scale_x);
      // --- 2.7 Multiplier table & ratio interpolation (float version) ---
#if PITCH_INTERP_MODE == PITCH_INTERP_Q12
      // Q12 A/B: legacy ×10000 table-units → Q16.
      float x1 = freqModifiers1 * (float)multiplierTableScale;
      float x2 = freqModifiers2 * (float)multiplierTableScale;
      float x3 = freqModifiers3 * (float)multiplierTableScale;
#endif
      BENCH_END(vt_freq_scale_x);

      BENCH_BEGIN(vt_ratio_interp);
#if PITCH_INTERP_MODE == PITCH_INTERP_FLOAT
      float ratio1 = interpolateRatioFloat_cached(freqModifiers1, DCO_A);
      float ratio2 = interpolateRatioFloat_cached(freqModifiers2, DCO_B);
      float ratio3 = interpolateRatioFloat_cached(freqModifiers3, DCO_C);
#elif PITCH_INTERP_MODE == PITCH_INTERP_FLOAT_FAST
      float ratio1 = interpolateRatioFloat_cached_fast(freqModifiers1, DCO_A);
      float ratio2 = interpolateRatioFloat_cached_fast(freqModifiers2, DCO_B);
      float ratio3 = interpolateRatioFloat_cached_fast(freqModifiers3, DCO_C);
#elif PITCH_INTERP_MODE == PITCH_INTERP_RATIO_Q16
      // Native Q16 tables: natural modifier → Q16 → float ratio.
      int32_t x1_Q16 = (int32_t)lroundf(freqModifiers1 * 65536.0f);
      int32_t x2_Q16 = (int32_t)lroundf(freqModifiers2 * 65536.0f);
      int32_t x3_Q16 = (int32_t)lroundf(freqModifiers3 * 65536.0f);
      static constexpr float Q16_TO_F = 1.0f / 65536.0f;
      float ratio1 = (float)interpolateRatioQ16_cached(x1_Q16, DCO_A) * Q16_TO_F;
      float ratio2 = (float)interpolateRatioQ16_cached(x2_Q16, DCO_B) * Q16_TO_F;
      float ratio3 = (float)interpolateRatioQ16_cached(x3_Q16, DCO_C) * Q16_TO_F;
#else
      // A/B: IntQ16 (Q12) inside float voice.
      int32_t x1_Q16 = (int32_t)lroundf(x1 * 65536.0f);
      int32_t x2_Q16 = (int32_t)lroundf(x2 * 65536.0f);
      int32_t x3_Q16 = (int32_t)lroundf(x3 * 65536.0f);
      float ratio1 = (float)interpolatePitchMultiplierIntQ16_cached(x1_Q16, DCO_A)
                     / (float)multiplierTableScale;
      float ratio2 = (float)interpolatePitchMultiplierIntQ16_cached(x2_Q16, DCO_B)
                     / (float)multiplierTableScale;
      float ratio3 = (float)interpolatePitchMultiplierIntQ16_cached(x3_Q16, DCO_C)
                     / (float)multiplierTableScale;
#endif
      BENCH_END(vt_ratio_interp);

      BENCH_BEGIN(vt_freq_scale_post);
      // Apply ratios to portamento frequencies, with osc2/osc3 detune (Hz domain).
      float freqA_Hz = freqA * ratio1;
      float freqB_Hz = freqB * (ratio2 * osc2DetuneRatio);
      float freqC_Hz = freqC * (ratio3 * osc3DetuneRatio);

  #if DCO_DEBUG_REPORT
      dbg_freq_after_mod_Hz = freqA_Hz;
  #endif

      BENCH_END(vt_freq_scale_post);

      clkdiv_bench_sample(freqA_Hz, freqB_Hz);

      // --- 2.8 Clock divider calculation (float equivalent) ---
      BENCH_BEGIN(vt_clk_div);

      float correction = 0.0f;   // keep your measured correction if needed
      float totalCycles1_f = (float)sysClock_Hz / freqA_Hz + correction;
      float totalCycles2_f = (float)sysClock_Hz / freqB_Hz + correction;
      float totalCycles3_f = (float)sysClock_Hz / freqC_Hz + correction;

      // Round to whole cycles here, then share the integer period model with the
      // fixed-point engine so both tune identically. fminf guards the cast: a zero
      // frequency yields +inf, which would otherwise overflow the uint32.
      uint32_t total_cycles1 = (uint32_t)fminf(totalCycles1_f + 0.5f, 4.0e9f);
      uint32_t total_cycles2 = (uint32_t)fminf(totalCycles2_f + 0.5f, 4.0e9f);
      uint32_t total_cycles3 = (uint32_t)fminf(totalCycles3_f + 0.5f, 4.0e9f);

      const uint32_t wA = osc_ramp_weight(DCO_A), kA = osc_period_overhead(DCO_A);
      const uint32_t wB = osc_ramp_weight(DCO_B), kB = osc_period_overhead(DCO_B);
      const uint32_t wC = osc_ramp_weight(DCO_C), kC = osc_period_overhead(DCO_C);

      uint32_t clk_div1 = pio_clk_div_for_y(total_cycles1, osc_last_y[DCO_A], wA, kA);
      uint32_t clk_div2 = pio_clk_div_for_y(total_cycles2, osc_last_y[DCO_B], wB, kB);
      uint32_t clk_div3 = pio_clk_div_for_y(total_cycles3, osc_last_y[DCO_C], wC, kC);
      BENCH_END(vt_clk_div);

      // Measured apart from the divider math because it only runs with sync on and a
      // non-zero phase offset; averaging the two together hides both.
      BENCH_BEGIN(vt_phase_align);
      // Phase align (OSC2 only). Coarse offset becomes a ramp-chunk entry jump at
      // note-on; only the sub-quarter residual widens the reset pulse.
      uint8_t phaseQuarters = 0;
      uint32_t phaseDelay = 0;
      // Phase-align / oscSync offset is mono-only (para has no coupled A+B sync).
      if (voiceMode == 0 && oscSync > 1 && phaseAlignOSC2 != 0) {
        uint16_t deg = (uint16_t)(phaseAlignOSC2 % 360u);
        phaseQuarters = (uint8_t)(deg / 90u);
        uint16_t residualDeg = (uint16_t)(deg - (uint16_t)phaseQuarters * 90u);
        phaseDelay = (uint32_t)(((float)total_cycles2 * (float)residualDeg / 360.0f) + 0.5f);
      }
      BENCH_END(vt_phase_align);

      // Exact period split on note-on+EXACT_Y frames, next to hot phase_align (I-cache).
      // Stash for the note-on block; OSC3 stays on the rounded clk_div path.
      PioPeriod retrig_p1{};
      PioPeriod retrig_p2{};
      uint32_t retrig_y2 = 0;
      // Mono EXACT_Y A+B stash only. Para splits inside its per-osc note-on loop.
      if (voiceMode == 0 && note_on_flag_flag[i] && oscSync >= 1 &&
          note_retrig_mode != NOTE_RETRIG_SYNC_JMP) {
        BENCH_BEGIN(vt_retrig_split);
        retrig_p1 = pio_period_split(total_cycles1, wA, kA);
        retrig_p2 = pio_period_split(total_cycles2 - phaseDelay, wB, kB);
        retrig_y2 = retrig_p2.y + phaseDelay;
        BENCH_END(vt_retrig_split);
      }

      // --- 2.9 Amplitude compensation using engine-agnostic helper ---
      BENCH_BEGIN(vt_chan_level);

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
      BENCH_END(vt_chan_level);

      // --- 2.10 PIO + PWM + PW math (very close to original, but float inside PW calc) ---
      PIO pioN_A = pio[VOICE_TO_PIO[DCO_A]];
      PIO pioN_B = pio[VOICE_TO_PIO[DCO_B]];
      PIO pioN_C = pio[VOICE_TO_PIO[DCO_C]];
      uint8_t sm1N = VOICE_TO_SM[DCO_A];
      uint8_t sm2N = VOICE_TO_SM[DCO_B];
      uint8_t smCN = VOICE_TO_SM[DCO_C];

      BENCH_BEGIN(vt_pio_write);
      pio_sm_put(pioN_A, sm1N, clk_div1);
      pio_sm_put(pioN_B, sm2N, clk_div2);
      pio_sm_put(pioN_C, smCN, clk_div3);
      pio_sm_exec(pioN_A, sm1N, pio_encode_pull(false, false));
      pio_sm_exec(pioN_B, sm2N, pio_encode_pull(false, false));
      pio_sm_exec(pioN_C, smCN, pio_encode_pull(false, false));
      osc_last_clk_div[DCO_A] = clk_div1;
      osc_last_clk_div[DCO_B] = clk_div2;
      osc_last_clk_div[DCO_C] = clk_div3;
      BENCH_END(vt_pio_write);

      // Mono: A+B oscSync retrig + RANGE A/B/C. Para: osc k only, no oscSync.
      if (voiceMode == 0) {
      if (note_on_flag_flag[i]) {
        BENCH_BEGIN(vt_note_retrig);
        // Sync logic mirrored from fixed-point voice_task_fixed_point, using float-derived periods.
        if (oscSync >= 1) {
          // One probe for the whole SM apply path — fine MAIN slices mis-attribute cold XIP.
          BENCH_BEGIN(vt_retrig_sm_apply);
          if (note_retrig_mode != NOTE_RETRIG_SYNC_JMP) {
            // Exact split was computed after phase_align (retrig_p1 / retrig_y2).
            // OSC3 is deliberately absent: never retriggered; Y stays at pioPulseLength.
            uint32_t maskAB = (1u << sm1N) | (1u << sm2N);
            pio_set_sm_mask_enabled(pio[0], maskAB, false);

            // Frame already did put+pull, so TX is empty — fused noclear load (no FJOIN).
            osc_load_periods_stopped_noclear(DCO_A, retrig_p1.y, retrig_p1.clk_div,
                                             DCO_B, retrig_y2, retrig_p2.clk_div);

            pio_sm_exec(pioN_A, sm1N, pio_encode_jmp(osc_restart_target(DCO_A)));

            if (phaseQuarters != 0) {
              // Enter a later ramp chunk for the coarse phase advance; that path skips
              // `set pins, 0`, so drive the reset pin low explicitly first.
              pio_sm_exec(pioN_B, sm2N, pio_encode_set(pio_pins, 0));
              pio_sm_exec(pioN_B, sm2N,
                          pio_encode_jmp(osc_ramp_entry_target(DCO_B, phaseQuarters)));
            } else {
              pio_sm_exec(pioN_B, sm2N, pio_encode_jmp(osc_restart_target(DCO_B)));
            }

            pio_enable_sm_mask_in_sync(pio[0], maskAB);
          } else {
            // SYNC_JMP: phase resync only — no disable / Y load / enable_in_sync.
            if (phaseQuarters != 0) {
              // Enter a later ramp chunk for the coarse phase advance; that path skips
              // `set pins, 0`, so drive the reset pin low explicitly first.
              pio_sm_exec(pioN_B, sm2N, pio_encode_set(pio_pins, 0));
              pio_sm_exec(pioN_B, sm2N,
                          pio_encode_jmp(osc_ramp_entry_target(DCO_B, phaseQuarters)));
              pio_sm_exec(pioN_A, sm1N, pio_encode_jmp(osc_restart_target(DCO_A)));
            } else {
              pio_sm_exec(pioN_A, sm1N, pio_encode_jmp(osc_restart_target(DCO_A)));
              pio_sm_exec(pioN_B, sm2N, pio_encode_jmp(osc_restart_target(DCO_B)));
            }
          }
          BENCH_END(vt_retrig_sm_apply);
        }

        BENCH_BEGIN(vt_retrig_pwm);
        if (char_amp_scale_q15) {
          const int32_t amp_j = character_amp_delta();
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], RANGE_PWM_CHANNELS[DCO_A],
                             character_clamp_amp((int32_t)chanLevel + amp_j));
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], RANGE_PWM_CHANNELS[DCO_B],
                             character_clamp_amp((int32_t)chanLevel2 + amp_j));
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], RANGE_PWM_CHANNELS[DCO_C],
                             character_clamp_amp((int32_t)chanLevel3 + amp_j));
        } else {
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], RANGE_PWM_CHANNELS[DCO_A], chanLevel);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], RANGE_PWM_CHANNELS[DCO_B], chanLevel2);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], RANGE_PWM_CHANNELS[DCO_C], chanLevel3);
        }
        BENCH_END(vt_retrig_pwm);
        BENCH_END(vt_note_retrig);
      }

      } else {
        // Paraphonic: edge k → RANGE osc k only (same as mono with oscSync off).
        // No SM stop/restart — that phase-resets and clicks when porta keeps the amp open.
        // Pitch continues via the per-frame put/pull above. No oscSync / phase-align.
        uint16_t chanLevels[3];
        if (char_amp_scale_q15) {
          const int32_t amp_j = character_amp_delta();
          chanLevels[0] = character_clamp_amp((int32_t)chanLevel + amp_j);
          chanLevels[1] = character_clamp_amp((int32_t)chanLevel2 + amp_j);
          chanLevels[2] = character_clamp_amp((int32_t)chanLevel3 + amp_j);
        } else {
          chanLevels[0] = chanLevel;
          chanLevels[1] = chanLevel2;
          chanLevels[2] = chanLevel3;
        }

        for (int k = 0; k < NUM_VOICES; k++) {
          if (!note_on_flag_flag[k]) continue;
          BENCH_BEGIN(vt_note_retrig);
          const uint8_t osc = (uint8_t)k;
          BENCH_BEGIN(vt_retrig_pwm);
          pwm_set_chan_level(RANGE_PWM_SLICES[osc], RANGE_PWM_CHANNELS[osc], chanLevels[k]);
          BENCH_END(vt_retrig_pwm);
          BENCH_END(vt_note_retrig);
        }
      }
  
      if (timer99microsFlag) {
        BENCH_BEGIN(vt_range_pwm);
        if (char_amp_scale_q15) {
          const int32_t amp_j = character_amp_delta();
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], RANGE_PWM_CHANNELS[DCO_A],
                             character_clamp_amp((int32_t)chanLevel + amp_j));
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], RANGE_PWM_CHANNELS[DCO_B],
                             character_clamp_amp((int32_t)chanLevel2 + amp_j));
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], RANGE_PWM_CHANNELS[DCO_C],
                             character_clamp_amp((int32_t)chanLevel3 + amp_j));
        } else {
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], RANGE_PWM_CHANNELS[DCO_A], chanLevel);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], RANGE_PWM_CHANNELS[DCO_B], chanLevel2);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], RANGE_PWM_CHANNELS[DCO_C], chanLevel3);
        }
        BENCH_END(vt_range_pwm);

        const bool pulseOn = waveEnable[0][1] || waveEnable[1][1] || waveEnable[2][1];
        if (pulseOn) {
          BENCH_FBEGIN(vt_pwm_calc);
          float adsr1_delta =
            ((float)ADSR1Level_q15[i] * (float)ADSR1toPWM_scale) * (1.0f / 32768.0f);
          float lfo2_delta =
            ((float)LFO2Level * (float)local_LFO2toPW) * (1.0f / 32768.0f);
          float pw_calc =
              (float)DIV_COUNTER_PW - 1.0f
            - (float)PW[0]
            - lfo2_delta
            + adsr1_delta
            + (float)character_pw_delta();
  
          if (pw_calc < 0.0f) pw_calc = 0.0f;
          if (pw_calc > (float)(DIV_COUNTER_PW - 1)) pw_calc = (float)(DIV_COUNTER_PW - 1);
  
          PW_PWM[i] = (uint16_t)pw_calc;
          BENCH_FEND(vt_pwm_calc);

          BENCH_BEGIN(vt_pw_update);
          pwm_set_chan_level(PW_PWM_SLICES[i],
                             pwm_gpio_to_channel(PW_PINS[i]),
                             get_PW_level_interpolated(PW_PWM[i], i));
          BENCH_END(vt_pw_update);
        } else {
          BENCH_BEGIN(vt_pw_update);
          pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), 0);
          BENCH_END(vt_pw_update);
        }
      }
    }

    for (int k = 0; k < NUM_VOICES; k++) {
      note_on_flag_flag[k] = false;
    }

  // Whole-task timing is taken by the BENCH_voice_task probe around voice_task_main() in
  // loop1(), so both engines are measured at the same boundary.

  last_portamento_time = portaTime;
  last_portamento_mode = portaMode;
}
#endif  // USE_FLOAT_VOICE_TASK

// Round-robin free-voice allocator. Called from note_on() when polyMode == 1.
inline uint8_t get_free_voice_sequential() {
  uint8_t nextVoice;
  uint8_t freeVoices = 0;

  if (VOICES[VOICES_LAST_SEQUENCE[NUM_VOICES - 1]] == 1 || VOICES[VOICES_LAST_SEQUENCE[NUM_VOICES - 1]] == 0) {
    for (int voiceIndex = NUM_VOICES - 1; voiceIndex > 0; voiceIndex--) {
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
    if (VOICES[VOICES_LAST_SEQUENCE[NUM_VOICES - 1]] == 0) {
      nextVoice = VOICES_LAST_SEQUENCE[NUM_VOICES - 1];

      for (int voiceIndex = NUM_VOICES - 1; voiceIndex > 0; voiceIndex--) {
        VOICES_LAST_SEQUENCE[voiceIndex] = VOICES_LAST_SEQUENCE[voiceIndex - 1];
      }

      VOICES_LAST_SEQUENCE[0] = nextVoice;

      return nextVoice;
    }
  }
  if (freeVoices == 0) {
    nextVoice = VOICES_LAST_SEQUENCE[NUM_VOICES - 1];

    for (int voiceIndex = NUM_VOICES - 1; voiceIndex > 0; voiceIndex--) {
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

  for (int i = 0; i < NUM_VOICES; i++)  // REVISAR!!
  {
    uint8_t n = (NEXT_VOICE + i) % NUM_VOICES;

    if (VOICES[n] == 0) {
      NEXT_VOICE = (n + 1) % NUM_VOICES;
      return n;
    }

    if (VOICES[i] < oldest_time) {
      oldest_time = VOICES[i];
      oldest_voice = i;
    }
  }

  NEXT_VOICE = (oldest_voice + 1) % NUM_VOICES;
  return oldest_voice;
}

// Map voiceMode → NUM_VOICES / STACK_VOICES. Called from init_voices and apply_param_voice_mode.
//   0 mono:       one MIDI voice → oscs 0..2 (engine still bound by NUM_VOICES_VOICE_TASK)
//   1 paraphonic: up to TOTAL notes, voice i → osc i (ownership in voice_task not wired yet)
//   2 stub:       DCO4 stack leftover — counts only; no new stack behavior
inline void setVoiceMode() {
  switch (voiceMode) {
    case 0:
      NUM_VOICES = 1;
      STACK_VOICES = 1;
      // Drop any stale held notes when entering mono.
      mono_note_stack_clear();
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

// Rebuild the PIO sync topology and retrigger voices.
// Called from apply_param_sync_mode (Serial2).
void setSyncMode() {
  // assign_sm_mapping() keeps the slave below its master in SM index; start_voice_sms()
  // re-derives every SM's program, set pin and sideset pin from syncMode and
  // softSyncChunks, then starts them all on the same cycle.
  //
  // The old implementation poked sideset pins in place and called pio_sm_restart(),
  // which cleared the shift counters but left PC, X and Y — it could strand an SM
  // mid-loop with a stale X for one glitched period. The note_on_flag retrigger below
  // already re-pushes everything, so the restart was never needed.
  assign_sm_mapping();
  start_voice_sms();

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    note_on_flag[i] = 1;
  }
}

/**
 * @brief Fast amplitude compensation lookup (Q8 Hz) for fixed path / AMP_COMP_FIXED.
 *
 * Uses a deterministic window selection rule that matches the float path:
 * for each oscillator we choose the *first* window i such that
 *   freqRow[i] <= x < freqRow[i+2]
 * scanning from low to high. Always compiled so float-engine builds can
 * select FIXED for A/B and benchmarks.
 */
uint16_t get_chan_level_lookup_fast(int32_t x, uint8_t voiceN) {
  const int32_t* freqRow   = ampCompFrequencyArray[voiceN];
  const int32_t* ampRow    = ampCompArray[voiceN];
  const int32_t* xBaseRow  = xBaseWIN[voiceN];
  const int32_t* spanRow   = dxWIN[voiceN];
  const uint32_t* invRow_q28 = invDxWIN_q28[voiceN];
  const int32_t* aRow      = aQWIN_fast[voiceN];
  const int32_t* bRow      = bQWIN_fast[voiceN];
  const uint16_t* cRow     = cQWIN[voiceN];

  if (x <= freqRow[0]) return (uint16_t)ampRow[0];

  // Domain ceiling: at/above AMP_COMP_MAX_HZ the cal sentinel is full RANGE.
  // Without this, x == MAX_Q matches no window (exclusive upper bound) and the
  // fallback window-0 path clamps t∈[0,1] to the first segment — ~12k PWM off
  // float, which extrapolates absolute Hz on that window to ~DIV_COUNTER.
  if (x >= AMP_COMP_MAX_HZ_Q) return (uint16_t)DIV_COUNTER;

  // Real plateau only: precompute leaves plateauStartFreqQ at AMP_COMP_MAX_HZ_Q
  // when none was found (same role as plateauStartIndex < 0 in the original code).
  // Do not use plateauStartIndex here — dual-build restores that for the float path.
  if (plateauStartFreqQ[voiceN] < AMP_COMP_MAX_HZ_Q &&
      x >= plateauStartFreqQ[voiceN]) {
    return (uint16_t)DIV_COUNTER;
  }

  const int maxWindow = ampCompTableSize - 2;
  int window = 0;
  for (int i = 0; i <= maxWindow; ++i) {
    if (x >= freqRow[i] && x < freqRow[i + 2]) {
      window = i;
      break;
    }
  }

  int32_t dx = x - xBaseRow[window];
  const int32_t span = spanRow[window];
  if (dx < 0) dx = 0;
  if (dx > span) dx = span;

  const uint32_t inv_q28 = invRow_q28[window];
  uint32_t t_q = (uint32_t)(((uint64_t)dx * inv_q28) >> (28 - T_FRAC));

  int64_t a = aRow[window];
  int64_t b = bRow[window];
  int32_t c = cRow[window];

  uint32_t t2 = (uint32_t)(((uint32_t)t_q * t_q) >> T_FRAC);
  int32_t term_a = (int32_t)((a * t2) >> T_FRAC);
  int32_t term_b = (int32_t)((b * t_q) >> T_FRAC);

  int32_t y_q = term_a + term_b + (c << T_FRAC);
  int32_t y = (y_q + (1 << (T_FRAC - 1))) >> T_FRAC;

  if (y < 0) y = 0;
  if (y > (int32_t)DIV_COUNTER) y = (int32_t)DIV_COUNTER;

  return (uint16_t)y;
}

#ifdef USE_FLOAT_AMP_COMP
/**
 * @brief Pure-float quadratic amp-comp (Hz domain). Live FLOAT_QUAD.
 * Window find: per-osc cache → walk → full scan (first-match [Hz[w], Hz[w+2]]).
 */
uint16_t get_chan_level_float_quad(float freqHz, uint8_t voiceN) {
  if (freqHz <= ampCompFrequencyHz[voiceN][0]) {
    BENCH_PATH_INC(amp_clamp);
    return (uint16_t)ampCompArray[voiceN][0];
  }

  // Same domain ceiling as FIXED (see get_chan_level_lookup_fast).
  if (freqHz >= (float)AMP_COMP_MAX_HZ) {
    BENCH_PATH_INC(amp_clamp);
    return (uint16_t)DIV_COUNTER;
  }

  if (plateauStartIndex[voiceN] >= 0) {
    float plateauFreqHz = plateauStartFreqHz[voiceN];
    if (freqHz >= plateauFreqHz) {
      BENCH_PATH_INC(amp_clamp);
      return (uint16_t)DIV_COUNTER;
    }
  }

  const int maxWindow = ampCompTableSize - 2;
  const float *hzRow = ampCompFrequencyHz[voiceN];
  int window = ampWinCache[voiceN];

  if (window >= 0 && window <= maxWindow &&
      freqHz >= hzRow[window] && freqHz < hzRow[window + 2]) {
    BENCH_PATH_INC(amp_hit);
  } else {
    int cand = window;
    if (cand < 0 || cand > maxWindow) cand = 0;
    uint32_t steps = 0;
    if (freqHz >= hzRow[cand + 2]) {
      while (cand < maxWindow && freqHz >= hzRow[cand + 2]) {
        ++cand;
        ++steps;
      }
    } else if (freqHz < hzRow[cand]) {
      while (cand > 0 && freqHz < hzRow[cand]) {
        --cand;
        ++steps;
      }
    }
    if (cand >= 0 && cand <= maxWindow &&
        freqHz >= hzRow[cand] && freqHz < hzRow[cand + 2]) {
      window = cand;
      BENCH_PATH_INC(amp_miss_walk);
      bench_path_amp_walk_steps(steps);
    } else {
      window = 0;
      for (int i = 0; i <= maxWindow; ++i) {
        if (freqHz >= hzRow[i] && freqHz < hzRow[i + 2]) {
          window = i;
          break;
        }
      }
      BENCH_PATH_INC(amp_miss_scan);
    }
    ampWinCache[voiceN] = (int16_t)window;
  }

  float a = aCoeff[voiceN][window];
  float b = bCoeff[voiceN][window];
  float c = cCoeff[voiceN][window];

  float interpolatedValue = (a * freqHz + b) * freqHz + c;
  return (uint16_t)round(interpolatedValue);
}

uint16_t get_chan_level_lut(float freqHz, uint8_t voiceN) {
  if (freqHz <= 0.0f) return ampCompLut[voiceN][0];
  if (freqHz >= (float)AMP_COMP_MAX_HZ) return ampCompLut[voiceN][AMP_COMP_MAX_HZ];
  // Nearest integer Hz (not trunc) — same single table load, lower quantize bias.
  // Stay off LUT[MAX] until the hard clamp above: rounding 6999.75→7000 would
  // return the plateau bin while float is still on the last window (~200 counts).
  int32_t hz = (int32_t)(freqHz + 0.5f);
  if (hz < 0) hz = 0;
  if (hz >= AMP_COMP_MAX_HZ) hz = AMP_COMP_MAX_HZ - 1;
  return ampCompLut[voiceN][hz];
}
#endif  // USE_FLOAT_AMP_COMP

// Map raw PW counter into calibrated center/limits for one oscillator. Used on the 99 µs PW path.
inline uint16_t get_PW_level_interpolated(uint16_t PWval, uint8_t oscN) {

  uint16_t chanLevel;

  // Horizontal PW axis: 0 .. DIV_COUNTER_PW-1 (pot/LFO/ADSR domain)
  // Vertical axis (output): mapped to calibrated low/center/high PWM limits.

  if (PWval >= (DIV_COUNTER_PW - 1)) {
    // Above max PW, clamp to calibrated high limit.
    return PW_HIGH_LIMIT[oscN];
  } else if (PWval <= 0) {
    // Below min PW, clamp to calibrated low limit.
    return PW_LOW_LIMIT[oscN];
  } else {
    uint16_t pwLowBreak  = PW_LOOKUP[0];  // usually 0
    uint16_t pwMidBreak  = PW_LOOKUP[1];  // mid-point
    uint16_t pwHighBreak = PW_LOOKUP[2];  // usually DIV_COUNTER_PW-1

    if (PWval >= pwMidBreak) {
      // Upper half: interpolate from center to high limit.
      chanLevel = map(PWval,
                      pwMidBreak, pwHighBreak,
                      PW_CENTER[oscN], PW_HIGH_LIMIT[oscN]);
    } else {
      // Lower half: interpolate from low limit to center.
      chanLevel = map(PWval,
                      pwLowBreak, pwMidBreak,
                      PW_LOW_LIMIT[oscN], PW_CENTER[oscN]);
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

  // Target period in cycles for the calibration tone. Guarded because freq can be 0,
  // which would make the float division infinite and the cast undefined.
  uint32_t autotune_total_cycles =
      (freq > 0.0f) ? (uint32_t)fminf(((float)sysClock_Hz / freq) + 0.5f, 4.0e9f) : 0u;

  if (manualCalibrationFlag == true) {  // One Ocillator at a time to get correct gap

    uint8_t currentCalibrationOscillator = (uint8_t)manualCalibrationStage;
    if (currentCalibrationOscillator >= NUM_OSCILLATORS) {
      currentCalibrationOscillator = NUM_OSCILLATORS - 1;
    }

    // ALL AT ONCE
    for (int i = 0; i < NUM_OSCILLATORS; i++) {
      PIO pioN = pio[VOICE_TO_PIO[i]];
      uint8_t sm1N = VOICE_TO_SM[i];

      if (i != currentCalibrationOscillator) {
        uint32_t silence_clk_div1 = 200;

        pio_sm_put(pioN, sm1N, silence_clk_div1);
        pio_sm_exec(pioN, sm1N, pio_encode_pull(false, false));
        pwm_set_chan_level(RANGE_PWM_SLICES[i], RANGE_PWM_CHANNELS[i], 0);
      } else {

        uint32_t clk_div1 = autotune_total_cycles
                              ? pio_clk_div_for_y(autotune_total_cycles, osc_last_y[i],
                                                  osc_ramp_weight(i), osc_period_overhead(i))
                              : 0u;

        pio_sm_put(pioN, sm1N, clk_div1);

        pio_sm_exec(pioN, sm1N, pio_encode_pull(false, false));

        pwm_set_chan_level(RANGE_PWM_SLICES[i], RANGE_PWM_CHANNELS[i], calibrationValue);

        pwm_set_chan_level(PW_PWM_SLICES[0], pwm_gpio_to_channel(PW_PINS[0]), 0);

        //Serial.println((String) "currentCalibrationOscillator: " + (int)currentCalibrationOscillator + (String) "   calibrationValue: " + (int)calibrationValue);
      }
    }
  } else {

    PIO pioN = pio[VOICE_TO_PIO[currentDCO]];
    uint8_t sm1N = VOICE_TO_SM[currentDCO];

    uint32_t clk_div1 = autotune_total_cycles
                          ? pio_clk_div_for_y(autotune_total_cycles, osc_last_y[currentDCO],
                                              osc_ramp_weight(currentDCO),
                                              osc_period_overhead(currentDCO))
                          : 0u;

    pio_sm_put(pioN, sm1N, clk_div1);
    pio_sm_exec(pioN, sm1N, pio_encode_pull(false, false));

    switch (taskAutotuneVoiceMode) {
      case 0:
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], RANGE_PWM_CHANNELS[currentDCO], calibrationValue);
        break;
      case 1:
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], RANGE_PWM_CHANNELS[currentDCO], calibrationValue);
        pio_sm_exec(pioN, sm1N, pio_encode_jmp(osc_restart_target(currentDCO)));
        break;
      case 2:
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], RANGE_PWM_CHANNELS[currentDCO], chanLevel);
        break;
      case 3:
        chanLevel = get_chan_level_for_engine(freq, currentDCO);
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], RANGE_PWM_CHANNELS[currentDCO], chanLevel);
      case 4:
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], RANGE_PWM_CHANNELS[currentDCO], calibrationValue);
        break;
    }

    //}
    // Serial.println((String) "| currentDCO: " + currentDCO + (String) " | freq: " + freq + (String) " | clk_div1: " + clk_div1 + (String) " | ampCompCalibrationVal: " + ampCompCalibrationVal);
  }
}

// Cached variant: pass DCO index to reuse last segment and avoid binary search
#if PITCH_INTERP_MODE == PITCH_INTERP_Q12
inline int32_t interpolatePitchMultiplierIntQ16_cached(int32_t xQ16, int dcoIndex) {
  int32_t xInt = xQ16 >> 16;
  if (xInt <= xMultiplierTable[0]) {
    return yMultiplierTable[0];
  }
  if (xInt >= xMultiplierTable[multiplierTableSize - 1]) {
    return yMultiplierTable[multiplierTableSize - 1];
  }
  int low = interpSegCache[dcoIndex];
  if (low < 0 || low > multiplierTableSize - 2 || !(xMultiplierTable[low] <= xInt && xInt < xMultiplierTable[low + 1])) {
    if (low >= 0 && low < multiplierTableSize - 1) {
      if (xInt >= xMultiplierTable[low + 1]) {
        while (low < multiplierTableSize - 2 && xInt >= xMultiplierTable[low + 1]) low++;
      } else if (xInt < xMultiplierTable[low]) {
        while (low > 0 && xInt < xMultiplierTable[low]) low--;
      }
    }
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
  int32_t deltaQ12 = (xQ16 - (x0 << 16)) >> 4;
  int32_t y = y0 + (int32_t)((((int64_t)deltaQ12 * (int64_t)slopeQ12[low]) + (1LL << 23)) >> 24);
  return y;
}
#endif // Q12

#if PITCH_INTERP_MODE == PITCH_INTERP_RATIO_Q16
// xQ16 and tables are natural Q16 (1.0 = 65536). Returns frequency ratio as Q16.
inline int32_t interpolateRatioQ16_cached(int32_t xQ16, int dcoIndex) {
  if (xQ16 <= xMultiplierTable[0]) {
    return yMultiplierTable[0];
  }
  if (xQ16 >= xMultiplierTable[multiplierTableSize - 1]) {
    return yMultiplierTable[multiplierTableSize - 1];
  }
  int low = interpSegCache[dcoIndex];
  if (low < 0 || low > multiplierTableSize - 2 ||
      !(xMultiplierTable[low] <= xQ16 && xQ16 < xMultiplierTable[low + 1])) {
    if (low >= 0 && low < multiplierTableSize - 1) {
      if (xQ16 >= xMultiplierTable[low + 1]) {
        while (low < multiplierTableSize - 2 && xQ16 >= xMultiplierTable[low + 1]) low++;
      } else if (xQ16 < xMultiplierTable[low]) {
        while (low > 0 && xQ16 < xMultiplierTable[low]) low--;
      }
    }
    if (!(low >= 0 && low < multiplierTableSize - 1 &&
          xMultiplierTable[low] <= xQ16 && xQ16 < xMultiplierTable[low + 1])) {
      int l = 0, h = multiplierTableSize - 1;
      while (l <= h) {
        int m = (l + h) >> 1;
        if (xMultiplierTable[m] <= xQ16 && xQ16 < xMultiplierTable[m + 1]) {
          low = m;
          break;
        } else if (xQ16 < xMultiplierTable[m]) {
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
  const int32_t x0 = xMultiplierTable[low];
  const int32_t y0 = yMultiplierTable[low];
  const int32_t delta = xQ16 - x0;
  // slopeQ20 = (dy << 20) / dx  →  y = y0 + (delta * slope) >> 20
  return y0 + (int32_t)((((int64_t)delta * (int64_t)slopeQ20[low]) + (1LL << 19)) >> 20);
}
#endif // PITCH_INTERP_RATIO_Q16

#if PITCH_INTERP_MODE == PITCH_INTERP_FLOAT
// x = pitch modifier in ~[-1, 3]; returns frequency ratio directly (tables are unscaled).
// Walk + bsearch find (A/B vs FLOAT_FAST).
inline float interpolateRatioFloat_cached(float x, int dcoIndex) {
  if (x <= xMultiplierTableF[0]) {
    return yMultiplierTableF[0];
  }
  if (x >= xMultiplierTableF[multiplierTableSize - 1]) {
    return yMultiplierTableF[multiplierTableSize - 1];
  }

  int low = interpSegCache[dcoIndex];
  if (low < 0 || low > multiplierTableSize - 2 ||
      !(xMultiplierTableF[low] <= x && x < xMultiplierTableF[low + 1])) {
    if (low >= 0 && low < multiplierTableSize - 1) {
      if (x >= xMultiplierTableF[low + 1]) {
        while (low < multiplierTableSize - 2 && x >= xMultiplierTableF[low + 1]) ++low;
      } else if (x < xMultiplierTableF[low]) {
        while (low > 0 && x < xMultiplierTableF[low]) --low;
      }
    }
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

  return yMultiplierTableF[low] + slopeF[low] * (x - xMultiplierTableF[low]);
}
#endif // PITCH_INTERP_FLOAT

#if PITCH_INTERP_MODE == PITCH_INTERP_FLOAT_FAST
// Trunc+clamp±1 find; same lerp as walk. Keep ±1 even when walk_steps≈0 (live ballast).
// noinline: isolate codegen from voice_task_float.
__attribute__((noinline))
float interpolateRatioFloat_cached_fast(float x, int dcoIndex) {
  // Endpoints match initMultiplierTables (-1 / 3).
  if (x <= -1.0f) {
    BENCH_PATH_INC(ratio_clamp);
    return yMultiplierTableF[0];
  }
  if (x >= 3.0f) {
    BENCH_PATH_INC(ratio_clamp);
    return yMultiplierTableF[multiplierTableSize - 1];
  }

  static constexpr float kPitchX0 = -1.0f;
  static constexpr float kPitchInvDx = (float)multiplierTableSize / 4.0f; // N/4 = 50
  const int lastSeg = multiplierTableSize - 2;

  int low = interpSegCache[dcoIndex];
  if (low >= 0 && low <= lastSeg &&
      xMultiplierTableF[low] <= x && x < xMultiplierTableF[low + 1]) {
    BENCH_PATH_INC(ratio_hit);
  } else {
    int cand = (int)((x - kPitchX0) * kPitchInvDx);
    if (cand < 0) cand = 0;
    else if (cand > lastSeg) cand = lastSeg;
    // Float rounding / live ballast: keep both ±1 compares even if steps stay 0.
    uint32_t steps = 0;
    if (cand < lastSeg && x >= xMultiplierTableF[cand + 1]) {
      ++cand;
      steps = 1;
    } else if (cand > 0 && x < xMultiplierTableF[cand]) {
      --cand;
      steps = 1;
    }
    low = cand;
    BENCH_PATH_INC(ratio_miss_direct);
    bench_path_walk_steps(steps);
    interpSegCache[dcoIndex] = (int16_t)low;
  }

  return yMultiplierTableF[low] + slopeF[low] * (x - xMultiplierTableF[low]);
}
#endif // PITCH_INTERP_FLOAT_FAST

// Build integer/float pitch-multiplier tables and slopes (boot). Called from init_voices().
void initMultiplierTables() {

  float y_value;
  double divisor = multiplierTableSize;
  double fraction = 4.00d / divisor;

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

#if PITCH_INTERP_MODE == PITCH_INTERP_FLOAT || \
    PITCH_INTERP_MODE == PITCH_INTERP_FLOAT_FAST
    // Natural domain: x = modifier [-1,3], y = frequency ratio (no int-table scale).
    xMultiplierTableF[i] = (float)x;
    yMultiplierTableF[i] = y_value;
#elif PITCH_INTERP_MODE == PITCH_INTERP_RATIO_Q16
    // Native Q16: x and y store natural units (1.0 = 65536). No ×10000.
    xMultiplierTable[i] = (int32_t)(x * 65536.0 + (x >= 0.0 ? 0.5 : -0.5));
    yMultiplierTable[i] = (int32_t)((double)y_value * 65536.0 + 0.5);
    x0Q16_tbl[i] = xMultiplierTable[i];
#else
    // Q12 A/B: legacy ×10000 table-units.
    xMultiplierTable[i] = (int32_t)(x * (double)multiplierTableScale);
    yMultiplierTable[i] = (int32_t)(y_value * (double)multiplierTableScale);
    x0Q16_tbl[i]        = xMultiplierTable[i] << 16;
#endif
  }

#if PITCH_INTERP_MODE == PITCH_INTERP_RATIO_Q16
  for (int i = 0; i < (multiplierTableSize - 1); ++i) {
    int32_t dx = xMultiplierTable[i + 1] - xMultiplierTable[i];
    if (dx == 0) dx = 1;
    int32_t dy = yMultiplierTable[i + 1] - yMultiplierTable[i];
    int64_t numSlope = ((int64_t)dy << 20) + (dx > 0 ? dx / 2 : -dx / 2);
    slopeQ20[i] = (int32_t)(numSlope / (int64_t)dx);
  }
#elif PITCH_INTERP_MODE == PITCH_INTERP_Q12
  for (int i = 0; i < (multiplierTableSize - 1); ++i) {
    int32_t dx = xMultiplierTable[i + 1] - xMultiplierTable[i];
    if (dx == 0) dx = 1;
    int32_t dy = yMultiplierTable[i + 1] - yMultiplierTable[i];
    int64_t numSlope12 = ((int64_t)dy << 12) + (dx > 0 ? dx / 2 : -dx / 2);
    slopeQ12[i] = (int32_t)(numSlope12 / (int64_t)dx);
  }
#elif PITCH_INTERP_MODE == PITCH_INTERP_FLOAT || \
      PITCH_INTERP_MODE == PITCH_INTERP_FLOAT_FAST
  for (int i = 0; i < multiplierTableSize - 1; ++i) {
    float dxF = xMultiplierTableF[i + 1] - xMultiplierTableF[i];
    if (dxF == 0.0f) dxF = 1.0f;
    slopeF[i] = (yMultiplierTableF[i + 1] - yMultiplierTableF[i]) / dxF;
  }
#endif

  for (int d = 0; d < NUM_OSCILLATORS; ++d) interpSegCache[d] = -1;
}

// Report the float-vs-double clock-divider comparison and clear its accumulators.
// Called from bench_poll_core0() on core 0, right after the main profiler report.
// Per-phase voice-task timings now come from the BENCH_* probes in bench.h.
void print_clkdiv_bench() {
#if defined(CLKDIV_BENCHMARK) && defined(RUNNING_AVERAGE)
  // Appends into bench_out_buf; paced TX is handled by bench_poll_core0().
  if (clkdiv_bench_count > 0) {
    double avgFloatUs  = clkdiv_time_float_sum_us  / (double)clkdiv_bench_count;
    double avgDoubleUs = clkdiv_time_double_sum_us / (double)clkdiv_bench_count;
    double avgDelta1   = (double)clkdiv_delta1_sum / (double)clkdiv_bench_count;
    double avgDelta2   = (double)clkdiv_delta2_sum / (double)clkdiv_bench_count;
    double avgFreqDiff1 = clkdiv_freq1_diff_sum / (double)clkdiv_bench_count;
    double avgFreqDiff2 = clkdiv_freq2_diff_sum / (double)clkdiv_bench_count;

    bench_out_println("=== CLKDIV BENCH ===");
    bench_out_printf("count=%lu avgFloatUs=%.3f avgDoubleUs=%.3f\n",
                     (unsigned long)clkdiv_bench_count, avgFloatUs, avgDoubleUs);
    bench_out_printf("clk_div1 delta avg=%.3f maxAbs=%lu\n",
                     avgDelta1, (unsigned long)clkdiv_delta1_max);
    bench_out_printf("clk_div2 delta avg=%.3f maxAbs=%lu\n",
                     avgDelta2, (unsigned long)clkdiv_delta2_max);
    bench_out_printf("freq1 diff avg=%.6f Hz maxAbs=%.6f\n",
                     avgFreqDiff1, clkdiv_freq1_diff_max_abs);
    bench_out_printf("freq2 diff avg=%.6f Hz maxAbs=%.6f\n",
                     avgFreqDiff2, clkdiv_freq2_diff_max_abs);
    bench_out_printf("OSC1 last: target=%.6f Hz float=%.6f Hz double=%.6f Hz\n",
                     clkdiv_last_target1_Hz, clkdiv_last_out1_float_Hz,
                     clkdiv_last_out1_double_Hz);
    bench_out_printf("OSC2 last: target=%.6f Hz float=%.6f Hz double=%.6f Hz\n",
                     clkdiv_last_target2_Hz, clkdiv_last_out2_float_Hz,
                     clkdiv_last_out2_double_Hz);
    bench_out_println("====================");

    clkdiv_bench_count         = 0;
    clkdiv_time_float_sum_us  = 0.0;
    clkdiv_time_double_sum_us = 0.0;
    clkdiv_delta1_sum = clkdiv_delta2_sum = 0;
    clkdiv_delta1_max = clkdiv_delta2_max = 0;
    clkdiv_freq1_diff_sum = clkdiv_freq2_diff_sum = 0.0;
    clkdiv_freq1_diff_max_abs = clkdiv_freq2_diff_max_abs = 0.0;
  }
#endif
}