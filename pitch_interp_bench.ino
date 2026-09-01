#include "include_all.h"
#include <math.h>

// Pitch-interpolator speed / accuracy benches. Self-contained: private tables and
// interpolators (does not touch live voice storage). Needs RUNNING_AVERAGE only for
// paced bench_out_* TX. Debug cmds 28 / 29.

#if defined(RUNNING_AVERAGE)

volatile bool pitch_interp_bench_speed_pending = false;
volatile bool pitch_interp_bench_accuracy_pending = false;

enum : uint8_t {
  PITCH_BENCH_FLOAT_LEGACY = 0,     // legacy walk + bsearch (pure math reference)
  PITCH_BENCH_FLOAT_BRANCHLESS,     // new mapping approach (matches live FLOAT_FAST)
  PITCH_BENCH_FLOAT_CACHED,         // trunc+clamp±1 find (matches live FLOAT_CACHED)
  PITCH_BENCH_RATIO_Q16_BRANCHLESS, // new native mapping (matches live RATIO_Q16)
  PITCH_BENCH_Q12,                  // old cached Q12 method
  PITCH_BENCH_METHODS,              // speed rows count
  PITCH_BENCH_Q20_REF = 6           // accuracy-only private int reference
};

static constexpr int PIB_SIZE = 200;
static constexpr int32_t PIB_SCALE = 10000;
// One osc is enough to rank methods (per-DCO cache is identical). Cuts wall time ~3×.
static constexpr int PIB_BENCH_OSCS = 1;

// Private tables (filled once on first bench run).
static float   pib_xF[PIB_SIZE];
static float   pib_yF[PIB_SIZE];
static float   pib_slopeF[PIB_SIZE - 1];
static int32_t pib_x[PIB_SIZE];           // Q12 ×10000 table-units
static int32_t pib_y[PIB_SIZE];
static int32_t pib_slopeQ12[PIB_SIZE - 1];
static int32_t pib_xQ16[PIB_SIZE];        // native Q16 (1.0 = 65536)
static int32_t pib_yQ16[PIB_SIZE];
static int32_t pib_slopeQ16[PIB_SIZE - 1];
static int32_t pib_slopeQ20[PIB_SIZE - 1]; // accuracy oracle on native Q16 knots
static int16_t pib_cache[NUM_OSCILLATORS];
static bool    pib_ready = false;

// Modifier-domain grid: -3.0f to 5.0f (8.0 Octaves) matches live engine
static constexpr float PITCH_BENCH_MOD_MIN = -3.0f;
static constexpr float PITCH_BENCH_MOD_MAX = 5.0f;
static constexpr float PITCH_BENCH_MOD_STEP = 0.0001f;       // seq (+ accuracy)
static constexpr float PITCH_BENCH_MOD_STEP_JUMP = 0.05f;

// Fixed-voice Q12 speed grid: ×10000 table-units.
static constexpr int32_t PIB_XINT_MIN = -3 * PIB_SCALE;    // -30000
static constexpr int32_t PIB_XINT_MAX = 5 * PIB_SCALE;     //  50000
static constexpr int32_t PIB_XINT_STEP_SEQ = 1;
static constexpr int32_t PIB_XINT_STEP_JUMP =
    (int32_t)(PITCH_BENCH_MOD_STEP_JUMP * (float)PIB_SCALE + 0.5f); // 500

// Fixed-voice RATIO speed grid: native xQ16 (live clamp domain).
static constexpr int32_t PIB_XQ16_MIN = -196608;   // -3.0
static constexpr int32_t PIB_XQ16_MAX = 327680;    //  5.0
static constexpr int32_t PIB_XQ16_STEP_SEQ = 7;     // ≈0.0001
static constexpr int32_t PIB_XQ16_STEP_JUMP = 3277; // ≈0.05

// |cents| histogram for p50/p95/p99 (linear bins over 0..HIST_MAX).
static constexpr int PIB_HIST_BINS = 64;
static constexpr double PIB_HIST_MAX_CENTS = 2.0;

// =============================================================================
// FLOAT TO STRING FORMATTER (Replaces stripped newlib %f printf)
// =============================================================================
static void pib_fmt_f(char *out, size_t sz, double val, int decimals, int width = 0) {
  if (sz == 0) return;
  if (isnan(val)) {
    snprintf(out, sz, "%*s", width, "NaN");
    return;
  }
  if (isinf(val)) {
    snprintf(out, sz, "%*s", width, val < 0 ? "-Inf" : "Inf");
    return;
  }

  bool neg = (val < 0.0);
  double abs_v = fabs(val);
  
  static const int64_t kPows10[] = {1, 10, 100, 1000, 10000, 100000, 1000000};
  int64_t mult = (decimals >= 0 && decimals <= 6) ? kPows10[decimals] : 100;
  
  int64_t total = (int64_t)llround(abs_v * (double)mult);
  if (total == 0) neg = false; // Prevent -0.00
  
  int64_t whole = total / mult;
  int64_t frac = total % mult;
  
  char raw[32];
  if (decimals > 0) {
    snprintf(raw, sizeof(raw), "%s%lld.%0*lld", neg ? "-" : "", (long long)whole, decimals, (long long)frac);
  } else {
    snprintf(raw, sizeof(raw), "%s%lld", neg ? "-" : "", (long long)whole);
  }

  if (width > 0) {
    snprintf(out, sz, "%*s", width, raw);
  } else {
    snprintf(out, sz, "%s", raw);
  }
}

