#include "include_all.h"

// Pitch-interpolator speed / accuracy benches. Self-contained: private tables and
// interpolators (does not touch live voice storage). Needs RUNNING_AVERAGE only for
// paced bench_out_* TX. Debug cmds 28 / 29.

#if defined(RUNNING_AVERAGE)

volatile bool pitch_interp_bench_speed_pending = false;
volatile bool pitch_interp_bench_accuracy_pending = false;

enum : uint8_t {
  PITCH_BENCH_FLOAT = 0,       // legacy walk + bsearch
  PITCH_BENCH_FLOAT_CACHED,      // trunc+clamp±1 find (matches live _fast)
  PITCH_BENCH_RATIO_Q16,
  PITCH_BENCH_Q12,
  PITCH_BENCH_METHODS,         // speed rows: FLOAT / FLOAT_FAST / RATIO / Q12
  PITCH_BENCH_Q20_REF = 5      // accuracy-only private int reference (not a live mode)
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
static int32_t pib_xQ16[PIB_SIZE];        // live RATIO: native Q16 (1.0 = 65536)
static int32_t pib_yQ16[PIB_SIZE];
static int32_t pib_slopeQ16[PIB_SIZE - 1];
static int32_t pib_slopeQ20[PIB_SIZE - 1]; // accuracy oracle on native Q16 knots (not live)
static int16_t pib_cache[NUM_OSCILLATORS];
static bool    pib_ready = false;

// Modifier-domain grid (matches live FLOAT).
// seq: step 0.0001 = walk-favorable (hit / +1 segment). Accuracy cmd 29 uses this.
// jump: step 0.05 ≈ 2.5 segments — miss-heavy; ranks closer to live under pitch mod.
static constexpr float PITCH_BENCH_MOD_MIN = -1.0f;
static constexpr float PITCH_BENCH_MOD_MAX = 3.0f;
static constexpr float PITCH_BENCH_MOD_STEP = 0.0001f;       // seq (+ accuracy)
static constexpr float PITCH_BENCH_MOD_STEP_JUMP = 0.05f;
// Fixed-voice Q12 speed grid: ×10000 table-units.
static constexpr int32_t PIB_XINT_MIN = -PIB_SCALE;       // -10000
static constexpr int32_t PIB_XINT_MAX = 3 * PIB_SCALE;    //  30000
static constexpr int32_t PIB_XINT_STEP_SEQ = 1;
static constexpr int32_t PIB_XINT_STEP_JUMP =
    (int32_t)(PITCH_BENCH_MOD_STEP_JUMP * (float)PIB_SCALE + 0.5f); // 500
// Fixed-voice RATIO speed grid: native xQ16 (live clamp domain).
static constexpr int32_t PIB_XQ16_MIN = -65536;   // -1.0
static constexpr int32_t PIB_XQ16_MAX = 196608;   //  3.0
static constexpr int32_t PIB_XQ16_STEP_SEQ = 7;     // ≈0.0001
static constexpr int32_t PIB_XQ16_STEP_JUMP = 3277; // ≈0.05

// |cents| histogram for p50/p95/p99 (linear bins over 0..HIST_MAX).
static constexpr int PIB_HIST_BINS = 64;
static constexpr double PIB_HIST_MAX_CENTS = 2.0;

static const char *pitch_bench_method_name(uint8_t m) {
  switch (m) {
    case PITCH_BENCH_FLOAT:       return "FLOAT";
    case PITCH_BENCH_FLOAT_CACHED:  return "FLOAT_CACHED";
    case PITCH_BENCH_RATIO_Q16:   return "RATIO_Q16";
    case PITCH_BENCH_Q12:         return "Q12";
    default:                      return "?";
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

  double fraction = 4.00 / (double)PIB_SIZE;
  for (int i = 0; i < PIB_SIZE; i++) {
    double x;
    float y_value;
    if (i == 0) {
      x = -1.00;
      y_value = 0.25f;
    } else if (i == PIB_SIZE - 1) {
      x = 3.0;
      y_value = 4.0f;
    } else {
      x = (-1.00 + (fraction * (double)i));
      y_value = (float)expInterpolationSolveY(x + 1.00, 1.00, 3.00, 0.50, 2.00);
    }
    // FLOAT: natural modifier / ratio. Q12: ×10000. RATIO: native Q16 (1.0 = 65536).
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
    // Non-live accuracy oracle: Q20 lerp on the same native Q16 knots.
    pib_slopeQ20[i] = (int32_t)((((int64_t)dy16 << 20) + (dx16 > 0 ? dx16 / 2 : -dx16 / 2)) / (int64_t)dx16);

    float dxF = pib_xF[i + 1] - pib_xF[i];
    if (dxF == 0.0f) dxF = 1.0f;
    pib_slopeF[i] = (pib_yF[i + 1] - pib_yF[i]) / dxF;
  }

  pib_reset_cache();
  pib_ready = true;
}

static int pib_find_seg_int(const int32_t *xTab, int32_t x, int dcoIndex) {
  int low = pib_cache[dcoIndex];
  if (low < 0 || low > PIB_SIZE - 2 || !(xTab[low] <= x && x < xTab[low + 1])) {
    if (low >= 0 && low < PIB_SIZE - 1) {
      if (x >= xTab[low + 1]) {
        while (low < PIB_SIZE - 2 && x >= xTab[low + 1]) low++;
      } else if (x < xTab[low]) {
        while (low > 0 && x < xTab[low]) low--;
      }
    }
    if (!(low >= 0 && low < PIB_SIZE - 1 && xTab[low] <= x && x < xTab[low + 1])) {
      int l = 0, h = PIB_SIZE - 1;
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
      if (low > PIB_SIZE - 2) low = PIB_SIZE - 2;
    }
    pib_cache[dcoIndex] = (int16_t)low;
  }
  return low;
}

// modifier in [-1,3] → frequency ratio (natural float tables).
// Legacy segment find: walk from cache, then binary search (previous FLOAT).
static float pib_interp_float(float modifier, int dcoIndex) {
  if (modifier <= pib_xF[0]) return pib_yF[0];
  if (modifier >= pib_xF[PIB_SIZE - 1]) return pib_yF[PIB_SIZE - 1];

  int low = pib_cache[dcoIndex];
  if (low < 0 || low > PIB_SIZE - 2 ||
      !(pib_xF[low] <= modifier && modifier < pib_xF[low + 1])) {
    if (low >= 0 && low < PIB_SIZE - 1) {
      if (modifier >= pib_xF[low + 1]) {
        while (low < PIB_SIZE - 2 && modifier >= pib_xF[low + 1]) ++low;
      } else if (modifier < pib_xF[low]) {
        while (low > 0 && modifier < pib_xF[low]) --low;
      }
    }
    if (!(low >= 0 && low < PIB_SIZE - 1 &&
          pib_xF[low] <= modifier && modifier < pib_xF[low + 1])) {
      int l = 0, h = PIB_SIZE - 1;
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
      if (low > PIB_SIZE - 2) low = PIB_SIZE - 2;
    }
    pib_cache[dcoIndex] = (int16_t)low;
  }
  return pib_yF[low] + pib_slopeF[low] * (modifier - pib_xF[low]);
}

// Same lerp as pib_interp_float; find matches live interpolateRatioFloat_cached_fast
// (cache hit or trunc+clamp±1, plain lerp). noinline matches live codegen isolation.
__attribute__((noinline))
static float pib_interp_float_fast(float modifier, int dcoIndex) {
  if (modifier <= -1.0f) return pib_yF[0];
  if (modifier >= 3.0f) return pib_yF[PIB_SIZE - 1];

  static constexpr float kPitchX0 = -1.0f;
  static constexpr float kPitchInvDx = (float)PIB_SIZE / 4.0f;
  const int lastSeg = PIB_SIZE - 2;

  int low = pib_cache[dcoIndex];
  if (low >= 0 && low <= lastSeg &&
      pib_xF[low] <= modifier && modifier < pib_xF[low + 1]) {
    // cache hit
  } else {
    int cand = (int)((modifier - kPitchX0) * kPitchInvDx);
    if (cand < 0) cand = 0;
    else if (cand > lastSeg) cand = lastSeg;
    if (cand < lastSeg && modifier >= pib_xF[cand + 1]) ++cand;
    else if (cand > 0 && modifier < pib_xF[cand]) --cand;
    low = cand;
    pib_cache[dcoIndex] = (int16_t)low;
  }
  return pib_yF[low] + pib_slopeF[low] * (modifier - pib_xF[low]);
}

// Live interpolateRatioQ16_cached clone: native Q16 x/y, slopeQ16, trunc±1 find,
// fused ratio return (y is already frequency ratio Q16).
static int32_t pib_interp_ratio_q16(int32_t xQ16, int dcoIndex) {
  static constexpr int32_t kPitchX0_Q16 = -65536;
  static constexpr int32_t kPitchX1_Q16 = 196608;
  if (xQ16 <= kPitchX0_Q16) return pib_yQ16[0];
  if (xQ16 >= kPitchX1_Q16) return pib_yQ16[PIB_SIZE - 1];

  const int lastSeg = PIB_SIZE - 2;
  int low = pib_cache[dcoIndex];
  if (low >= 0 && low <= lastSeg &&
      pib_xQ16[low] <= xQ16 && xQ16 < pib_xQ16[low + 1]) {
    // cache hit
  } else {
    static constexpr int kPitchInvDx = PIB_SIZE / 4; // 50
    int cand = (int)(((int64_t)(xQ16 - kPitchX0_Q16) * (int64_t)kPitchInvDx) >> 16);
    if (cand < 0) cand = 0;
    else if (cand > lastSeg) cand = lastSeg;
    if (cand < lastSeg && xQ16 >= pib_xQ16[cand + 1]) ++cand;
    else if (cand > 0 && xQ16 < pib_xQ16[cand]) --cand;
    low = cand;
    pib_cache[dcoIndex] = (int16_t)low;
  }
  const int32_t delta = xQ16 - pib_xQ16[low];
  return pib_yQ16[low] + (((delta * pib_slopeQ16[low]) + (1 << 15)) >> 16);
}

static int32_t pib_interp_y_q12(int32_t xQ16, int dcoIndex) {
  int32_t xInt = xQ16 >> 16;
  if (xInt <= pib_x[0]) return pib_y[0];
  if (xInt >= pib_x[PIB_SIZE - 1]) return pib_y[PIB_SIZE - 1];
  int low = pib_find_seg_int(pib_x, xInt, dcoIndex);
  int32_t deltaQ12 = (xQ16 - (pib_x[low] << 16)) >> 4;
  return pib_y[low] + (int32_t)((((int64_t)deltaQ12 * (int64_t)pib_slopeQ12[low]) + (1LL << 23)) >> 24);
}

// Non-live accuracy oracle: Q20 lerp on native Q16 knots. Returns ratio Q16.
static int32_t pib_interp_y_q20(int32_t xQ16, int dcoIndex) {
  static constexpr int32_t kPitchX0_Q16 = -65536;
  static constexpr int32_t kPitchX1_Q16 = 196608;
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

// All methods take pitch modifier in [-1,3]. FLOAT uses it directly.
// RATIO / Q20 ref: lroundf(mod*65536) like float-voice A/B. Q12: ×10000 glue.
static float pitch_bench_call(uint8_t method, float modifier, int dco) {
  switch (method) {
    case PITCH_BENCH_FLOAT:
      return pib_interp_float(modifier, dco);
    case PITCH_BENCH_FLOAT_CACHED:
      return pib_interp_float_fast(modifier, dco);
    case PITCH_BENCH_RATIO_Q16: {
      int32_t xQ16 = (int32_t)lroundf(modifier * 65536.0f);
      return (float)pib_interp_ratio_q16(xQ16, dco) * (1.0f / 65536.0f);
    }
    case PITCH_BENCH_Q12: {
      float xTab = modifier * (float)PIB_SCALE;
      int32_t xQ16 = (int32_t)lroundf(xTab * 65536.0f);
      return (float)pib_interp_y_q12(xQ16, dco) / (float)PIB_SCALE;
    }
    case PITCH_BENCH_Q20_REF: {
      // Private accuracy reference (Q20 lerp on native Q16 knots); not a live mode.
      int32_t xQ16 = (int32_t)lroundf(modifier * 65536.0f);
      return (float)pib_interp_y_q20(xQ16, dco) * (1.0f / 65536.0f);
    }
    default:
      return 1.0f;
  }
}

static double pib_cents_abs(float cand, float ref) {
  if (!(ref > 1e-12f) || !(cand > 1e-12f)) return 0.0;
  return fabs(1200.0 * log2((double)cand / (double)ref));
}

// True when modifier maps to a native-Q16 knot (live RATIO / Q20-ref domain).
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

// Bands on modifier (maps to old table bands [-10000,0) / [0,15000) / [15000,30000]).
static int pib_x_band(float modifier) {
  if (modifier < 0.0f) return 0;      // low
  if (modifier < 1.5f) return 1;      // mid
  return 2;                           // high
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

// Count mod-domain samples for one osc over [MIN, MAX] with the given step.
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

// Timed flag-path speed run for one method. `repeats` replays the grid (jump uses
// many passes so call count ≈ seq). Cache reset once at start — jump steps still miss.
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
        if (method == PITCH_BENCH_FLOAT) {
          sink += pib_interp_float(mod, o);
        } else if (method == PITCH_BENCH_FLOAT_CACHED) {
          sink += pib_interp_float_fast(mod, o);
        } else if (method == PITCH_BENCH_RATIO_Q16) {
          int32_t xQ16 = (int32_t)lroundf(mod * 65536.0f);
          sink += (float)pib_interp_ratio_q16(xQ16, o) * Q16_TO_F;
        } else if (method == PITCH_BENCH_Q12) {
          float xTab = mod * (float)PIB_SCALE;
          int32_t xQ16 = (int32_t)lroundf(xTab * 65536.0f);
          sink += (float)pib_interp_y_q12(xQ16, o) / (float)PIB_SCALE;
        }
        ++calls;
      }
    }
  }
  (void)sink;
#else
  // Fixed voice: FLOAT / FLOAT_FAST are soft-float refs (not selectable pitch modes).
  if (method == PITCH_BENCH_FLOAT || method == PITCH_BENCH_FLOAT_CACHED) {
    volatile float sink = 0.0f;
    for (uint32_t r = 0; r < repeats; ++r) {
      for (uint8_t o = 0; o < PIB_BENCH_OSCS; ++o) {
        for (float mod = PITCH_BENCH_MOD_MIN;
             mod <= PITCH_BENCH_MOD_MAX + 0.5f * modStep;
             mod += modStep) {
          sink += (method == PITCH_BENCH_FLOAT_CACHED)
                      ? pib_interp_float_fast(mod, o)
                      : pib_interp_float(mod, o);
          ++calls;
        }
      }
    }
    (void)sink;
  } else if (method == PITCH_BENCH_RATIO_Q16) {
    volatile int32_t sink = 0;
    for (uint32_t r = 0; r < repeats; ++r) {
      for (uint8_t o = 0; o < PIB_BENCH_OSCS; ++o) {
        for (int32_t xQ16 = PIB_XQ16_MIN; xQ16 <= PIB_XQ16_MAX; xQ16 += xQ16Step) {
          sink += pib_interp_ratio_q16(xQ16, o);
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
          const int32_t xQ16 = xInt << 16;
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
  double refMeanNs = ((double)totalUs[PITCH_BENCH_FLOAT] * 1000.0) /
                     (double)totalCalls[PITCH_BENCH_FLOAT];
  if (refMeanNs < 1e-9) refMeanNs = 1e-9;

  bench_out_printf("-- pattern=%s  step=%.4f repeats=%lu oscs=%d",
                   pattern, (double)modStep, (unsigned long)repeats, PIB_BENCH_OSCS);
#ifndef USE_FLOAT_VOICE_TASK
  bench_out_printf("  xIntStep=%ld xQ16Step=%ld", (long)xIntStep, (long)xQ16Step);
#else
  (void)xIntStep;
  (void)xQ16Step;
#endif
  bench_out_println("");
  bench_out_println("method       calls    totalUs   meanNs  pctVsFloat");
  bench_out_println("------------ -------- --------- -------- ----------");
  for (uint8_t method = 0; method < PITCH_BENCH_METHODS; ++method) {
    double meanNs = ((double)totalUs[method] * 1000.0) / (double)totalCalls[method];
    double pct = 100.0 * meanNs / refMeanNs;
    bench_out_printf("%-12s %8lu %9lu %8.2f %10.1f\n",
                     pitch_bench_method_name(method),
                     (unsigned long)totalCalls[method],
                     (unsigned long)totalUs[method],
                     meanNs,
                     pct);
  }
}

void pitch_interp_bench_run_speed() {
  pib_ensure_tables();

  // Jump grid is coarse; repeat until call count ≈ one seq pass.
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

  bench_out_println("=== PITCH INTERP BENCH ===");
  bench_out_printf("live_pitch=%s (private tables; does not touch voice)\n",
                   pitch_bench_live_mode_name());
#ifdef USE_FLOAT_VOICE_TASK
  bench_out_printf("speed=flag-path voice=FLOAT (to float ratio; RATIO lroundf(mod*65536))\n");
#else
  bench_out_printf("speed=flag-path voice=FIXED (RATIO native xQ16; Q12 y->ratio; FLOAT*=ref)\n");
#endif
  bench_out_printf("RATIO=native Q16 slopeQ16 trunc±1  Q12=×10000 slopeQ12\n");
  bench_out_printf("FLOAT=walk find  FLOAT_FAST=live trunc+clamp±1 noinline  pctVsFloat vs FLOAT meanNs\n");
  bench_out_printf("seq=walk-favorable  jump=miss-heavy (live-like under pitch mod)\n");
  bench_out_printf("grid=mod %.4f..%.4f\n",
                   (double)PITCH_BENCH_MOD_MIN, (double)PITCH_BENCH_MOD_MAX);

  for (uint8_t p = 0; p < 2; ++p) {
    uint32_t totalCalls[PITCH_BENCH_METHODS] = {0};
    uint64_t totalUs[PITCH_BENCH_METHODS] = {0};
    for (uint8_t method = 0; method < PITCH_BENCH_METHODS; ++method) {
      uint32_t reps = patterns[p].repeats;
#ifndef USE_FLOAT_VOICE_TASK
      if (method == PITCH_BENCH_RATIO_Q16 && p == 1) reps = ratioJumpRepeats;
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

  // vs FLOAT (legacy walk): FLOAT_FAST should be ~0¢; RATIO/Q12 show table gap.
  // vs private Q20 ref: float + int modes (FLOAT/FLOAT_FAST ≈ same table-gap cents).
  static const uint8_t kVsFloat[] = {
    PITCH_BENCH_FLOAT_CACHED, PITCH_BENCH_RATIO_Q16, PITCH_BENCH_Q12
  };
  static const uint8_t kSlopeCand[] = {
    PITCH_BENCH_FLOAT, PITCH_BENCH_FLOAT_CACHED, PITCH_BENCH_RATIO_Q16, PITCH_BENCH_Q12
  };
  static constexpr int N_VS_FLOAT = 3;
  static constexpr int N_SLOPE = 4;

  struct Acc {
    double sumCents;
    double maxCents;
    float xAtMax;
    uint32_t n;
    uint32_t worse05;   // > 0.5¢
    uint32_t worse10;   // > 1.0¢
    uint32_t worse001;  // > 0.01¢ (vs Q20 only)
    uint32_t worse01;   // > 0.1¢  (vs Q20 only)
    double sumKnot;
    double sumMid;
    uint32_t nKnot;
    uint32_t nMid;
    uint32_t hist[PIB_HIST_BINS];
    double bandSum[3];
    double bandMax[3];
    uint32_t bandN[3];
  };

  Acc vsFloat[N_VS_FLOAT];
  Acc vsQ20[N_SLOPE];
  memset(vsFloat, 0, sizeof(vsFloat));
  memset(vsQ20, 0, sizeof(vsQ20));

  // Section 4: knot-only |Q20 - FLOAT| (pure int-table quantization floor).
  double knotQuantSum = 0.0;
  double knotQuantMax = 0.0;
  uint32_t knotQuantN = 0;

  pib_reset_cache();

  for (uint8_t o = 0; o < PIB_BENCH_OSCS; ++o) {
    for (float mod = PITCH_BENCH_MOD_MIN;
         mod <= PITCH_BENCH_MOD_MAX + 0.5f * PITCH_BENCH_MOD_STEP;
         mod += PITCH_BENCH_MOD_STEP) {
      float rFloat = pitch_bench_call(PITCH_BENCH_FLOAT, mod, o);
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

  bench_out_println("=== PITCH INTERP ACCURACY ===");
  bench_out_printf("live_pitch=%s | RATIO=native Q16 slopeQ16 trunc±1 | "
                   "Q20 ref=non-live Q20 lerp on Q16 knots\n",
                   pitch_bench_live_mode_name());
  bench_out_printf("grid=mod %.4f..%.4f step=%.4f oscs=%d | n=%lu\n",
                   (double)PITCH_BENCH_MOD_MIN, (double)PITCH_BENCH_MOD_MAX,
                   (double)PITCH_BENCH_MOD_STEP, PIB_BENCH_OSCS, (unsigned long)nSamples);

  // --- Section 4 first as context for Section 1 ---
  bench_out_println("\n-- Table quantization floor (knot |Q20-FLOAT|) --");
  if (knotQuantN > 0) {
    bench_out_printf("  mean=%.4f¢  max=%.4f¢  n_knots=%lu\n",
                     knotQuantSum / (double)knotQuantN, knotQuantMax,
                     (unsigned long)knotQuantN);
    bench_out_println("  (int knot y vs float knot y; independent of slope mode)");
  } else {
    bench_out_println("  (no knot samples)");
  }

  // --- Section 1: vs FLOAT (legacy walk) ---
  bench_out_println("\n-- vs FLOAT walk (FLOAT_FAST≈0¢; RATIO/Q12 = table gap) --");
  bench_out_println("method       mean¢    max¢   p50¢   p95¢   p99¢  >0.5¢%  >1.0¢%");
  for (int i = 0; i < N_VS_FLOAT; ++i) {
    Acc &a = vsFloat[i];
    double mean = (a.n > 0) ? (a.sumCents / (double)a.n) : 0.0;
    double p50 = pib_hist_percentile(a.hist, a.n, 50.0);
    double p95 = pib_hist_percentile(a.hist, a.n, 95.0);
    double p99 = pib_hist_percentile(a.hist, a.n, 99.0);
    double pct05 = (a.n > 0) ? (100.0 * (double)a.worse05 / (double)a.n) : 0.0;
    double pct10 = (a.n > 0) ? (100.0 * (double)a.worse10 / (double)a.n) : 0.0;
    bench_out_printf("%-12s %6.3f %7.3f %6.3f %6.3f %6.3f %6.2f %6.2f\n",
                     pitch_bench_method_name(kVsFloat[i]),
                     mean, a.maxCents, p50, p95, p99, pct05, pct10);
  }

  // --- Section 2: vs Q20 (float table-gap + int slope A/B) ---
  bench_out_println("\n-- vs Q20 ref (native Q16 knots; FLOAT*≈table gap; RATIO=slopeQ16 vs Q20) --");
  bench_out_println("method       mean¢    max¢   p50¢   p95¢   p99¢  >0.01¢% >0.1¢%");
  for (int i = 0; i < N_SLOPE; ++i) {
    Acc &a = vsQ20[i];
    double mean = (a.n > 0) ? (a.sumCents / (double)a.n) : 0.0;
    double p50 = pib_hist_percentile(a.hist, a.n, 50.0);
    double p95 = pib_hist_percentile(a.hist, a.n, 95.0);
    double p99 = pib_hist_percentile(a.hist, a.n, 99.0);
    double pct001 = (a.n > 0) ? (100.0 * (double)a.worse001 / (double)a.n) : 0.0;
    double pct01 = (a.n > 0) ? (100.0 * (double)a.worse01 / (double)a.n) : 0.0;
    bench_out_printf("%-12s %6.4f %7.4f %6.4f %6.4f %6.4f %7.3f %6.3f\n",
                     pitch_bench_method_name(kSlopeCand[i]),
                     mean, a.maxCents, p50, p95, p99, pct001, pct01);
  }
  bench_out_println("knot vs mid mean¢ (slope error should appear in mid):");
  for (int i = 0; i < N_SLOPE; ++i) {
    Acc &a = vsQ20[i];
    double mk = (a.nKnot > 0) ? (a.sumKnot / (double)a.nKnot) : 0.0;
    double mm = (a.nMid > 0) ? (a.sumMid / (double)a.nMid) : 0.0;
    bench_out_printf("  %-12s knot=%.5f¢ (n=%lu)  mid=%.5f¢ (n=%lu)  max@mod=%.4f\n",
                     pitch_bench_method_name(kSlopeCand[i]),
                     mk, (unsigned long)a.nKnot, mm, (unsigned long)a.nMid,
                     (double)a.xAtMax);
  }

  // --- Section 3: modifier bands vs Q20 ---
  bench_out_println("\n-- vs Q20 ref by mod-band (low<0, mid<1.5, high) --");
  bench_out_println("method       low mean/max¢     mid mean/max¢     high mean/max¢");
  for (int i = 0; i < N_SLOPE; ++i) {
    Acc &a = vsQ20[i];
    double m0 = (a.bandN[0] > 0) ? (a.bandSum[0] / (double)a.bandN[0]) : 0.0;
    double m1 = (a.bandN[1] > 0) ? (a.bandSum[1] / (double)a.bandN[1]) : 0.0;
    double m2 = (a.bandN[2] > 0) ? (a.bandSum[2] / (double)a.bandN[2]) : 0.0;
    bench_out_printf("%-12s %6.4f/%6.4f   %6.4f/%6.4f   %6.4f/%6.4f\n",
                     pitch_bench_method_name(kSlopeCand[i]),
                     m0, a.bandMax[0], m1, a.bandMax[1], m2, a.bandMax[2]);
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
