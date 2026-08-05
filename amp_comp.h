#ifndef __AMP_COMP_H__
#define __AMP_COMP_H__

#include "include_all.h"
#include <math.h>
#include <limits.h>
#include <string.h>
#include "hardware/sync.h"

// Common table dimensions and shared data
static constexpr int     ampCompTableSize = 22;
static constexpr int32_t AMP_COMP_MAX_HZ  = 7000;

int32_t freq_to_amp_comp_array[chanLevelVoiceDataSize * NUM_OSCILLATORS];

// Per-oscillator plateau metadata:
//  - plateauStartIndex: first table index with freq < AMP_COMP_MAX_HZ and amp == DIV_COUNTER.
//  - plateauStartFreqQ: corresponding frequency in fixed-point Q(FREQ_FRAC_BITS) (fixed engine).
//  - plateauStartFreqHz: corresponding frequency in Hz (float engine).
int16_t plateauStartIndex[NUM_OSCILLATORS];
int32_t plateauStartFreqQ[NUM_OSCILLATORS];
float   plateauStartFreqHz[NUM_OSCILLATORS];

// Calibration levels (PWM counts) — shared by float and fixed precompute.
// (Historically float used uint16_t and fixed used int32_t; unified so both engines
// can coexist for method A/B and benches under USE_FLOAT_AMP_COMP.)
int32_t ampCompArray[NUM_OSCILLATORS][ampCompTableSize + 1];

// ----- Fixed-point (Q8) amp-comp data -----
// Always compiled: used as the live path when !USE_FLOAT_AMP_COMP, and also built
// under USE_FLOAT_AMP_COMP so AMP_COMP_FIXED can be selected / timed / compared.
//
// Frequency values for amplitude compensation are stored as fixed-point Hz (Q(FREQ_FRAC_BITS)).
static constexpr int     FREQ_FRAC_BITS    = 8;
static constexpr int32_t AMP_COMP_SENTINEL_FREQ_Q = 50000000; // sentinel marker from FS data (Q8)
static constexpr int32_t AMP_COMP_MAX_HZ_Q = (int32_t)(AMP_COMP_MAX_HZ << FREQ_FRAC_BITS);

int32_t  ampCompFrequencyArray[NUM_OSCILLATORS][ampCompTableSize + 1]; // Q8 Hz

// Per-window normalized quadratic in t = (x - x0) / (x2 - x0), where x,x0,x2 are integer Hz.
// Runtime uses 32-bit fixed-point t (Q(T_FRAC)) and precomputed integer coefficients.
static constexpr int     T_FRAC           = 12;
int32_t  xBaseWIN   [NUM_OSCILLATORS][ampCompTableSize - 1];
int32_t  dxWIN      [NUM_OSCILLATORS][ampCompTableSize - 1];
// Use Q28 reciprocal to avoid underflow on very large dx while keeping shifts small
uint32_t invDxWIN_q28[NUM_OSCILLATORS][ampCompTableSize - 1];
int64_t  aQWIN      [NUM_OSCILLATORS][ampCompTableSize - 1]; // Q(T_FRAC)
int64_t  bQWIN      [NUM_OSCILLATORS][ampCompTableSize - 1]; // Q(T_FRAC)
uint16_t cQWIN      [NUM_OSCILLATORS][ampCompTableSize - 1];
int32_t  aQWIN_fast [NUM_OSCILLATORS][ampCompTableSize - 1];
int32_t  bQWIN_fast [NUM_OSCILLATORS][ampCompTableSize - 1];

// High-precision float coefficients (Hz-domain): y = a*x^2 + b*x + c
// Used by both fixed-point (for reference) and float amp-comp paths.
float    aCoeff[NUM_OSCILLATORS][ampCompTableSize - 1];
float    bCoeff[NUM_OSCILLATORS][ampCompTableSize - 1];
float    cCoeff[NUM_OSCILLATORS][ampCompTableSize - 1];