static const char *pitch_bench_method_name(uint8_t m) {
  switch (m) {
    case PITCH_BENCH_FLOAT_LEGACY:         return "FLOAT_WALK";
    case PITCH_BENCH_FLOAT_BRANCHLESS:     return "FLOAT_NEW";
    case PITCH_BENCH_FLOAT_CACHED:         return "FLOAT_CACHED";
    case PITCH_BENCH_RATIO_Q16_BRANCHLESS: return "Q16_NEW";
    case PITCH_BENCH_Q12:                  return "Q12_CACHED";
    default:                               return "?";
  }
}

static const char *pitch_bench_live_mode_name() {
#if PITCH_INTERP_MODE == PITCH_INTERP_FLOAT
  return "FLOAT";
#elif PITCH_INTERP_MODE == PITCH_INTERP_FLOAT_CACHED
  return "FLOAT_CACHED";
#elif PITCH_INTERP_MODE == PITCH_INTERP_RATIO_Q16
  return "RATIO_Q16";
#elif PITCH_INTERP_MODE == PITCH_INTERP_Q12
  return "Q12";
#else
  return "?";
#endif
}

static void pib_reset_cache() {
  for (int d = 0; d < NUM_OSCILLATORS; ++d) pib_cache[d] = -1;
}

static void pib_ensure_tables() {
  if (pib_ready) return;

  // New Span: 8.0 Octaves (-3.0 to 5.0)
  double fraction = 8.00 / (double)(PIB_SIZE - 1);
  for (int i = 0; i < PIB_SIZE; i++) {
    double x;
    float y_value;
    if (i == 0) {
      x = -3.00;
      y_value = 0.0625f; // 2^(-4)
    } else if (i == PIB_SIZE - 1) {
      x = 5.0;
      y_value = 16.0f;   // 2^(4)
    } else {
      x = -3.00 + (fraction * (double)i);
      y_value = (float)expInterpolationSolveY(x + 1.00, 1.00, 3.00, 0.50, 2.00);
    }
    
    pib_xF[i] = (float)x;
    pib_yF[i] = y_value;
    pib_x[i]  = (int32_t)(x * (double)PIB_SCALE);
    pib_y[i]  = (int32_t)(y_value * (double)PIB_SCALE);
    pib_xQ16[i] = (int32_t)(x * 65536.0 + (x >= 0.0 ? 0.5 : -0.5));
    pib_yQ16[i] = (int32_t)((double)y_value * 65536.0 + 0.5);
  }

  for (int i = 0; i < PIB_SIZE - 1; ++i) {
    int32_t dx12 = pib_x[i + 1] - pib_x[i];
    if (dx12 == 0) dx12 = 1;
    int32_t dy12 = pib_y[i + 1] - pib_y[i];
    pib_slopeQ12[i] = (int32_t)((((int64_t)dy12 << 12) + (dx12 > 0 ? dx12 / 2 : -dx12 / 2)) / (int64_t)dx12);

    int32_t dx16 = pib_xQ16[i + 1] - pib_xQ16[i];
    if (dx16 == 0) dx16 = 1;
    int32_t dy16 = pib_yQ16[i + 1] - pib_yQ16[i];
    pib_slopeQ16[i] = (int32_t)((((int64_t)dy16 << 16) + (dx16 > 0 ? dx16 / 2 : -dx16 / 2)) / (int64_t)dx16);
    pib_slopeQ20[i] = (int32_t)((((int64_t)dy16 << 20) + (dx16 > 0 ? dx16 / 2 : -dx16 / 2)) / (int64_t)dx16);

    float dxF = pib_xF[i + 1] - pib_xF[i];
    if (dxF == 0.0f) dxF = 1.0f;
    pib_slopeF[i] = (pib_yF[i + 1] - pib_yF[i]) / dxF;
  }

  pib_reset_cache();
  pib_ready = true;
}

// Bounded integer segment finder (safe against out-of-bounds indexing)
static int pib_find_seg_int(const int32_t *xTab, int32_t x, int dcoIndex) {
  const int lastSeg = PIB_SIZE - 2;
  int low = pib_cache[dcoIndex];
  
  if (low < 0 || low > lastSeg || !(xTab[low] <= x && x < xTab[low + 1])) {
    if (low >= 0 && low <= lastSeg) {
      if (x >= xTab[low + 1]) {
        while (low < lastSeg && x >= xTab[low + 1]) low++;
      } else if (x < xTab[low]) {
        while (low > 0 && x < xTab[low]) low--;
      }
    }
    if (!(low >= 0 && low <= lastSeg && xTab[low] <= x && x < xTab[low + 1])) {
      int l = 0, h = lastSeg;
      while (l <= h) {
        int m = (l + h) >> 1;
        if (xTab[m] <= x && x < xTab[m + 1]) {
          low = m;
          break;
        } else if (x < xTab[m]) {
          h = m - 1;
        } else {
          l = m + 1;
        }
      }
      if (low < 0) low = 0;
      if (low > lastSeg) low = lastSeg;
    }
    pib_cache[dcoIndex] = (int16_t)low;
  }
  return low;
}

// -----------------------------------------------------------------------------
// INTERPOLATION METHODS
// -----------------------------------------------------------------------------

