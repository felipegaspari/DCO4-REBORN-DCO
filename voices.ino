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

      // Period model per oscillator: period = Y + weight*clk_div + overhead. The weight
      // and overhead depend on which program the SM runs (the polled sync program spends
      // two cycles per iteration in its final chunk).
      const uint32_t wA = osc_ramp_weight(DCO_A), kA = osc_period_overhead(DCO_A);
      const uint32_t wB = osc_ramp_weight(DCO_B), kB = osc_period_overhead(DCO_B);
      const uint32_t wC = osc_ramp_weight(DCO_C), kC = osc_period_overhead(DCO_C);

      total_cycles1 += arbitrary_measured_correction_value;
      total_cycles2 += arbitrary_measured_correction_value;
      total_cycles3 += arbitrary_measured_correction_value;

      // Solve clk_div against the Y each SM is actually holding, so the reset pulse and
      // the ramp always describe the same period. Y itself can only be rewritten while
      // the SM is stopped, which happens at note-on below.
      clk_div1 = pio_clk_div_for_y(total_cycles1, osc_last_y[DCO_A], wA, kA);
      clk_div3 = pio_clk_div_for_y(total_cycles3, osc_last_y[DCO_C], wC, kC);

      // Phase align applies to OSC2 only (OSC1<->OSC2 sync); OSC3 is free-running.
      // The coarse part of the offset is a jump into a later ramp chunk at note-on,
      // which costs no waveform distortion at all. Only the sub-quarter residual widens
      // the reset pulse, so the held-reset distortion is capped at 25% of a period
      // instead of the old worst case of nearly a whole period at 180 degrees.
      uint8_t phaseQuarters = 0;
      if (oscSync > 1 && phaseAlignOSC2 != 0) {
        uint16_t deg = (uint16_t)(phaseAlignOSC2 % 360u);
        phaseQuarters = (uint8_t)(deg / 90u);
        uint16_t residualDeg = (uint16_t)(deg - (uint16_t)phaseQuarters * 90u);
        uint64_t phase_num = (uint64_t)total_cycles2 * (uint64_t)residualDeg;
        phaseDelay = (uint32_t)((phase_num + 180u) / 360u);
      } else {
        phaseDelay = 0;
      }

      clk_div2 = pio_clk_div_for_y(total_cycles2, osc_last_y[DCO_B], wB, kB);

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
      osc_last_clk_div[DCO_A] = clk_div1;
      osc_last_clk_div[DCO_B] = clk_div2;
      osc_last_clk_div[DCO_C] = clk_div3;

      if (note_on_flag_flag[i]) {
        // --- Exact period split, only safe here because the SMs get stopped below ---
        // OSC3 is deliberately absent: it is never retriggered, so its Y stays at
        // pioPulseLength and its clk_div keeps the rounded path.
        PioPeriod p1 = pio_period_split(total_cycles1, wA, kA);
        PioPeriod p2 = pio_period_split(total_cycles2 - phaseDelay, wB, kB);
        uint32_t y_val2 = p2.y + phaseDelay;

        // --- Reverse Calculation to find the expected output frequency ---
        uint32_t actual_total_osr_val = clk_div1 * wA;
        uint32_t actual_total_period = osc_last_y[DCO_A] + actual_total_osr_val + kA;
        float expected_freq = (double)sysClock_Hz / (double)actual_total_period;

#if DCO_DEBUG_REPORT
        // --- Print Diagnostic Report ---
        Serial.println("----------------[ DCO DEBUG REPORT ]----------------");
        Serial.printf("Target Freq In:   %.2f Hz\n", (float)freq_q24_A / (float)(1 << 24));
        Serial.printf("Total Cycles Calc:  %lu (Target for the whole period)\n", total_cycles1);
        Serial.printf("Reset pulse (Y):    %lu cycles (incl. period remainder)\n", p1.y);
        Serial.printf("Period Overhead:    %lu cycles (program constant)\n", kA);
        Serial.printf("Total OSR Delay:    %lu cycles (Remaining for loops)\n", p1.clk_div * wA);
        Serial.printf("clk_div (Exact):    %lu (Value sent to PIO)\n", p1.clk_div);
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
        Serial.printf("  detune_fifo_q24:           %.6f\n", (double)detune_fifo_q24 / (double)(1 << 24));
        Serial.printf("  unisonMODIFIER_q24:        %.6f\n", (double)unisonMODIFIER_OSC1_q24 / (double)(1 << 24));
        Serial.printf("  pitchbend_q24:             %.6f\n", (double)calcPitchbend_q24 / (double)(1 << 24));
        Serial.printf("  Q24_ONE_EPS:               %.6f\n", (double)Q24_ONE_EPS / (double)(1 << 24));
        Serial.printf("  modifiersBase_q24:         %.6f\n", (double)modifiersBase_q24 / (double)(1 << 24));
        Serial.printf("  freqModifiers_q24:         %.6f\n", (double)freqModifiers_q24 / (double)(1 << 24));
        Serial.println("---");

        Serial.println("OSC1 Multiplier Table Inputs:");
        Serial.printf("  x1_q24s (table-units*Q24): %.6f\n", (double)x1_q24s / (double)(1 << 24));
        Serial.printf("  xScaled1_Q16:              %ld (int)\n", (long)xScaled1_Q16);
        Serial.printf("  ratio1_Q16:                %.6f\n", (double)ratio1_Q16 / (double)(1 << 16));
        Serial.println("----------------------------------------------------\n");

#endif

        if (oscSync >= 1) {
          // All oscillators share pio0, so OSC1 and OSC2 can be stopped, reloaded and
          // restarted on a single cycle. The old code drove separate blocks with separate
          // enable calls, which left a few hundred nanoseconds of skew between them.
          uint32_t maskAB = (1u << smAN) | (1u << smBN);
          pio_set_sm_mask_enabled(pio[0], maskAB, false);

          // Note-on is the one point where Y can be rewritten safely: the SMs are
          // stopped, and the envelope is at the start of its attack, so the phase
          // discontinuity is inaudible. This is where the exact-period remainder and any
          // phase-align residual actually reach the hardware.
          osc_load_period_stopped(DCO_A, p1.y, p1.clk_div);
          osc_load_period_stopped(DCO_B, y_val2, p2.clk_div);

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

      // Solve clk_div against the Y each SM currently holds; Y itself is only rewritten
      // at note-on, where the SM can be stopped safely.
      uint32_t clk_div1 = pio_clk_div_for_y(total_cycles1, osc_last_y[DCO_A], wA, kA);
      uint32_t clk_div3 = pio_clk_div_for_y(total_cycles3, osc_last_y[DCO_C], wC, kC);

      // Phase align (OSC2 only). Coarse offset becomes a ramp-chunk entry jump at
      // note-on; only the sub-quarter residual widens the reset pulse.
      uint8_t phaseQuarters = 0;
      uint32_t phaseDelay = 0;
      if (oscSync > 1 && phaseAlignOSC2 != 0) {
        uint16_t deg = (uint16_t)(phaseAlignOSC2 % 360u);
        phaseQuarters = (uint8_t)(deg / 90u);
        uint16_t residualDeg = (uint16_t)(deg - (uint16_t)phaseQuarters * 90u);
        phaseDelay = (uint32_t)(((float)total_cycles2 * (float)residualDeg / 360.0f) + 0.5f);
      }

      uint32_t clk_div2 = pio_clk_div_for_y(total_cycles2, osc_last_y[DCO_B], wB, kB);

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
      osc_last_clk_div[DCO_A] = clk_div1;
      osc_last_clk_div[DCO_B] = clk_div2;
      osc_last_clk_div[DCO_C] = clk_div3;
  
      if (note_on_flag_flag[i]) {
        // Sync logic mirrored from fixed-point voice_task, using float-derived periods.
        if (oscSync >= 1) {
          // OSC3 is deliberately absent: it is never retriggered, so its Y stays at
          // pioPulseLength and its clk_div keeps the rounded path.
          PioPeriod p1 = pio_period_split(total_cycles1, wA, kA);
          PioPeriod p2 = pio_period_split(total_cycles2 - phaseDelay, wB, kB);
          uint32_t y_val2 = p2.y + phaseDelay;

          // Everything is on pio0, so OSC1 and OSC2 stop, reload and restart on one cycle.
          uint32_t maskAB = (1u << sm1N) | (1u << sm2N);
          pio_set_sm_mask_enabled(pio[0], maskAB, false);

          osc_load_period_stopped(DCO_A, p1.y, p1.clk_div);
          osc_load_period_stopped(DCO_B, y_val2, p2.clk_div);

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
        pwm_set_chan_level(RANGE_PWM_SLICES[i], pwm_gpio_to_channel(RANGE_PINS[i]), 0);
      } else {

        uint32_t clk_div1 = autotune_total_cycles
                              ? pio_clk_div_for_y(autotune_total_cycles, osc_last_y[i],
                                                  osc_ramp_weight(i), osc_period_overhead(i))
                              : 0u;

        pio_sm_put(pioN, sm1N, clk_div1);

        pio_sm_exec(pioN, sm1N, pio_encode_pull(false, false));

        pwm_set_chan_level(RANGE_PWM_SLICES[i], pwm_gpio_to_channel(RANGE_PINS[i]), calibrationValue);

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
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], pwm_gpio_to_channel(RANGE_PINS[currentDCO]), calibrationValue);
        break;
      case 1:
        pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], pwm_gpio_to_channel(RANGE_PINS[currentDCO]), calibrationValue);
        pio_sm_exec(pioN, sm1N, pio_encode_jmp(osc_restart_target(currentDCO)));
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