// Float-domain frequency breakpoints (Hz) used by the float amp-comp path.
#ifdef USE_FLOAT_AMP_COMP
float    ampCompFrequencyHz[NUM_OSCILLATORS][ampCompTableSize + 1];
// Dense LUT: index = integer Hz, value = RANGE PWM.
// Filled once after float precompute from get_chan_level_float_quad() so integer-Hz
// LUT at integer Hz matches float quadratic exactly; lookup uses nearest Hz.
// Selectable for speed A/B; live default is FIXED.
uint16_t ampCompLut[NUM_OSCILLATORS][AMP_COMP_MAX_HZ + 1];
// Last quadratic window per osc for get_chan_level_float_quad (-1 = cold).
int16_t  ampWinCache[NUM_OSCILLATORS];
#endif

// ---------------------------------------------------------------------------
// Method selection (live voice_task_main dispatch under USE_FLOAT_AMP_COMP)
// ---------------------------------------------------------------------------
// Compile-time default: AMP_COMP_METHOD_DEFAULT in DCO.ino board defaults
// (RP2350 → FLOAT_QUAD, RP2040 → FIXED; overridable in ENGINE — overrides).
// Runtime: PARAM_DEBUG_COMMAND 20–22 via amp_comp_set_method().
enum AmpCompMethod : uint8_t {
  AMP_COMP_FLOAT_QUAD = 0, // cached walk + y=(a*x+b)*x+c — live default on RP2350
  AMP_COMP_LUT        = 1, // nearest Hz → ampCompLut index (speed A/B)
  AMP_COMP_FIXED      = 2, // get_chan_level_lookup_fast (Q8 tables)
};

#ifndef AMP_COMP_METHOD_DEFAULT
#define AMP_COMP_METHOD_DEFAULT AMP_COMP_FIXED
#endif

volatile uint8_t amp_comp_method = (uint8_t)AMP_COMP_METHOD_DEFAULT;

// Set by PARAM_DEBUG_COMMAND 20–22; Core 0 prints via paced bench_out when RUNNING_AVERAGE.
volatile bool amp_comp_method_ack_pending = false;

static inline const char *amp_comp_method_name(uint8_t m) {
  switch (m) {
    case AMP_COMP_FLOAT_QUAD: return "FLOAT_QUAD";
    case AMP_COMP_LUT:        return "LUT";
    case AMP_COMP_FIXED:      return "FIXED";
    default:                  return "UNKNOWN";
  }
}

static inline void amp_comp_set_method(uint8_t m) {
#ifdef USE_FLOAT_AMP_COMP
  if (m > AMP_COMP_FIXED) m = AMP_COMP_FLOAT_QUAD;
#else
  m = AMP_COMP_FIXED;
#endif
  amp_comp_method = m;
  __dmb();  // publish to Core 1 voice_task_main before continued audio / ack
}

/**
 * @brief Pre-calculates all necessary data for the fixed-point amplitude compensation function.
 *
 * This function's sole purpose is to prepare the data for `get_chan_level_lookup_fast`.
 * It is called once at startup (and again under float engine so FIXED can be selected).
 * For every 3-point window in the compensation table, it:
 * 1.  Loads the raw data into local variables for sanitization.
 * 2.  Optionally cleans the local data by handling sentinels and smoothing plateaus.
 * 3.  Computes the complete, consistent data package (base, width, reciprocal, and
 *     normalized coefficients) needed for the fast fixed-point quadratic calculation.
 */
