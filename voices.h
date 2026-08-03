#include <cstdint>
#ifndef __VOICES_H__
#define __VOICES_H__

void init_voices();

volatile bool note_on_flag_flag[NUM_VOICES_TOTAL];

uint32_t portamentoTimer[NUM_VOICES_TOTAL];
uint32_t portamentoStartMillis[NUM_VOICES_TOTAL];
uint32_t portamentoStartMicros[NUM_VOICES_TOTAL];

bool portamento = true;
uint32_t portamento_time = 0;

// Portamento mode: 0 = fixed-time glide (current behaviour),
//                  1 = analog-style slew-rate (time scales with interval).
enum PortamentoMode : uint8_t {
  PORTA_MODE_TIME = 0,
  PORTA_MODE_SLEW = 1
};
uint8_t portamento_mode = PORTA_MODE_SLEW;

// Portamento state in Q24 (Hz * 2^24) — one slot per oscillator
int64_t portamento_start_q24[NUM_OSCILLATORS];
int64_t portamento_stop_q24[NUM_OSCILLATORS];
int64_t portamento_cur_freq_q24[NUM_OSCILLATORS];
// per-microsecond step in Q24
int64_t freqPortaStep_q24[NUM_OSCILLATORS];

// Portamento state in note-space (Q16 semitones) for slew-rate mode
int32_t porta_note_start_q16[NUM_OSCILLATORS];
int32_t porta_note_stop_q16[NUM_OSCILLATORS];
int32_t porta_note_cur_q16[NUM_OSCILLATORS];
int32_t porta_note_step_q16[NUM_OSCILLATORS];

// Float portamento state for the float engine (Hz and semitone domains)
#ifdef USE_FLOAT_VOICE_TASK
// Time-based mode: linear in frequency (Hz)
float porta_freq_start_f[NUM_OSCILLATORS];
float porta_freq_stop_f [NUM_OSCILLATORS];
float porta_freq_step_f [NUM_OSCILLATORS];  // Hz per microsecond
float porta_freq_cur_f  [NUM_OSCILLATORS];

// Slew-rate mode: linear in note-space (semitones)
float porta_note_start_f[NUM_OSCILLATORS];
float porta_note_stop_f [NUM_OSCILLATORS];
float porta_note_cur_f  [NUM_OSCILLATORS];
float porta_note_step_f [NUM_OSCILLATORS];  // semitones per microsecond
#endif

uint8_t highestNote = 124;

static const int multiplierTableSize = 200;
// Int-table scale only (FLOAT stores natural modifier/ratio and ignores this).
const int32_t multiplierTableScale = 10000;

// Pitch-multiplier storage: only the active PITCH_INTERP_MODE is allocated (see DCO.ino).
#if PITCH_INTERP_MODE == PITCH_INTERP_FLOAT
float   xMultiplierTableF[multiplierTableSize]; // modifier [-1,3]
float   yMultiplierTableF[multiplierTableSize]; // frequency ratio
float   slopeF[multiplierTableSize - 1];
#else
int32_t xMultiplierTable[multiplierTableSize];
int32_t yMultiplierTable[multiplierTableSize];
int32_t x0Q16_tbl[multiplierTableSize];
#if PITCH_INTERP_MODE == PITCH_INTERP_RATIO_Q16
int32_t slopeQ20[multiplierTableSize - 1];
#elif PITCH_INTERP_MODE == PITCH_INTERP_Q12
int32_t slopeQ12[multiplierTableSize - 1];
#endif
#endif

// Per-DCO segment cache for interpolation (stores last 'low' index)
int16_t interpSegCache[NUM_OSCILLATORS];

static const uint16_t maxFrequency = 4000;

// Q24 frequency constants
static constexpr int32_t Q24_ONE = (1 << 24);
static constexpr int32_t Q24_EPS_DELTA_1P00001 = 168; // round(0.00001 * 2^24)
static constexpr int32_t Q24_ONE_EPS = Q24_ONE + Q24_EPS_DELTA_1P00001;


// Voice-task probe storage lives in bench.h, generated from the BENCH_PROBES table. There is
// deliberately nothing to declare here: the previous extern block drifted out of step with
// voices.ino (it named probes that had been renamed or deleted) and compiled anyway, because
// an extern that is never odr-used needs no definition.

#endif