// 1. LEGACY FLOAT (Walk + Bsearch)
static float pib_interp_float_legacy(float modifier, int dcoIndex) {
  if (modifier <= pib_xF[0]) return pib_yF[0];
  if (modifier >= pib_xF[PIB_SIZE - 1]) return pib_yF[PIB_SIZE - 1];

  const int lastSeg = PIB_SIZE - 2;
  int low = pib_cache[dcoIndex];
  if (low < 0 || low > lastSeg || !(pib_xF[low] <= modifier && modifier < pib_xF[low + 1])) {
    if (low >= 0 && low <= lastSeg) {
      if (modifier >= pib_xF[low + 1]) {
        while (low < lastSeg && modifier >= pib_xF[low + 1]) ++low;
      } else if (modifier < pib_xF[low]) {
        while (low > 0 && modifier < pib_xF[low]) --low;
      }
    }
    if (!(low >= 0 && low <= lastSeg && pib_xF[low] <= modifier && modifier < pib_xF[low + 1])) {
      int l = 0, h = lastSeg;
      while (l <= h) {
        int m = (l + h) >> 1;
        if (pib_xF[m] <= modifier && modifier < pib_xF[m + 1]) {
          low = m;
          break;
        } else if (modifier < pib_xF[m]) {
          h = m - 1;
        } else {
          l = m + 1;
        }
      }
      if (low < 0) low = 0;
      if (low > lastSeg) low = lastSeg;
    }
    pib_cache[dcoIndex] = (int16_t)low;
  }
  return pib_yF[low] + pib_slopeF[low] * (modifier - pib_xF[low]);
}

// 2. NEW BRANCHLESS FLOAT (Mirrors live interpolateRatioFloat_fast)
__attribute__((noinline))
static float pib_interp_float_branchless(float x) {
  static constexpr float MIN_X = -3.0f;
  static constexpr float MAX_X = 5.0f;
  static constexpr float SCALE = (float)(PIB_SIZE - 1) / 8.0f;

  float clamped_x = __builtin_fmaxf(MIN_X, __builtin_fminf(x, MAX_X));
  float mapped_idx = (clamped_x - MIN_X) * SCALE;
  uint32_t idx = (uint32_t)mapped_idx;
  
  const uint32_t MAX_IDX = PIB_SIZE - 2;
  idx = (idx >= MAX_IDX) ? MAX_IDX : idx;
  
  float frac = mapped_idx - (float)idx;
  float y0 = pib_yF[idx];
  float y1 = pib_yF[idx + 1];
  
  return __builtin_fmaf(y1 - y0, frac, y0);
}

// 3. UPDATED FLOAT CACHED (Mirrors live interpolateRatioFloat_cached_fast)
__attribute__((noinline))
static float pib_interp_float_cached(float x, int dcoIndex) {
  if (__builtin_expect(x <= -3.0f, 0)) {
    return pib_yF[0];
  }
  if (__builtin_expect(x >= 5.0f, 0)) {
    return pib_yF[PIB_SIZE - 1];
  }

  static constexpr float kPitchInvDx = (float)(PIB_SIZE - 1) / 8.0f;
  const uint32_t lastSeg = PIB_SIZE - 2;

  uint32_t low = (uint32_t)pib_cache[dcoIndex];
  float x_low;

  if (__builtin_expect(low <= lastSeg && x >= (x_low = pib_xF[low]) && x < pib_xF[low + 1], 1)) {
    // cache hit
  } else {
    uint32_t cand = (uint32_t)((x + 3.0f) * kPitchInvDx);
    if (cand > lastSeg) cand = lastSeg;

    float c_next_x = pib_xF[cand + 1];
    if (cand < lastSeg && x >= c_next_x) {
      ++cand;
      x_low = c_next_x;
    } else {
      float c_x = pib_xF[cand];
      if (cand > 0 && x < c_x) {
        --cand;
        x_low = pib_xF[cand];
      } else {
        x_low = c_x;
      }
    }
    low = cand;
    pib_cache[dcoIndex] = (int16_t)low;
  }
  return __builtin_fmaf(pib_slopeF[low], x - x_low, pib_yF[low]);
}

// 4. NEW BRANCHLESS RATIO Q16 (Mirrors live interpolateRatioQ16_fast)
__attribute__((noinline))
static int32_t pib_interp_ratio_q16_branchless(int32_t xQ16) {
  static constexpr int32_t MIN_X_Q16 = -196608; // -3.0
  static constexpr int32_t MAX_X_Q16 =  327680; // 5.0

  xQ16 = (xQ16 < MIN_X_Q16) ? MIN_X_Q16 : xQ16;
  xQ16 = (xQ16 > MAX_X_Q16) ? MAX_X_Q16 : xQ16;

  uint32_t phase = (uint32_t)(xQ16 - MIN_X_Q16);
  uint32_t mapped = (phase * (PIB_SIZE - 1)) >> 3;
  
  uint32_t idx = mapped >> 16;
  int32_t frac = mapped & 0xFFFF;
  
  const uint32_t MAX_IDX = PIB_SIZE - 2;
  idx = (idx > MAX_IDX) ? MAX_IDX : idx;

  int32_t y0 = pib_yQ16[idx];
  int32_t y1 = pib_yQ16[idx + 1];
  
  return y0 + (((y1 - y0) * frac + 32768) >> 16);
}

// 5. CACHED Q12 (64-bit safe: eliminates 32-bit integer overflow at +5.0 octaves)
static int32_t pib_interp_y_q12(int64_t xQ16, int dcoIndex) {
  int32_t xInt = (int32_t)(xQ16 >> 16);
  if (xInt <= pib_x[0]) return pib_y[0];
  if (xInt >= pib_x[PIB_SIZE - 1]) return pib_y[PIB_SIZE - 1];
  
  int low = pib_find_seg_int(pib_x, xInt, dcoIndex);
  int32_t deltaQ12 = (int32_t)((xQ16 - ((int64_t)pib_x[low] << 16)) >> 4);
  return pib_y[low] + (int32_t)((((int64_t)deltaQ12 * (int64_t)pib_slopeQ12[low]) + (1LL << 23)) >> 24);
}