// Fixed-point precompute: builds Q-format tables for the fixed engine / FIXED method.
// rewritePlateaus: when true (fixed-only boot), sanitize sentinels/plateaus in-place.
// When false (dual-build after float sanitize), keep seeded breakpoints and only
// build Q windows + plateauStartFreqQ — avoids a second rewrite that desyncs FIXED.
static void precomputeCoefficients(bool rewritePlateaus = true) {
  static_assert(T_FRAC > 0 && T_FRAC < 28, "T_FRAC must be in a valid range for the math to work.");

  // --- Data Sanitization ---
  // Append a final point to each table to guarantee it reaches the defined maximum.
  // This makes the system robust to incomplete calibration data from the filesystem.
  for (int j = 0; j < NUM_OSCILLATORS; ++j) {
      ampCompFrequencyArray[j][ampCompTableSize] = AMP_COMP_MAX_HZ_Q;
      ampCompArray[j][ampCompTableSize] = DIV_COUNTER;
  }

  const double freqScale    = (double)(1u << FREQ_FRAC_BITS);
  const double invFreqScale = 1.0 / freqScale;
  const double maxFreqHz    = (double)AMP_COMP_MAX_HZ;

  for (int j = 0; j < NUM_OSCILLATORS; j++) {
    bool plateauSeen = false;  // ensure we only capture plateau start once per oscillator

    // Initialise plateau metadata for this oscillator. We'll fill these as
    // soon as we detect the first real DIV_COUNTER point below AMP_COMP_MAX_HZ.
    plateauStartIndex[j] = -1;
    plateauStartFreqQ[j] = AMP_COMP_MAX_HZ_Q;

    for (int i = 0; i < ampCompTableSize - 1; ++i) {
      double x0_f = (double)ampCompFrequencyArray[j][i]     * invFreqScale;
      double x1_f = (double)ampCompFrequencyArray[j][i + 1] * invFreqScale;
      double x2_f = (double)ampCompFrequencyArray[j][i + 2] * invFreqScale;
      double y0_f = (double)ampCompArray[j][i];
      double y1_f = (double)ampCompArray[j][i + 1];
      double y2_f = (double)ampCompArray[j][i + 2];

      // Any frequency above AMP_COMP_MAX_HZ is a synthetic filler/sentinel
      // (e.g., 20 kHz) and must be treated as if it were exactly at the
      // physical maximum. Clamp those to maxFreqHz so they don't distort
      // the real plateau shape near the top of the usable range.
      if (ampCompFrequencyArray[j][i + 1] >= AMP_COMP_MAX_HZ_Q) x1_f = maxFreqHz;
      if (ampCompFrequencyArray[j][i + 2] >= AMP_COMP_MAX_HZ_Q) x2_f = maxFreqHz;

      if (y1_f >= DIV_COUNTER && y2_f >= DIV_COUNTER) {
        // Capture the plateau start from the *raw* calibration point before
        // we modify/smooth the table. We want the first real full-scale
        // breakpoint strictly below the synthetic high-frequency fillers.
        if (!plateauSeen &&
            plateauStartIndex[j] < 0 &&
            ampCompFrequencyArray[j][i + 1] < AMP_COMP_MAX_HZ_Q) {
          plateauStartIndex[j] = i + 1;
          plateauStartFreqQ[j] = ampCompFrequencyArray[j][i + 1];
          plateauSeen = true;
        }

        if (rewritePlateaus) {
          double plateau_end_y = (double)DIV_COUNTER;
          x1_f = (x0_f + maxFreqHz) * 0.5;
          y1_f = (y0_f + plateau_end_y) * 0.5;
          x2_f = maxFreqHz;
          y2_f = plateau_end_y;

          ampCompFrequencyArray[j][i + 1] = (int32_t)llround(x1_f * freqScale);
          ampCompArray[j][i + 1] = (int32_t)llround(y1_f);
          ampCompFrequencyArray[j][i + 2] = AMP_COMP_MAX_HZ_Q;
          ampCompArray[j][i + 2] = (int32_t)DIV_COUNTER;
        }
      }

      // --- 2. Calculate Float Coefficients (for the fallback / reference) ---
      // Under USE_FLOAT_AMP_COMP dual-build these aCoeff writes are overwritten by
      // a restore after fixed precompute so FLOAT_QUAD keeps the float-engine coeffs.
      long double denom_ld = (long double)(x0_f - x1_f) * (long double)(x0_f - x2_f) * (long double)(x1_f - x2_f);
      if (denom_ld == 0.0L) denom_ld = 1.0L;
      long double inv_denom_ld = 1.0L / denom_ld;

      long double aVal_ld = ((long double)x2_f * (long double)(y1_f - y0_f) +
                             (long double)x1_f * (long double)(y0_f - y2_f) +
                             (long double)x0_f * (long double)(y2_f - y1_f)) * inv_denom_ld;
      long double bVal_ld = ((long double)x2_f * (long double)x2_f * (long double)(y0_f - y1_f) +
                             (long double)x1_f * (long double)x1_f * (long double)(y2_f - y0_f) +
                             (long double)x0_f * (long double)x0_f * (long double)(y1_f - y2_f)) * inv_denom_ld;
      long double cVal_ld = ((long double)x1_f * (long double)x2_f * (long double)(x1_f - x2_f) * (long double)y0_f +
                             (long double)x2_f * (long double)x0_f * (long double)(x2_f - x0_f) * (long double)y1_f +
                             (long double)x0_f * (long double)x1_f * (long double)(x0_f - x1_f) * (long double)y2_f) * inv_denom_ld;

      aCoeff[j][i] = (float)aVal_ld;
      bCoeff[j][i] = (float)bVal_ld;
      cCoeff[j][i] = (float)cVal_ld;

      long double dx02_ld = (long double)x2_f - (long double)x0_f;
      if (dx02_ld <= 0.0L) dx02_ld = 1.0L;
      long double inv_dx02_ld = 1.0L / dx02_ld;
      long double t1_ld = ((long double)x1_f - (long double)x0_f) * inv_dx02_ld;
      long double d20_ld = (long double)y2_f - (long double)y0_f;
      long double d10_ld = (long double)y1_f - (long double)y0_f;

      long double denom_norm_ld = (t1_ld * t1_ld - t1_ld);
      if (denom_norm_ld == 0.0L) denom_norm_ld = 1.0L;
      long double inv_denom_norm_ld = 1.0L / denom_norm_ld;

      long double aN_ld = (d10_ld - d20_ld * t1_ld) * inv_denom_norm_ld;
      long double bN_ld = d20_ld - aN_ld;

      xBaseWIN[j][i] = ampCompFrequencyArray[j][i];
      dxWIN[j][i]    = ampCompFrequencyArray[j][i + 2] - ampCompFrequencyArray[j][i];
      if (dxWIN[j][i] <= 0) dxWIN[j][i] = 1;

      // Exact integer rounding for Q28 reciprocal
      {
        uint32_t dxu = (uint32_t)dxWIN[j][i];
        uint64_t num = (uint64_t)1ULL << 28;
        invDxWIN_q28[j][i] = (uint32_t)((num + (dxu >> 1)) / dxu);
      }

      aQWIN[j][i] = (int64_t)llroundl(aN_ld * (long double)(1LL << T_FRAC));
      bQWIN[j][i] = (int64_t)llroundl(bN_ld * (long double)(1LL << T_FRAC));

      int32_t c_temp = (int32_t)lrint(y0_f);
      if (c_temp < 0) c_temp = 0;
      if (c_temp > (int32_t)DIV_COUNTER) c_temp = (int32_t)DIV_COUNTER;
      cQWIN[j][i] = (uint16_t)c_temp;

      int64_t aFastLL = llroundl(aN_ld * (long double)(1 << T_FRAC));
      if (aFastLL > (int64_t)INT32_MAX) aFastLL = (int64_t)INT32_MAX;
      if (aFastLL < (int64_t)INT32_MIN) aFastLL = (int64_t)INT32_MIN;
      aQWIN_fast[j][i] = (int32_t)aFastLL;

      // Derive b so that a + b = (y2 - y0) in fast scaling => exact match at t=1
      int64_t d20_int = ((int64_t)ampCompArray[j][i + 2] - (int64_t)ampCompArray[j][i]) << T_FRAC;
      int64_t bFastLL = d20_int - aFastLL;
      if (bFastLL > (int64_t)INT32_MAX) bFastLL = (int64_t)INT32_MAX;
      if (bFastLL < (int64_t)INT32_MIN) bFastLL = (int64_t)INT32_MIN;
      bQWIN_fast[j][i] = (int32_t)bFastLL;
    }
  }
}

/**
 * @brief Float-only variant of amplitude compensation precompute.
 *
 * This version prepares only the data needed by the pure-float amp-comp path:
 *  - Sanitises ampCompFrequencyHz / ampCompArray (sentinels, plateaus).
 *  - Computes float quadratic coefficients aCoeff/bCoeff/cCoeff in Hz domain.
 *  - Fills ampCompFrequencyHz (sanitised breakpoints in Hz).
 *
 * Fixed-point structures (xBaseWIN, dxWIN, invDxWIN_q28, aQWIN*, …) are built
 * separately afterward under dual-build via precomputeCoefficients().
 */
#ifdef USE_FLOAT_AMP_COMP
static void precomputeCoefficients_float() {
  // Ensure each table has a final point at the defined maximum in Hz.
  for (int j = 0; j < NUM_OSCILLATORS; ++j) {
    ampCompFrequencyHz[j][ampCompTableSize] = (float)AMP_COMP_MAX_HZ;
    ampCompArray[j][ampCompTableSize]       = DIV_COUNTER;
  }

  const double maxFreqHz = (double)AMP_COMP_MAX_HZ;

  for (int j = 0; j < NUM_OSCILLATORS; ++j) {
    bool plateauSeen = false;  // ensure we only capture plateau start once per oscillator

    // Initialise plateau metadata for this oscillator in the float/Hz domain.
    // We'll fill these as soon as we detect the first real DIV_COUNTER point
    // below AMP_COMP_MAX_HZ.
    plateauStartIndex[j]  = -1;
    plateauStartFreqHz[j] = (float)AMP_COMP_MAX_HZ;

    for (int i = 0; i < ampCompTableSize - 1; ++i) {
      // Work directly in Hz domain using the float frequency table.
      double x0_f = (double)ampCompFrequencyHz[j][i];
      double x1_f = (double)ampCompFrequencyHz[j][i + 1];
      double x2_f = (double)ampCompFrequencyHz[j][i + 2];
      double y0_f = (double)ampCompArray[j][i];
      double y1_f = (double)ampCompArray[j][i + 1];
      double y2_f = (double)ampCompArray[j][i + 2];

      // Clamp any out-of-band/sentinel frequencies to the defined maximum.
      if (x1_f >= maxFreqHz) x1_f = maxFreqHz;
      if (x2_f >= maxFreqHz) x2_f = maxFreqHz;

      // Plateau smoothing: if both subsequent points are at/beyond full scale,
      // synthesise a smooth transition into the plateau and update the tables.
      if (y1_f >= DIV_COUNTER && y2_f >= DIV_COUNTER) {
        // Capture the plateau start from the *raw* calibration point before
        // we modify/smooth the table. Use the first full-scale breakpoint
        // strictly below the float-domain maximum.
        if (!plateauSeen &&
            plateauStartIndex[j] < 0 &&
            ampCompFrequencyHz[j][i + 1] < (float)maxFreqHz) {
          plateauStartIndex[j]  = i + 1;
          plateauStartFreqHz[j] = ampCompFrequencyHz[j][i + 1];
          plateauSeen = true;
        }

        double plateau_end_y = (double)DIV_COUNTER;
        x1_f = (x0_f + maxFreqHz) * 0.5;
        y1_f = (y0_f + plateau_end_y) * 0.5;
        x2_f = maxFreqHz;
        y2_f = plateau_end_y;

        ampCompFrequencyHz[j][i + 1] = (float)x1_f;
        ampCompArray[j][i + 1]       = (int32_t)llround(y1_f);
        ampCompFrequencyHz[j][i + 2] = (float)maxFreqHz;
        ampCompArray[j][i + 2]       = (int32_t)DIV_COUNTER;
      }

      // Compute float quadratic coefficients y = a*x^2 + b*x + c in Hz domain.
      long double denom_ld =
        (long double)(x0_f - x1_f) * (long double)(x0_f - x2_f) * (long double)(x1_f - x2_f);
      if (denom_ld == 0.0L) denom_ld = 1.0L;
      long double inv_denom_ld = 1.0L / denom_ld;

      long double aVal_ld = ((long double)x2_f * (long double)(y1_f - y0_f) +
                             (long double)x1_f * (long double)(y0_f - y2_f) +
                             (long double)x0_f * (long double)(y2_f - y1_f)) * inv_denom_ld;
      long double bVal_ld = ((long double)x2_f * (long double)x2_f * (long double)(y0_f - y1_f) +
                             (long double)x1_f * (long double)x1_f * (long double)(y2_f - y0_f) +
                             (long double)x0_f * (long double)x0_f * (long double)(y1_f - y2_f)) * inv_denom_ld;
      long double cVal_ld = ((long double)x1_f * (long double)x2_f * (long double)(x1_f - x2_f) * (long double)y0_f +
                             (long double)x2_f * (long double)x0_f * (long double)(x2_f - x0_f) * (long double)y1_f +
                             (long double)x0_f * (long double)x1_f * (long double)(x0_f - x1_f) * (long double)y2_f) * inv_denom_ld;

      aCoeff[j][i] = (float)aVal_ld;
      bCoeff[j][i] = (float)bVal_ld;
      cCoeff[j][i] = (float)cVal_ld;

      // Store the left breakpoint in Hz for this window (kept in sync with any smoothing).
      ampCompFrequencyHz[j][i] = (float)x0_f;
    }

    // The final breakpoint (ampCompTableSize) in Hz has already been
    // initialised to AMP_COMP_MAX_HZ.
  }
}
#endif  // USE_FLOAT_AMP_COMP