// Non-live accuracy oracle: Q20 lerp on native Q16 knots.
static int32_t pib_interp_y_q20(int32_t xQ16, int dcoIndex) {
  static constexpr int32_t kPitchX0_Q16 = -196608;
  static constexpr int32_t kPitchX1_Q16 = 327680;
  
  if (xQ16 <= kPitchX0_Q16) return pib_yQ16[0];
  if (xQ16 >= kPitchX1_Q16) return pib_yQ16[PIB_SIZE - 1];
  
  int low = pib_find_seg_int(pib_xQ16, xQ16, dcoIndex);
  int32_t delta = xQ16 - pib_xQ16[low];
  return pib_yQ16[low] +
         (int32_t)((((int64_t)delta * (int64_t)pib_slopeQ20[low]) + (1LL << 19)) >> 20);
}

// Live fixed-voice yTab → ratioQ16 (same reciprocal as voices.ino vt_freq_scale_post).
static inline int32_t pib_y_to_ratio_q16(int32_t yTab) {
  uint64_t num = ((uint64_t)(uint32_t)yTab << 16) + 5000u;
  return (int32_t)((num * 0xD1B71759ULL) >> 45);
}

static float pitch_bench_call(uint8_t method, float modifier, int dco) {
  switch (method) {
    case PITCH_BENCH_FLOAT_LEGACY:
      return pib_interp_float_legacy(modifier, dco);
    case PITCH_BENCH_FLOAT_BRANCHLESS:
      return pib_interp_float_branchless(modifier);
    case PITCH_BENCH_FLOAT_CACHED:
      return pib_interp_float_cached(modifier, dco);
    case PITCH_BENCH_RATIO_Q16_BRANCHLESS: {
      int32_t xQ16 = (int32_t)lroundf(modifier * 65536.0f);
      return (float)pib_interp_ratio_q16_branchless(xQ16) * (1.0f / 65536.0f);
    }
    case PITCH_BENCH_Q12: {
      double xTab = (double)modifier * (double)PIB_SCALE;
      int64_t xQ16 = (int64_t)llround(xTab * 65536.0);
      return (float)pib_interp_y_q12(xQ16, dco) / (float)PIB_SCALE;
    }
    case PITCH_BENCH_Q20_REF: {
      int32_t xQ16 = (int32_t)lroundf(modifier * 65536.0f);
      return (float)pib_interp_y_q20(xQ16, dco) * (1.0f / 65536.0f);
    }
    default:
      return 1.0f;
  }
}

// Fast Single-Precision Cents Delta (100x faster than double log2)
static inline double pib_cents_abs(float cand, float ref) {
  if (ref <= 1e-12f || cand <= 1e-12f) return 0.0;
  float ratio = cand / ref;
  return (double)fabsf(1200.0f * log2f(ratio));
}

static bool pib_is_knot(float modifier) {
  int32_t xi = (int32_t)lroundf(modifier * 65536.0f);
  int l = 0, h = PIB_SIZE - 1;
  while (l <= h) {
    int m = (l + h) >> 1;
    if (pib_xQ16[m] == xi) return true;
    if (pib_xQ16[m] < xi) l = m + 1;
    else h = m - 1;
  }
  return false;
}

static int pib_x_band(float modifier) {
  if (modifier < 0.0f) return 0;      // low (-3.0 to 0)
  if (modifier < 2.5f) return 1;      // mid (0 to 2.5)
  return 2;                           // high (2.5 to 5.0)
}

static void pib_hist_add(uint32_t *h, double cents) {
  if (cents < 0.0) cents = 0.0;
  int b = (int)(cents * (double)PIB_HIST_BINS / PIB_HIST_MAX_CENTS);
  if (b < 0) b = 0;
  if (b >= PIB_HIST_BINS) b = PIB_HIST_BINS - 1;
  h[b]++;
}

static double pib_hist_percentile(const uint32_t *h, uint32_t n, double pct) {
  if (n == 0) return 0.0;
  uint64_t target = (uint64_t)((pct / 100.0) * (double)n);
  if (target >= n) target = n - 1u;
  uint64_t cum = 0;
  for (int b = 0; b < PIB_HIST_BINS; ++b) {
    cum += h[b];
    if (cum > target) {
      return ((double)b + 0.5) * PIB_HIST_MAX_CENTS / (double)PIB_HIST_BINS;
    }
  }
  return PIB_HIST_MAX_CENTS;
}

static uint32_t pib_mod_samples_per_osc(float modStep) {
  uint32_t n = 0;
  for (float mod = PITCH_BENCH_MOD_MIN;
       mod <= PITCH_BENCH_MOD_MAX + 0.5f * modStep;
       mod += modStep) {
    ++n;
  }
  return n;
}

static uint32_t pib_q16_samples(int32_t step) {
  uint32_t n = 0;
  for (int32_t x = PIB_XQ16_MIN; x <= PIB_XQ16_MAX; x += step) ++n;
  return n;
}