// Lookups implemented in voices.ino
uint16_t get_chan_level_lookup_fast(int32_t x, uint8_t voiceN);
#ifdef USE_FLOAT_AMP_COMP
uint16_t get_chan_level_float_quad(float freqHz, uint8_t voiceN);
uint16_t get_chan_level_lut(float freqHz, uint8_t voiceN);
#endif

#ifdef USE_FLOAT_AMP_COMP
// Fill dense LUT from float quadratic (ampWinCache reset after fill in precompute).
static inline void fill_amp_comp_lut_from_quad() {
  for (uint8_t o = 0; o < NUM_OSCILLATORS; ++o) {
    for (int32_t hz = 0; hz <= AMP_COMP_MAX_HZ; ++hz) {
      ampCompLut[o][hz] = get_chan_level_float_quad((float)hz, o);
    }
  }
}

// After float sanitize, copy breakpoints into Q8 arrays for fixed precompute.
static inline void amp_comp_seed_fixed_from_float_tables() {
  const double freqScale = (double)(1u << FREQ_FRAC_BITS);
  for (int j = 0; j < NUM_OSCILLATORS; ++j) {
    for (int i = 0; i <= ampCompTableSize; ++i) {
      double hz = (double)ampCompFrequencyHz[j][i];
      ampCompFrequencyArray[j][i] = (int32_t)llround(hz * freqScale);
    }
  }
}
#endif

// ---------------------------------------------------------------------------
// Engine-agnostic amp-comp API
// ---------------------------------------------------------------------------
// These helpers let the rest of the code call a single precompute/lookup API,
// while the concrete implementation is selected at compile time / runtime method.

// Dispatch to the correct precompute routine based on the active amp-comp mode.
// Under USE_FLOAT_AMP_COMP: float sanitize + coeffs, then seed Q8 + fixed tables
// for FIXED, restore float coeffs/plateau/levels, then fill LUT from float quad.
static inline void precompute_amp_comp_for_engine() {
#ifdef USE_FLOAT_AMP_COMP
  precomputeCoefficients_float();

  // Preserve float coeffs / plateau / levels. Fixed build must not steal float
  // plateauStartIndex or re-derive plateauStartFreqQ from the float index into
  // a rewritten Q row (that caused FIXED to clamp to DIV_COUNTER almost everywhere).
  float aSave[NUM_OSCILLATORS][ampCompTableSize - 1];
  float bSave[NUM_OSCILLATORS][ampCompTableSize - 1];
  float cSave[NUM_OSCILLATORS][ampCompTableSize - 1];
  int16_t plateauIdxSave[NUM_OSCILLATORS];
  float plateauHzSave[NUM_OSCILLATORS];
  int32_t ampSave[NUM_OSCILLATORS][ampCompTableSize + 1];
  int32_t plateauQSave[NUM_OSCILLATORS];

  memcpy(aSave, aCoeff, sizeof(aSave));
  memcpy(bSave, bCoeff, sizeof(bSave));
  memcpy(cSave, cCoeff, sizeof(cSave));
  memcpy(plateauIdxSave, plateauStartIndex, sizeof(plateauIdxSave));
  memcpy(plateauHzSave, plateauStartFreqHz, sizeof(plateauHzSave));
  memcpy(ampSave, ampCompArray, sizeof(ampSave));

  amp_comp_seed_fixed_from_float_tables();
  // Seeded breakpoints are already float-sanitized; do not plateau-rewrite again.
  precomputeCoefficients(/*rewritePlateaus=*/false);
  memcpy(plateauQSave, plateauStartFreqQ, sizeof(plateauQSave));

  // Restore float-domain state for FLOAT_QUAD / LUT fill / live float path.
  memcpy(aCoeff, aSave, sizeof(aSave));
  memcpy(bCoeff, bSave, sizeof(bSave));
  memcpy(cCoeff, cSave, sizeof(cSave));
  memcpy(plateauStartIndex, plateauIdxSave, sizeof(plateauIdxSave));
  memcpy(plateauStartFreqHz, plateauHzSave, sizeof(plateauHzSave));
  memcpy(ampCompArray, ampSave, sizeof(ampSave));
  // FIXED early-out uses plateauStartFreqQ only (see get_chan_level_lookup_fast).
  memcpy(plateauStartFreqQ, plateauQSave, sizeof(plateauQSave));

  fill_amp_comp_lut_from_quad();
  for (int o = 0; o < NUM_OSCILLATORS; ++o) ampWinCache[o] = -1;
#else
  precomputeCoefficients(/*rewritePlateaus=*/true);
#endif
}