static void pib_speed_run(uint8_t method, float modStep, int32_t xIntStep, int32_t xQ16Step,
                          uint32_t repeats, uint32_t *outCalls, uint64_t *outUs) {
  pib_reset_cache();
  uint32_t t0 = micros();
  uint32_t calls = 0;

#ifdef USE_FLOAT_VOICE_TASK
  (void)xIntStep;
  (void)xQ16Step;
  volatile float sink = 0.0f;
  static constexpr float Q16_TO_F = 1.0f / 65536.0f;
  for (uint32_t r = 0; r < repeats; ++r) {
    for (uint8_t o = 0; o < PIB_BENCH_OSCS; ++o) {
      for (float mod = PITCH_BENCH_MOD_MIN;
           mod <= PITCH_BENCH_MOD_MAX + 0.5f * modStep;
           mod += modStep) {
        if (method == PITCH_BENCH_FLOAT_LEGACY) {
          sink += pib_interp_float_legacy(mod, o);
        } else if (method == PITCH_BENCH_FLOAT_BRANCHLESS) {
          sink += pib_interp_float_branchless(mod);
        } else if (method == PITCH_BENCH_FLOAT_CACHED) {
          sink += pib_interp_float_cached(mod, o);
        } else if (method == PITCH_BENCH_RATIO_Q16_BRANCHLESS) {
          int32_t xQ16 = (int32_t)lroundf(mod * 65536.0f);
          sink += (float)pib_interp_ratio_q16_branchless(xQ16) * Q16_TO_F;
        } else if (method == PITCH_BENCH_Q12) {
          double xTab = (double)mod * (double)PIB_SCALE;
          int64_t xQ16 = (int64_t)llround(xTab * 65536.0);
          sink += (float)pib_interp_y_q12(xQ16, o) / (float)PIB_SCALE;
        }
        ++calls;
      }
    }
  }
  (void)sink;
#else
  if (method == PITCH_BENCH_FLOAT_LEGACY || method == PITCH_BENCH_FLOAT_BRANCHLESS || method == PITCH_BENCH_FLOAT_CACHED) {
    volatile float sink = 0.0f;
    for (uint32_t r = 0; r < repeats; ++r) {
      for (uint8_t o = 0; o < PIB_BENCH_OSCS; ++o) {
        for (float mod = PITCH_BENCH_MOD_MIN; mod <= PITCH_BENCH_MOD_MAX + 0.5f * modStep; mod += modStep) {
          sink += (method == PITCH_BENCH_FLOAT_LEGACY)     ? pib_interp_float_legacy(mod, o) :
                  (method == PITCH_BENCH_FLOAT_BRANCHLESS) ? pib_interp_float_branchless(mod) :
                                                             pib_interp_float_cached(mod, o);
          ++calls;
        }
      }
    }
    (void)sink;
  } else if (method == PITCH_BENCH_RATIO_Q16_BRANCHLESS) {
    volatile int32_t sink = 0;
    for (uint32_t r = 0; r < repeats; ++r) {
      for (uint8_t o = 0; o < PIB_BENCH_OSCS; ++o) {
        for (int32_t xQ16 = PIB_XQ16_MIN; xQ16 <= PIB_XQ16_MAX; xQ16 += xQ16Step) {
          sink += pib_interp_ratio_q16_branchless(xQ16);
          ++calls;
        }
      }
    }
    (void)sink;
  } else {
    volatile int32_t sink = 0;
    for (uint32_t r = 0; r < repeats; ++r) {
      for (uint8_t o = 0; o < PIB_BENCH_OSCS; ++o) {
        for (int32_t xInt = PIB_XINT_MIN; xInt <= PIB_XINT_MAX; xInt += xIntStep) {
          const int64_t xQ16 = (int64_t)xInt << 16;
          sink += pib_y_to_ratio_q16(pib_interp_y_q12(xQ16, o));
          ++calls;
        }
      }
    }
    (void)sink;
  }
#endif

  uint32_t t1 = micros();
  *outCalls = calls;
  *outUs = (uint64_t)(t1 - t0);
}

static void pib_speed_print_table(const char *pattern, float modStep, int32_t xIntStep,
                                  int32_t xQ16Step, uint32_t repeats,
                                  const uint32_t *totalCalls, const uint64_t *totalUs) {
  double refMeanNs = ((double)totalUs[PITCH_BENCH_FLOAT_LEGACY] * 1000.0) /
                     (double)totalCalls[PITCH_BENCH_FLOAT_LEGACY];
  if (refMeanNs < 1e-9) refMeanNs = 1e-9;

  char s_step[16];
  pib_fmt_f(s_step, sizeof(s_step), (double)modStep, 4);

  bench_out_printf("-- pattern=%s  step=%s repeats=%lu oscs=%d",
                   pattern, s_step, (unsigned long)repeats, PIB_BENCH_OSCS);
#ifndef USE_FLOAT_VOICE_TASK
  bench_out_printf("  xIntStep=%ld xQ16Step=%ld", (long)xIntStep, (long)xQ16Step);
#else
  (void)xIntStep;
  (void)xQ16Step;
#endif
  bench_out_println("");
  bench_out_println("method       calls    totalUs   meanNs  pctVsWalk");
  bench_out_println("------------ -------- --------- -------- ----------");
  for (uint8_t method = 0; method < PITCH_BENCH_METHODS; ++method) {
    double meanNs = ((double)totalUs[method] * 1000.0) / (double)totalCalls[method];
    double pct = 100.0 * meanNs / refMeanNs;

    char s_mean[16], s_pct[16];
    pib_fmt_f(s_mean, sizeof(s_mean), meanNs, 2, 8);
    pib_fmt_f(s_pct, sizeof(s_pct), pct, 1, 10);

    bench_out_printf("%-12s %8lu %9lu %s %s\n",
                     pitch_bench_method_name(method),
                     (unsigned long)totalCalls[method],
                     (unsigned long)totalUs[method],
                     s_mean,
                     s_pct);
  }
}