// Unified lookup facade: always take frequency in Hz.
// The concrete implementations live in voices.ino; we only provide the wrapper
// and prototypes here. Under USE_FLOAT_AMP_COMP, dispatch on amp_comp_method.

#ifdef USE_FLOAT_AMP_COMP
static inline uint16_t get_chan_level_by_method(float freqHz, uint8_t voiceN) {
  switch (amp_comp_method) {
    case AMP_COMP_LUT:
      return get_chan_level_lut(freqHz, voiceN);
    case AMP_COMP_FIXED: {
      if (freqHz <= 0.0f) return 0;
      if (freqHz >= (float)AMP_COMP_MAX_HZ) {
        return get_chan_level_lookup_fast(AMP_COMP_MAX_HZ_Q, voiceN);
      }
      int32_t x_q = (int32_t)lrintf(freqHz * (float)(1 << FREQ_FRAC_BITS));
      return get_chan_level_lookup_fast(x_q, voiceN);
    }
    case AMP_COMP_FLOAT_QUAD:
    default:
      return get_chan_level_float_quad(freqHz, voiceN);
  }
}

// Back-compat name: previously the float-quad body; now dispatches on method.
static inline uint16_t get_chan_level_float(float freqHz, uint8_t voiceN) {
  return get_chan_level_by_method(freqHz, voiceN);
}
#endif

static inline uint16_t get_chan_level_for_engine(float freqHz, uint8_t voiceN) {
#ifdef USE_FLOAT_AMP_COMP
  // Float amp-comp: delegate to the active method (FIXED by default).
  return get_chan_level_by_method(freqHz, voiceN);
#else
  // Fixed-point amp-comp: convert Hz to Q(FREQ_FRAC_BITS) and call fast lookup.
  if (freqHz <= 0.0f) return 0;
  if (freqHz >= (float)AMP_COMP_MAX_HZ) {
    return get_chan_level_lookup_fast(AMP_COMP_MAX_HZ_Q, voiceN);
  }

  int32_t x_q = (int32_t)lrintf(freqHz * (float)(1 << FREQ_FRAC_BITS));
  return get_chan_level_lookup_fast(x_q, voiceN);
#endif
}

// Bench / accuracy (amp_comp_bench.ino); no-ops unless AMP_COMP_BENCHMARK + RUNNING_AVERAGE
extern volatile bool amp_comp_bench_speed_pending;
extern volatile bool amp_comp_bench_accuracy_pending;
void print_amp_comp_bench();
void amp_comp_bench_run_speed();
void amp_comp_bench_run_accuracy();

#endif