void pitch_interp_bench_run_speed() {
  pib_ensure_tables();

  const uint32_t seqPerOsc = pib_mod_samples_per_osc(PITCH_BENCH_MOD_STEP);
  const uint32_t jumpPerOsc = pib_mod_samples_per_osc(PITCH_BENCH_MOD_STEP_JUMP);
  uint32_t jumpRepeats = 1;
  if (jumpPerOsc > 0 && seqPerOsc > jumpPerOsc) {
    jumpRepeats = (seqPerOsc + jumpPerOsc - 1u) / jumpPerOsc;
  }
  const uint32_t q16Seq = pib_q16_samples(PIB_XQ16_STEP_SEQ);
  const uint32_t q16Jump = pib_q16_samples(PIB_XQ16_STEP_JUMP);
  uint32_t ratioJumpRepeats = 1;
  if (q16Jump > 0 && q16Seq > q16Jump) {
    ratioJumpRepeats = (q16Seq + q16Jump - 1u) / q16Jump;
  }

  struct Pattern {
    const char *name;
    float modStep;
    int32_t xIntStep;
    int32_t xQ16Step;
    uint32_t repeats;
  };
  const Pattern patterns[2] = {
    {"seq",  PITCH_BENCH_MOD_STEP,      PIB_XINT_STEP_SEQ,  PIB_XQ16_STEP_SEQ,  1u},
    {"jump", PITCH_BENCH_MOD_STEP_JUMP, PIB_XINT_STEP_JUMP, PIB_XQ16_STEP_JUMP, jumpRepeats},
  };

  char s_min[16], s_max[16];
  pib_fmt_f(s_min, sizeof(s_min), (double)PITCH_BENCH_MOD_MIN, 4);
  pib_fmt_f(s_max, sizeof(s_max), (double)PITCH_BENCH_MOD_MAX, 4);

  bench_out_println("=== PITCH INTERP BENCH ===");
  bench_out_printf("live_pitch=%s (private tables; does not touch voice)\n",
                   pitch_bench_live_mode_name());
#ifdef USE_FLOAT_VOICE_TASK
  bench_out_printf("speed=flag-path voice=FLOAT (to float ratio; RATIO lroundf(mod*65536))\n");
#else
  bench_out_printf("speed=flag-path voice=FIXED (RATIO native xQ16; Q12 y->ratio; FLOAT*=ref)\n");
#endif
  bench_out_printf("FLOAT_NEW=branchless index map  Q16_NEW=branchless Q16 map  FLOAT_WALK*=ref\n");
  bench_out_printf("seq=walk-favorable  jump=miss-heavy (live-like under pitch mod)\n");
  bench_out_printf("grid=mod %s..%s (span: 8.0 oct)\n", s_min, s_max);

  for (uint8_t p = 0; p < 2; ++p) {
    uint32_t totalCalls[PITCH_BENCH_METHODS] = {0};
    uint64_t totalUs[PITCH_BENCH_METHODS] = {0};
    for (uint8_t method = 0; method < PITCH_BENCH_METHODS; ++method) {
      uint32_t reps = patterns[p].repeats;
#ifndef USE_FLOAT_VOICE_TASK
      if (method == PITCH_BENCH_RATIO_Q16_BRANCHLESS && p == 1) reps = ratioJumpRepeats;
#else
      (void)ratioJumpRepeats;
#endif
      pib_speed_run(method, patterns[p].modStep, patterns[p].xIntStep,
                    patterns[p].xQ16Step, reps,
                    &totalCalls[method], &totalUs[method]);
    }
    pib_speed_print_table(patterns[p].name, patterns[p].modStep, patterns[p].xIntStep,
                          patterns[p].xQ16Step, patterns[p].repeats, totalCalls, totalUs);
  }
  bench_out_println("=========================");
}

void pitch_interp_bench_run_accuracy() {
  pib_ensure_tables();

  // vs FLOAT_LEGACY (mathematically accurate baseline)
  static const uint8_t kVsFloat[] = {
    PITCH_BENCH_FLOAT_BRANCHLESS, PITCH_BENCH_FLOAT_CACHED, PITCH_BENCH_RATIO_Q16_BRANCHLESS, PITCH_BENCH_Q12
  };
  // vs Q20 private int reference
  static const uint8_t kSlopeCand[] = {
    PITCH_BENCH_FLOAT_LEGACY, PITCH_BENCH_FLOAT_BRANCHLESS, PITCH_BENCH_FLOAT_CACHED, PITCH_BENCH_RATIO_Q16_BRANCHLESS, PITCH_BENCH_Q12
  };
  static constexpr int N_VS_FLOAT = 4;
  static constexpr int N_SLOPE = 5;

  struct Acc {
    double sumCents;
    double maxCents;
    float xAtMax;
    uint32_t n;
    uint32_t worse05;   // > 0.5¢
    uint32_t worse10;   // > 1.0¢
    uint32_t worse001;  // > 0.01¢
    uint32_t worse01;   // > 0.1¢ 
    double sumKnot;
    double sumMid;
    uint32_t nKnot;
    uint32_t nMid;
    uint32_t hist[PIB_HIST_BINS];
    double bandSum[3];
    double bandMax[3];
    uint32_t bandN[3];
  };

  static Acc vsFloat[N_VS_FLOAT];
  static Acc vsQ20[N_SLOPE];
  memset(vsFloat, 0, sizeof(vsFloat));
  memset(vsQ20, 0, sizeof(vsQ20));

  double knotQuantSum = 0.0;
  double knotQuantMax = 0.0;
  uint32_t knotQuantN = 0;

  pib_reset_cache();

  for (uint8_t o = 0; o < PIB_BENCH_OSCS; ++o) {
    for (float mod = PITCH_BENCH_MOD_MIN;
         mod <= PITCH_BENCH_MOD_MAX + 0.5f * PITCH_BENCH_MOD_STEP;
         mod += PITCH_BENCH_MOD_STEP) {
         
      float rFloat = pitch_bench_call(PITCH_BENCH_FLOAT_LEGACY, mod, o);
      float rQ20   = pitch_bench_call(PITCH_BENCH_Q20_REF, mod, o);
      if (!(rFloat > 1e-12f) || !(rQ20 > 1e-12f)) continue;

      const bool atKnot = pib_is_knot(mod);
      const int band = pib_x_band(mod);

      if (atKnot) {
        double cq = pib_cents_abs(rQ20, rFloat);
        knotQuantSum += cq;
        if (cq > knotQuantMax) knotQuantMax = cq;
        knotQuantN++;
      }

      for (int i = 0; i < N_VS_FLOAT; ++i) {
        float y = pitch_bench_call(kVsFloat[i], mod, o);
        double c = pib_cents_abs(y, rFloat);
        Acc &a = vsFloat[i];
        a.sumCents += c;
        a.n++;
        if (c > 0.5) a.worse05++;
        if (c > 1.0) a.worse10++;
        if (c > a.maxCents) {
          a.maxCents = c;
          a.xAtMax = mod;
        }
        pib_hist_add(a.hist, c);
      }

      for (int i = 0; i < N_SLOPE; ++i) {
        float y = pitch_bench_call(kSlopeCand[i], mod, o);
        double c = pib_cents_abs(y, rQ20);
        Acc &a = vsQ20[i];
        a.sumCents += c;
        a.n++;
        if (c > 0.01) a.worse001++;
        if (c > 0.1) a.worse01++;
        if (c > a.maxCents) {
          a.maxCents = c;
          a.xAtMax = mod;
        }
        pib_hist_add(a.hist, c);
        if (atKnot) {
          a.sumKnot += c;
          a.nKnot++;
        } else {
          a.sumMid += c;
          a.nMid++;
        }
        a.bandSum[band] += c;
        a.bandN[band]++;
        if (c > a.bandMax[band]) a.bandMax[band] = c;
      }
    }
  }

  uint32_t nSamples = vsFloat[0].n;

  char s_min_a[16], s_max_a[16], s_step_a[16];
  pib_fmt_f(s_min_a, sizeof(s_min_a), (double)PITCH_BENCH_MOD_MIN, 4);
  pib_fmt_f(s_max_a, sizeof(s_max_a), (double)PITCH_BENCH_MOD_MAX, 4);
  pib_fmt_f(s_step_a, sizeof(s_step_a), (double)PITCH_BENCH_MOD_STEP, 4);

  bench_out_println("=== PITCH INTERP ACCURACY ===");
  bench_out_printf("live_pitch=%s | NEW=branchless array mapping\n",
                   pitch_bench_live_mode_name());
  bench_out_printf("grid=mod %s..%s step=%s oscs=%d | n=%lu\n",
                   s_min_a, s_max_a, s_step_a, PIB_BENCH_OSCS, (unsigned long)nSamples);

  bench_out_println("\n-- Table quantization floor (knot |Q20-FLOAT|) --");
  if (knotQuantN > 0) {
    char s_qmean[16], s_qmax[16];
    double qmean = knotQuantSum / (double)knotQuantN;
    pib_fmt_f(s_qmean, sizeof(s_qmean), qmean, 4);
    pib_fmt_f(s_qmax, sizeof(s_qmax), knotQuantMax, 4);

    bench_out_printf("  mean=%s¢  max=%s¢  n_knots=%lu\n",
                     s_qmean, s_qmax, (unsigned long)knotQuantN);
    bench_out_println("  (int knot y vs float knot y; independent of slope mode)");
  } else {
    bench_out_println("  (no knot samples)");
  }

  bench_out_println("\n-- vs FLOAT_WALK (FLOAT_NEW≈0¢; RATIO/Q12 = table gap) --");
  bench_out_println("method       mean¢    max¢   p50¢   p95¢   p99¢  >0.5¢%  >1.0¢%");
  for (int i = 0; i < N_VS_FLOAT; ++i) {
    Acc &a = vsFloat[i];
    double mean = (a.n > 0) ? (a.sumCents / (double)a.n) : 0.0;
    double p50 = pib_hist_percentile(a.hist, a.n, 50.0);
    double p95 = pib_hist_percentile(a.hist, a.n, 95.0);
    double p99 = pib_hist_percentile(a.hist, a.n, 99.0);
    double pct05 = (a.n > 0) ? (100.0 * (double)a.worse05 / (double)a.n) : 0.0;
    double pct10 = (a.n > 0) ? (100.0 * (double)a.worse10 / (double)a.n) : 0.0;

    char s_mean[16], s_max[16], s_p50[16], s_p95[16], s_p99[16], s_pct05[16], s_pct10[16];
    pib_fmt_f(s_mean, sizeof(s_mean), mean, 3, 6);
    pib_fmt_f(s_max, sizeof(s_max), a.maxCents, 3, 7);
    pib_fmt_f(s_p50, sizeof(s_p50), p50, 3, 6);
    pib_fmt_f(s_p95, sizeof(s_p95), p95, 3, 6);
    pib_fmt_f(s_p99, sizeof(s_p99), p99, 3, 6);
    pib_fmt_f(s_pct05, sizeof(s_pct05), pct05, 2, 6);
    pib_fmt_f(s_pct10, sizeof(s_pct10), pct10, 2, 6);

    bench_out_printf("%-12s %s %s %s %s %s %s %s\n",
                     pitch_bench_method_name(kVsFloat[i]),
                     s_mean, s_max, s_p50, s_p95, s_p99, s_pct05, s_pct10);
  }

  bench_out_println("\n-- vs Q20 ref (native Q16 knots; FLOAT*≈table gap) --");
  bench_out_println("method       mean¢    max¢   p50¢   p95¢   p99¢  >0.01¢% >0.1¢%");
  for (int i = 0; i < N_SLOPE; ++i) {
    Acc &a = vsQ20[i];
    double mean = (a.n > 0) ? (a.sumCents / (double)a.n) : 0.0;
    double p50 = pib_hist_percentile(a.hist, a.n, 50.0);
    double p95 = pib_hist_percentile(a.hist, a.n, 95.0);
    double p99 = pib_hist_percentile(a.hist, a.n, 99.0);
    double pct001 = (a.n > 0) ? (100.0 * (double)a.worse001 / (double)a.n) : 0.0;
    double pct01 = (a.n > 0) ? (100.0 * (double)a.worse01 / (double)a.n) : 0.0;

    char s_mean[16], s_max[16], s_p50[16], s_p95[16], s_p99[16], s_pct001[16], s_pct01[16];
    pib_fmt_f(s_mean, sizeof(s_mean), mean, 4, 6);
    pib_fmt_f(s_max, sizeof(s_max), a.maxCents, 4, 7);
    pib_fmt_f(s_p50, sizeof(s_p50), p50, 4, 6);
    pib_fmt_f(s_p95, sizeof(s_p95), p95, 4, 6);
    pib_fmt_f(s_p99, sizeof(s_p99), p99, 4, 6);
    pib_fmt_f(s_pct001, sizeof(s_pct001), pct001, 3, 7);
    pib_fmt_f(s_pct01, sizeof(s_pct01), pct01, 3, 6);

    bench_out_printf("%-12s %s %s %s %s %s %s %s\n",
                     pitch_bench_method_name(kSlopeCand[i]),
                     s_mean, s_max, s_p50, s_p95, s_p99, s_pct001, s_pct01);
  }

  bench_out_println("knot vs mid mean¢ (slope error should appear in mid):");
  for (int i = 0; i < N_SLOPE; ++i) {
    Acc &a = vsQ20[i];
    double mk = (a.nKnot > 0) ? (a.sumKnot / (double)a.nKnot) : 0.0;
    double mm = (a.nMid > 0) ? (a.sumMid / (double)a.nMid) : 0.0;

    char s_mk[16], s_mm[16], s_maxmod[16];
    pib_fmt_f(s_mk, sizeof(s_mk), mk, 5);
    pib_fmt_f(s_mm, sizeof(s_mm), mm, 5);
    pib_fmt_f(s_maxmod, sizeof(s_maxmod), (double)a.xAtMax, 4);

    bench_out_printf("  %-12s knot=%s¢ (n=%lu)  mid=%s¢ (n=%lu)  max@mod=%s\n",
                     pitch_bench_method_name(kSlopeCand[i]),
                     s_mk, (unsigned long)a.nKnot, s_mm, (unsigned long)a.nMid,
                     s_maxmod);
  }

  bench_out_println("\n-- vs Q20 ref by mod-band (low<0, mid<2.5, high) --");
  bench_out_println("method       low mean/max¢     mid mean/max¢     high mean/max¢");
  for (int i = 0; i < N_SLOPE; ++i) {
    Acc &a = vsQ20[i];
    double m0 = (a.bandN[0] > 0) ? (a.bandSum[0] / (double)a.bandN[0]) : 0.0;
    double m1 = (a.bandN[1] > 0) ? (a.bandSum[1] / (double)a.bandN[1]) : 0.0;
    double m2 = (a.bandN[2] > 0) ? (a.bandSum[2] / (double)a.bandN[2]) : 0.0;

    char s_m0[16], s_mx0[16], s_m1[16], s_mx1[16], s_m2[16], s_mx2[16];
    pib_fmt_f(s_m0, sizeof(s_m0), m0, 4, 6);
    pib_fmt_f(s_mx0, sizeof(s_mx0), a.bandMax[0], 4, 6);
    pib_fmt_f(s_m1, sizeof(s_m1), m1, 4, 6);
    pib_fmt_f(s_mx1, sizeof(s_mx1), a.bandMax[1], 4, 6);
    pib_fmt_f(s_m2, sizeof(s_m2), m2, 4, 6);
    pib_fmt_f(s_mx2, sizeof(s_mx2), a.bandMax[2], 4, 6);

    bench_out_printf("%-12s %s/%s   %s/%s   %s/%s\n",
                     pitch_bench_method_name(kSlopeCand[i]),
                     s_m0, s_mx0, s_m1, s_mx1, s_m2, s_mx2);
  }

  bench_out_println("============================");
}

void print_pitch_interp_bench() {
  if (pitch_interp_bench_speed_pending) {
    pitch_interp_bench_speed_pending = false;
    pitch_interp_bench_run_speed();
  }
  if (pitch_interp_bench_accuracy_pending) {
    pitch_interp_bench_accuracy_pending = false;
    pitch_interp_bench_run_accuracy();
  }
}

#else  // !RUNNING_AVERAGE

volatile bool pitch_interp_bench_speed_pending = false;
volatile bool pitch_interp_bench_accuracy_pending = false;

void pitch_interp_bench_run_speed() {}
void pitch_interp_bench_run_accuracy() {}
void print_pitch_interp_bench() {}

#endif