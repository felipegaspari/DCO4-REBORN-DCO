#include "include_all.h"
#include <math.h>
#include "clkdiv.h"

// Clkdiv speed / accuracy (cmds 32 / 33). All six methods on both voice engines.
// Glue matches live: fixed Q24 helpers; float native Hz for GOLD/FLOAT, Hz→Q24 then helper
// for integer (same as clkdiv_live_hz_total_cycles). GOLD_REF is true-Hz llround (not a
// CLKDIV_MODE). Speed pctVsGOLD_REF. Needs RUNNING_AVERAGE for paced bench_out_* TX.

#if defined(RUNNING_AVERAGE)

volatile bool clkdiv_hp_bench_speed_pending = false;
volatile bool clkdiv_hp_bench_accuracy_pending = false;

enum : uint8_t {
  CDB_GOLD_REF = 0,
  CDB_GOLD_LIVE,
  CDB_FLOAT_LIVE,
  CDB_Q16,
  CDB_Q8,
  CDB_FAST_Q4,
  CDB_METHODS
};

// Accuracy + speed seq: 1.00 … 7000.00 Hz @ 0.01 Hz inclusive.
static constexpr int32_t CDB_HZ_MAX = 7000;
static constexpr int32_t CDB_CHZ_MIN = 100;
static constexpr uint32_t CDB_CORRECTION = 0; // live arbitrary_measured_correction_value

static constexpr int CDB_HIST_BINS = 100;
static constexpr double CDB_HIST_MAX_CENTS = 50.0;

// Speed seq: scratch chunk (div by 3). Jump: 10 geometric steps/semitone (~10× notes).
static constexpr uint32_t CDB_CHUNK = 384;
static constexpr uint32_t CDB_JUMP_STEPS = 10;
static constexpr uint32_t CDB_NOTE_N =
    (uint32_t)(sizeof(sNotePitches_q24) / sizeof(sNotePitches_q24[0]));
static constexpr uint32_t CDB_JUMP_MAX =
    ((CDB_JUMP_STEPS * (CDB_NOTE_N > 1u ? CDB_NOTE_N - 1u : 0u) + 1u + 2u) / 3u) * 3u;
static int64_t cdb_jump_q24[CDB_JUMP_MAX];
static double cdb_jump_hz[CDB_JUMP_MAX];
static uint32_t cdb_jump_n = 0;
static bool cdb_jump_ready = false;

static uint32_t cdb_seq_osc_n() {
  return (uint32_t)(CDB_HZ_MAX * 100 - CDB_CHZ_MIN + 1);
}

static const char *cdb_method_name(uint8_t method) {
  switch (method) {
    case CDB_GOLD_REF:    return "GOLD_REF";
    case CDB_GOLD_LIVE:   return "GOLD_LIVE";
    case CDB_FLOAT_LIVE:  return "FLOAT_LIVE";
    case CDB_Q16:         return "Q16";
    case CDB_Q8:          return "Q8";
    default:              return "FAST_Q4";
  }
}

static const char *cdb_live_clkdiv_name() {
#ifdef USE_FLOAT_VOICE_TASK
  return "FLOAT";
#elif CLKDIV_MODE == CLKDIV_GOLD
  return "GOLD";
#elif CLKDIV_MODE == CLKDIV_FLOAT
  return "FLOAT";
#elif CLKDIV_MODE == CLKDIV_Q16
  return "Q16";
#elif CLKDIV_MODE == CLKDIV_Q8
  return "Q8";
#else
  return "FAST_Q4";
#endif
}

static const char *cdb_voice_name() {
#ifdef USE_FLOAT_VOICE_TASK
  return "FLOAT";
#else
  return "FIXED";
#endif
}

static inline int64_t cdb_hz_to_q24(double hz) {
  return (int64_t)llround(hz * 16777216.0);
}

static inline int64_t cdb_float_hz_to_q24(float hz) {
  return (int64_t)llround((double)hz * 16777216.0);
}

static inline double cdb_q24_to_hz(int64_t freq_q24) {
  return (double)freq_q24 * (1.0 / 16777216.0);
}

static inline uint32_t cdb_pio(uint32_t total, uint32_t y, uint32_t w, uint32_t k) {
  return pio_clk_div_for_y(total + CDB_CORRECTION, y, w, k);
}

// GOLD_REF: true-Hz llround (both engines).
static inline uint32_t cdb_clk_div_gold_ref(uint32_t sys_hz, double hz,
                                            uint32_t y, uint32_t w, uint32_t k) {
  return cdb_pio(clkdiv_gold_total_cycles_Hz(sys_hz, hz), y, w, k);
}

#ifdef USE_FLOAT_VOICE_TASK
// Float voice: native Hz. Integer CLKDIV_MODE rows pay Hz→Q24 (matches live clkdiv_live_hz).
static inline uint32_t cdb_clk_div_by_method(uint8_t method, uint32_t sys_hz,
                                             int64_t /*freq_q24*/, double hz,
                                             uint32_t y, uint32_t w, uint32_t k) {
  if (method == CDB_GOLD_REF) return cdb_clk_div_gold_ref(sys_hz, hz, y, w, k);
  const float freq_f = (float)hz;
  if (method == CDB_GOLD_LIVE)
    return cdb_pio(clkdiv_gold_total_cycles_Hz(sys_hz, (double)freq_f), y, w, k);
  if (method == CDB_FLOAT_LIVE)
    return cdb_pio(clkdiv_float_total_cycles_Hz(sys_hz, freq_f), y, w, k);
  const int64_t q24 = cdb_float_hz_to_q24(freq_f);
  if (method == CDB_Q16)
    return cdb_pio(clkdiv_q16_total_cycles(sys_hz, q24), y, w, k);
  if (method == CDB_Q8)
    return cdb_pio(clkdiv_q8_total_cycles(sys_hz, q24), y, w, k);
  return cdb_pio(clkdiv_fast_q4_total_cycles(sys_hz, q24), y, w, k);
}
#else
// Fixed voice: Q24 domain. GOLD_REF uses true Hz; others use precomputed q24 (no extra convert).
static inline uint32_t cdb_clk_div_by_method(uint8_t method, uint32_t sys_hz,
                                             int64_t freq_q24, double hz,
                                             uint32_t y, uint32_t w, uint32_t k) {
  if (method == CDB_GOLD_REF) return cdb_clk_div_gold_ref(sys_hz, hz, y, w, k);
  if (method == CDB_GOLD_LIVE)
    return cdb_pio(clkdiv_gold_total_cycles(sys_hz, freq_q24), y, w, k);
  if (method == CDB_FLOAT_LIVE)
    return cdb_pio(clkdiv_float_total_cycles(sys_hz, freq_q24), y, w, k);
  if (method == CDB_Q16)
    return cdb_pio(clkdiv_q16_total_cycles(sys_hz, freq_q24), y, w, k);
  if (method == CDB_Q8)
    return cdb_pio(clkdiv_q8_total_cycles(sys_hz, freq_q24), y, w, k);
  return cdb_pio(clkdiv_fast_q4_total_cycles(sys_hz, freq_q24), y, w, k);
}
#endif

static inline double cdb_out_hz(uint32_t sys_hz, uint32_t div,
                                uint32_t y, uint32_t w, uint32_t k) {
  uint64_t period = (uint64_t)y + (uint64_t)div * (uint64_t)w + (uint64_t)k;
  if (period == 0) return 0.0;
  return (double)sys_hz / (double)period;
}

static inline double cdb_cents_abs(double out_hz, double target_hz) {
  if (!(out_hz > 0.0) || !(target_hz > 0.0)) return 0.0;
  return fabs(1200.0 * log2(out_hz / target_hz));
}

static int cdb_hz_band(double hz) {
  if (hz < 100.0) return 0;
  if (hz < 1000.0) return 1;
  return 2;
}

static void cdb_hist_add(uint32_t *h, double cents) {
  if (cents < 0.0) cents = 0.0;
  int b = (int)(cents * (double)CDB_HIST_BINS / CDB_HIST_MAX_CENTS);
  if (b < 0) b = 0;
  if (b >= CDB_HIST_BINS) b = CDB_HIST_BINS - 1;
  h[b]++;
}

static double cdb_hist_percentile(const uint32_t *h, uint32_t n, double pct) {
  if (n == 0) return 0.0;
  uint64_t target = (uint64_t)((pct / 100.0) * (double)n);
  if (target >= n) target = n - 1u;
  uint64_t cum = 0;
  for (int b = 0; b < CDB_HIST_BINS; ++b) {
    cum += h[b];
    if (cum > target) {
      return ((double)b + 0.5) * CDB_HIST_MAX_CENTS / (double)CDB_HIST_BINS;
    }
  }
  return CDB_HIST_MAX_CENTS;
}

static void cdb_ensure_jump_grid() {
  if (cdb_jump_ready) return;

  uint32_t n = 0;
  for (uint32_t i = 0; i + 1u < CDB_NOTE_N && n + CDB_JUMP_STEPS <= CDB_JUMP_MAX; ++i) {
    const double f0 = (double)sNotePitches_q24[i];
    for (uint32_t k = 0; k < CDB_JUMP_STEPS; ++k) {
      int64_t f = (int64_t)llround(f0 * pow(2.0, (double)k / 120.0));
      if (f < 1) f = 1;
      cdb_jump_q24[n] = f;
      cdb_jump_hz[n] = cdb_q24_to_hz(f);
      ++n;
    }
  }
  if (CDB_NOTE_N > 0 && n < CDB_JUMP_MAX) {
    cdb_jump_q24[n] = sNotePitches_q24[CDB_NOTE_N - 1u];
    cdb_jump_hz[n] = cdb_q24_to_hz(cdb_jump_q24[n]);
    ++n;
  }
  while (n % 3u != 0 && n < CDB_JUMP_MAX) {
    cdb_jump_q24[n] = (n > 0) ? cdb_jump_q24[n - 1u] : (int64_t)(1LL << 24);
    cdb_jump_hz[n] = cdb_q24_to_hz(cdb_jump_q24[n]);
    ++n;
  }
  cdb_jump_n = n;
  cdb_jump_ready = true;
}

// Timed 3-osc clone. q24 + true Hz precomputed; GOLD_REF uses hz only.
static void cdb_speed_run(uint8_t method, const int64_t *q24, const double *hz, uint32_t nPts,
                          uint32_t repeats, uint32_t sys_hz,
                          uint32_t y, uint32_t w, uint32_t k,
                          uint32_t *outFrames, uint64_t *outUs) {
  uint32_t t0 = micros();
  uint32_t frames = 0;
  volatile uint32_t sink = 0;

  for (uint32_t r = 0; r < repeats; ++r) {
    for (uint32_t i = 0; i + 2u < nPts; i += 3u) {
      sink += cdb_clk_div_by_method(method, sys_hz, q24[i], hz[i], y, w, k);
      sink += cdb_clk_div_by_method(method, sys_hz, q24[i + 1], hz[i + 1], y, w, k);
      sink += cdb_clk_div_by_method(method, sys_hz, q24[i + 2], hz[i + 2], y, w, k);
      ++frames;
    }
  }

  uint32_t t1 = micros();
  *outFrames = frames;
  *outUs = (uint64_t)(t1 - t0);
  (void)sink;
}

// Same 0.01 Hz grid as accuracy. Fill q24 + true Hz outside t0/t1.
static void cdb_speed_run_seq(uint8_t method, uint32_t sys_hz,
                              uint32_t y, uint32_t w, uint32_t k,
                              uint32_t *outFrames, uint64_t *outUs) {
  int64_t chunk_q24[CDB_CHUNK];
  double chunk_hz[CDB_CHUNK];
  const int32_t cHzMax = CDB_HZ_MAX * 100;
  uint32_t frames = 0;
  uint64_t totalUs = 0;
  volatile uint32_t sink = 0;

  for (int32_t cHz = CDB_CHZ_MIN; cHz <= cHzMax; ) {
    uint32_t n = 0;
    while (n < CDB_CHUNK && cHz <= cHzMax) {
      double hz = (double)cHz * 0.01;
      chunk_hz[n] = hz;
      chunk_q24[n] = cdb_hz_to_q24(hz);
      ++n;
      ++cHz;
    }
    while (n % 3u != 0 && n < CDB_CHUNK) {
      chunk_hz[n] = chunk_hz[n - 1u];
      chunk_q24[n] = chunk_q24[n - 1u];
      ++n;
    }

    uint32_t t0 = micros();
    for (uint32_t i = 0; i + 2u < n; i += 3u) {
      sink += cdb_clk_div_by_method(method, sys_hz, chunk_q24[i], chunk_hz[i], y, w, k);
      sink += cdb_clk_div_by_method(method, sys_hz, chunk_q24[i + 1], chunk_hz[i + 1], y, w, k);
      sink += cdb_clk_div_by_method(method, sys_hz, chunk_q24[i + 2], chunk_hz[i + 2], y, w, k);
      ++frames;
    }
    uint32_t t1 = micros();
    totalUs += (uint64_t)(t1 - t0);
  }

  *outFrames = frames;
  *outUs = totalUs;
  (void)sink;
}

static void cdb_speed_print_table(const char *pattern, uint32_t unique, uint32_t repeats,
                                  const uint32_t *frames, const uint64_t *totalUs) {
  double refMeanUs = (frames[CDB_GOLD_REF] > 0)
                         ? ((double)totalUs[CDB_GOLD_REF] / (double)frames[CDB_GOLD_REF])
                         : 1.0;
  if (refMeanUs < 1e-9) refMeanUs = 1e-9;

  bench_out_printf("-- pattern=%s  unique=%lu repeats=%lu oscs=3\n",
                   pattern, (unsigned long)unique, (unsigned long)repeats);
  bench_out_println("method      frames    totalUs   meanUs  meanNs/osc  pctVsGOLD_REF");
  bench_out_println("----------- -------- --------- -------- ---------- --------------");
  for (uint8_t method = 0; method < CDB_METHODS; ++method) {
    double meanUs = (frames[method] > 0)
                        ? ((double)totalUs[method] / (double)frames[method])
                        : 0.0;
    double meanNsOsc = meanUs * 1000.0 / 3.0;
    double pct = 100.0 * meanUs / refMeanUs;
    bench_out_printf("%-11s %8lu %9lu %8.3f %10.1f %14.1f\n",
                     cdb_method_name(method),
                     (unsigned long)frames[method],
                     (unsigned long)totalUs[method],
                     meanUs,
                     meanNsOsc,
                     pct);
  }
}

void clkdiv_hp_bench_run_speed() {
  cdb_ensure_jump_grid();

  const uint32_t sys_hz = sysClock_Hz;
  const uint32_t y = pioPulseLength;
  const uint32_t w = PIO_RAMP_WEIGHT_FREE;
  const uint32_t k = PIO_PERIOD_OVERHEAD_FREE;
  const uint32_t seqUnique = cdb_seq_osc_n();

  uint32_t seqFramesEst = (seqUnique + 2u) / 3u;
  const uint32_t jumpFrames = (cdb_jump_n >= 3u) ? (cdb_jump_n / 3u) : 1u;
  uint32_t jumpRepeats = 1;
  if (seqFramesEst > jumpFrames)
    jumpRepeats = (seqFramesEst + jumpFrames - 1u) / jumpFrames;

  bench_out_println("=== CLKDIV BENCH ===");
  bench_out_printf("live_clkdiv=%s voice=%s (engine-domain glue + GOLD_REF; does not touch PIO)\n",
                   cdb_live_clkdiv_name(), cdb_voice_name());
#ifdef USE_FLOAT_VOICE_TASK
  bench_out_println("domain=FLOAT Hz  GOLD_LIVE=native double on freq_f  FLOAT_LIVE=native float");
  bench_out_println("integer=Hz→Q24 then helper (matches live clkdiv_live_hz)");
#else
  bench_out_println("domain=FIXED Q24  GOLD_LIVE=Q24 double  FLOAT_LIVE=Q24 float  integer=Q24 helpers");
#endif
  bench_out_printf("seq=0.01Hz 1..%d (same grid as accuracy)  jump=10 steps/semitone (~10× notes)\n",
                   CDB_HZ_MAX);
  bench_out_println("compare dump10 clkdiv math mean ≈ meanUs/frame; ranking from pctVsGOLD_REF (jump)");
  bench_out_printf("y=%lu w=%lu k=%lu (free-run) sys=%lu Hz\n",
                   (unsigned long)y, (unsigned long)w, (unsigned long)k,
                   (unsigned long)sys_hz);

  {
    uint32_t frames[CDB_METHODS] = {0};
    uint64_t totalUs[CDB_METHODS] = {0};
    for (uint8_t method = 0; method < CDB_METHODS; ++method) {
      cdb_speed_run_seq(method, sys_hz, y, w, k, &frames[method], &totalUs[method]);
    }
    cdb_speed_print_table("seq", seqUnique, 1u, frames, totalUs);
  }
  {
    uint32_t frames[CDB_METHODS] = {0};
    uint64_t totalUs[CDB_METHODS] = {0};
    for (uint8_t method = 0; method < CDB_METHODS; ++method) {
      cdb_speed_run(method, cdb_jump_q24, cdb_jump_hz, cdb_jump_n, jumpRepeats,
                    sys_hz, y, w, k, &frames[method], &totalUs[method]);
    }
    cdb_speed_print_table("jump", cdb_jump_n, jumpRepeats, frames, totalUs);
  }
  bench_out_println("=======================");
}

void clkdiv_hp_bench_run_accuracy() {
  const uint32_t sys_hz = sysClock_Hz;
  const uint32_t y = pioPulseLength;
  const uint32_t w = PIO_RAMP_WEIGHT_FREE;
  const uint32_t k = PIO_PERIOD_OVERHEAD_FREE;
  const int32_t cHzMax = CDB_HZ_MAX * 100;

  struct Acc {
    double sumCents;
    double maxCents;
    float hzAtMax;
    uint32_t n;
    uint32_t worse01;   // > 0.1¢
    uint32_t worse05;   // > 0.5¢
    uint32_t worse10;   // > 1.0¢
    uint32_t maxDivDelta;
    uint32_t hist[CDB_HIST_BINS];
    double bandSum[3];
    double bandMax[3];
    uint32_t bandN[3];
  } acc[CDB_METHODS];

  memset(acc, 0, sizeof(acc));

  uint32_t disagreeQ8PreciseN = 0, maxQ8Precise = 0;
  uint32_t disagreeLiveRefN = 0, maxLiveRef = 0;
  uint32_t disagreeFloatGoldN = 0, maxFloatGold = 0;

  for (int32_t cHz = CDB_CHZ_MIN; cHz <= cHzMax; ++cHz) {
    double hz = (double)cHz * 0.01;
    int64_t freq_q24 = cdb_hz_to_q24(hz);

    uint32_t divs[CDB_METHODS];
    for (uint8_t method = 0; method < CDB_METHODS; ++method) {
      divs[method] = cdb_clk_div_by_method(method, sys_hz, freq_q24, hz, y, w, k);
    }
    const uint32_t ref_div = divs[CDB_GOLD_REF];

#ifdef USE_FLOAT_VOICE_TASK
    const int64_t q24_int = cdb_float_hz_to_q24((float)hz);
#else
    const int64_t q24_int = freq_q24;
#endif
    const uint32_t precise8_div =
        cdb_pio(clkdiv_precise_q8_total_cycles(sys_hz, q24_int), y, w, k);
    uint32_t dQ8P = (divs[CDB_Q8] > precise8_div)
                        ? (divs[CDB_Q8] - precise8_div)
                        : (precise8_div - divs[CDB_Q8]);
    if (dQ8P != 0) ++disagreeQ8PreciseN;
    if (dQ8P > maxQ8Precise) maxQ8Precise = dQ8P;

    uint32_t dLive = (divs[CDB_GOLD_LIVE] > ref_div)
                         ? (divs[CDB_GOLD_LIVE] - ref_div)
                         : (ref_div - divs[CDB_GOLD_LIVE]);
    if (dLive != 0) ++disagreeLiveRefN;
    if (dLive > maxLiveRef) maxLiveRef = dLive;

    uint32_t dFG = (divs[CDB_FLOAT_LIVE] > divs[CDB_GOLD_LIVE])
                       ? (divs[CDB_FLOAT_LIVE] - divs[CDB_GOLD_LIVE])
                       : (divs[CDB_GOLD_LIVE] - divs[CDB_FLOAT_LIVE]);
    if (dFG != 0) ++disagreeFloatGoldN;
    if (dFG > maxFloatGold) maxFloatGold = dFG;

    const int band = cdb_hz_band(hz);
    for (uint8_t method = 0; method < CDB_METHODS; ++method) {
      double out = cdb_out_hz(sys_hz, divs[method], y, w, k);
      double c = cdb_cents_abs(out, hz);
      Acc &a = acc[method];
      a.sumCents += c;
      a.n++;
      if (c > 0.1) a.worse01++;
      if (c > 0.5) a.worse05++;
      if (c > 1.0) a.worse10++;
      if (c > a.maxCents) {
        a.maxCents = c;
        a.hzAtMax = (float)hz;
      }
      uint32_t dDelta = (divs[method] > ref_div)
                            ? (divs[method] - ref_div)
                            : (ref_div - divs[method]);
      if (dDelta > a.maxDivDelta) a.maxDivDelta = dDelta;
      cdb_hist_add(a.hist, c);
      a.bandSum[band] += c;
      a.bandN[band]++;
      if (c > a.bandMax[band]) a.bandMax[band] = c;
    }
  }

  uint32_t nSamples = acc[CDB_GOLD_REF].n;

  bench_out_println("=== CLKDIV ACCURACY ===");
  bench_out_printf("live_clkdiv=%s voice=%s | ref=GOLD_REF true-Hz llround + pio_clk_div_for_y\n",
                   cdb_live_clkdiv_name(), cdb_voice_name());
#ifdef USE_FLOAT_VOICE_TASK
  bench_out_println("GOLD_LIVE=native double on freq_f  FLOAT_LIVE=native float  integer=Hz→Q24");
#else
  bench_out_println("GOLD_LIVE=Q24→double  FLOAT_LIVE=Q24→float  Q4≈±1/32 Hz  Q8≈±1/512 Hz before PIO");
#endif
  bench_out_printf("y=%lu w=%lu k=%lu (free-run) sys=%lu Hz\n",
                   (unsigned long)y, (unsigned long)w, (unsigned long)k,
                   (unsigned long)sys_hz);
  bench_out_printf("grid=0.01Hz 1..%d | n=%lu | cents vs target Hz; max|Δdiv| vs GOLD_REF\n",
                   CDB_HZ_MAX, (unsigned long)nSamples);

  bench_out_println("\n-- vs target Hz (Δdiv vs GOLD_REF) --");
  bench_out_println("method      mean¢    max¢   p50¢   p95¢   p99¢  >0.1¢%  >0.5¢%  >1.0¢%  max|Δdiv|");
  for (uint8_t method = 0; method < CDB_METHODS; ++method) {
    Acc &a = acc[method];
    double mean = (a.n > 0) ? (a.sumCents / (double)a.n) : 0.0;
    double p50 = cdb_hist_percentile(a.hist, a.n, 50.0);
    double p95 = cdb_hist_percentile(a.hist, a.n, 95.0);
    double p99 = cdb_hist_percentile(a.hist, a.n, 99.0);
    double pct01 = (a.n > 0) ? (100.0 * (double)a.worse01 / (double)a.n) : 0.0;
    double pct05 = (a.n > 0) ? (100.0 * (double)a.worse05 / (double)a.n) : 0.0;
    double pct10 = (a.n > 0) ? (100.0 * (double)a.worse10 / (double)a.n) : 0.0;
    bench_out_printf("%-11s %6.3f %7.3f %6.3f %6.3f %6.3f %6.2f %6.2f %6.2f %10lu\n",
                     cdb_method_name(method),
                     mean, a.maxCents, p50, p95, p99, pct01, pct05, pct10,
                     (unsigned long)a.maxDivDelta);
    bench_out_printf("            max@%.2f Hz\n", (double)a.hzAtMax);
  }

  bench_out_println("\n-- vs target by Hz-band (low<100, mid<1000, high) --");
  bench_out_println("method      low mean/max¢      mid mean/max¢      high mean/max¢");
  for (uint8_t method = 0; method < CDB_METHODS; ++method) {
    Acc &a = acc[method];
    double m0 = (a.bandN[0] > 0) ? (a.bandSum[0] / (double)a.bandN[0]) : 0.0;
    double m1 = (a.bandN[1] > 0) ? (a.bandSum[1] / (double)a.bandN[1]) : 0.0;
    double m2 = (a.bandN[2] > 0) ? (a.bandSum[2] / (double)a.bandN[2]) : 0.0;
    bench_out_printf("%-11s %7.3f/%7.3f   %7.3f/%7.3f   %7.3f/%7.3f\n",
                     cdb_method_name(method),
                     m0, a.bandMax[0], m1, a.bandMax[1], m2, a.bandMax[2]);
  }

  auto pctN = [nSamples](uint32_t n) -> double {
    return (nSamples > 0) ? (100.0 * (double)n / (double)nSamples) : 0.0;
  };
  bench_out_println("\n-- vs GOLD_REF clk_div --");
#ifdef USE_FLOAT_VOICE_TASK
  bench_out_printf("GOLD_LIVE vs GOLD_REF   disagree=%lu / %lu (%.3f%%)  max|Δdiv|=%lu  (float-Hz quant)\n",
                   (unsigned long)disagreeLiveRefN, (unsigned long)nSamples,
                   pctN(disagreeLiveRefN), (unsigned long)maxLiveRef);
#else
  bench_out_printf("GOLD_LIVE vs GOLD_REF   disagree=%lu / %lu (%.3f%%)  max|Δdiv|=%lu  (Q24 floor)\n",
                   (unsigned long)disagreeLiveRefN, (unsigned long)nSamples,
                   pctN(disagreeLiveRefN), (unsigned long)maxLiveRef);
#endif
  bench_out_printf("FLOAT_LIVE vs GOLD_REF  max|Δdiv|=%lu\n",
                   (unsigned long)acc[CDB_FLOAT_LIVE].maxDivDelta);
  bench_out_printf("Q16 vs GOLD_REF         max|Δdiv|=%lu\n",
                   (unsigned long)acc[CDB_Q16].maxDivDelta);
  bench_out_printf("Q8 vs GOLD_REF          max|Δdiv|=%lu\n",
                   (unsigned long)acc[CDB_Q8].maxDivDelta);
  bench_out_printf("FAST_Q4 vs GOLD_REF     max|Δdiv|=%lu\n",
                   (unsigned long)acc[CDB_FAST_Q4].maxDivDelta);
#ifdef USE_FLOAT_VOICE_TASK
  bench_out_println("-- FLOAT_LIVE vs GOLD_LIVE clk_div (float mantissa vs double on freq_f) --");
#else
  bench_out_println("-- FLOAT_LIVE vs GOLD_LIVE clk_div (float mantissa vs double) --");
#endif
  bench_out_printf("disagree=%lu / %lu (%.3f%%)  max|Δdiv|=%lu\n",
                   (unsigned long)disagreeFloatGoldN, (unsigned long)nSamples,
                   pctN(disagreeFloatGoldN), (unsigned long)maxFloatGold);
  bench_out_println("-- Q8 vs internal precise_q8 clk_div (same Q8 identity) --");
  bench_out_printf("disagree=%lu / %lu (%.3f%%)  max|Δdiv|=%lu  (expect 0 above ~16 Hz)\n",
                   (unsigned long)disagreeQ8PreciseN, (unsigned long)nSamples,
                   pctN(disagreeQ8PreciseN), (unsigned long)maxQ8Precise);

  bench_out_println("\n-- human --");
  bench_out_printf("Y-locked free-run (y=%lu w=%lu k=%lu): cents vs target Hz = Q quant + PIO.\n",
                   (unsigned long)y, (unsigned long)w, (unsigned long)k);
#ifdef USE_FLOAT_VOICE_TASK
  bench_out_println("GOLD_REF Δdiv is 0. GOLD_LIVE vs GOLD_REF is float-Hz quantize.");
  bench_out_println("FLOAT_LIVE vs GOLD_LIVE is float mantissa vs double on the same freq_f.");
#else
  bench_out_println("GOLD_REF Δdiv is 0 by definition. GOLD_LIVE vs GOLD_REF is Q24 quantize.");
  bench_out_println("FLOAT_LIVE vs GOLD_LIVE is float mantissa vs Q24 double.");
#endif
  bench_out_println("p50/p95/p99 use 0.5¢ hist bins (0.250=bin0); trust mean / max / >x¢%.");
  bench_out_println("max|Δdiv| at ~1 Hz is huge clk_div counts, not pitch. Disagree% ≠ audible error.");

  for (uint8_t method = 0; method < CDB_METHODS; ++method) {
    Acc &a = acc[method];
    double mean = (a.n > 0) ? (a.sumCents / (double)a.n) : 0.0;
    double m0 = (a.bandN[0] > 0) ? (a.bandSum[0] / (double)a.bandN[0]) : 0.0;
    double m1 = (a.bandN[1] > 0) ? (a.bandSum[1] / (double)a.bandN[1]) : 0.0;
    double m2 = (a.bandN[2] > 0) ? (a.bandSum[2] / (double)a.bandN[2]) : 0.0;
    double playMax = (a.bandMax[1] > a.bandMax[2]) ? a.bandMax[1] : a.bandMax[2];
    const char *worstWhere = (a.hzAtMax < 100.0f) ? "sub-audio/LFO (<100 Hz)"
                                                   : "keyboard range";
    bench_out_printf("\n%s:\n", cdb_method_name(method));
    bench_out_printf("  typical (mean):   %.3f¢\n", mean);
    bench_out_printf("  worst:            %.3f¢ at %.2f Hz  (%s)\n",
                     a.maxCents, (double)a.hzAtMax, worstWhere);
    bench_out_printf("  low <100 Hz:      mean %.3f¢  max %.3f¢\n",
                     m0, a.bandMax[0]);
    bench_out_printf("  mid 100-1000 Hz:  mean %.3f¢  max %.3f¢\n", m1, a.bandMax[1]);
    bench_out_printf("  high >1000 Hz:    mean %.3f¢  max %.3f¢  (PIO ±2 cyc, not clkdiv mode)\n",
                     m2, a.bandMax[2]);
    bench_out_printf("  playing range:    max %.3f¢ (mid+high)\n", playMax);
    bench_out_printf("  vs GOLD_REF:      max|Δdiv|=%lu\n", (unsigned long)a.maxDivDelta);
  }

  {
    Acc &ref = acc[CDB_GOLD_REF];
    Acc &live = acc[CDB_GOLD_LIVE];
    Acc &fl = acc[CDB_FLOAT_LIVE];
    Acc &q16 = acc[CDB_Q16];
    Acc &q8 = acc[CDB_Q8];
    Acc &f4 = acc[CDB_FAST_Q4];
    auto playMax = [](const Acc &a) -> double {
      return (a.bandMax[1] > a.bandMax[2]) ? a.bandMax[1] : a.bandMax[2];
    };
    bench_out_println("\nverdict:");
    bench_out_printf("  playing range (mid+high) vs target Hz: GOLD_REF %.3f¢  GOLD_LIVE %.3f¢  FLOAT_LIVE %.3f¢  Q16 %.3f¢  Q8 %.3f¢  FAST_Q4 %.3f¢\n",
                     playMax(ref), playMax(live), playMax(fl), playMax(q16), playMax(q8), playMax(f4));
#ifdef USE_FLOAT_VOICE_TASK
    bench_out_printf("  GOLD_LIVE vs GOLD_REF Δdiv disagree %.3f%% (max|Δdiv|=%lu) — float-Hz quant.\n",
                     pctN(disagreeLiveRefN), (unsigned long)maxLiveRef);
#else
    bench_out_printf("  GOLD_LIVE vs GOLD_REF Δdiv disagree %.3f%% (max|Δdiv|=%lu) — Q24 floor.\n",
                     pctN(disagreeLiveRefN), (unsigned long)maxLiveRef);
#endif
    bench_out_printf("  FLOAT_LIVE vs GOLD_LIVE Δdiv disagree %.3f%% (max|Δdiv|=%lu) — float vs double.\n",
                     pctN(disagreeFloatGoldN), (unsigned long)maxFloatGold);
    if (playMax(f4) <= playMax(q16) * 1.5 + 0.05) {
      bench_out_printf("  FAST_Q4 ≈ Q16 on playing range — PIO-limited.\n");
    } else {
      bench_out_printf("  Q16 closer to GOLD_REF than FAST_Q4 on playing range.\n");
    }
    bench_out_printf("  low-Hz tail: GOLD_REF %.3f¢  GOLD_LIVE %.3f¢  FLOAT_LIVE %.3f¢  Q16 %.3f¢  Q8 %.3f¢  FAST_Q4 %.3f¢ at <100 Hz.\n",
                     ref.bandMax[0], live.bandMax[0], fl.bandMax[0], q16.bandMax[0], q8.bandMax[0], f4.bandMax[0]);
    bench_out_printf("  Q8 vs internal precise_q8 clk_div disagree %.3f%% (max|Δdiv|=%lu).\n",
                     pctN(disagreeQ8PreciseN), (unsigned long)maxQ8Precise);
  }
  bench_out_println("==========================");
}

void print_clkdiv_hp_bench() {
  if (clkdiv_hp_bench_speed_pending) {
    clkdiv_hp_bench_speed_pending = false;
    clkdiv_hp_bench_run_speed();
  }
  if (clkdiv_hp_bench_accuracy_pending) {
    clkdiv_hp_bench_accuracy_pending = false;
    clkdiv_hp_bench_run_accuracy();
  }
}

#else  // !RUNNING_AVERAGE

volatile bool clkdiv_hp_bench_speed_pending = false;
volatile bool clkdiv_hp_bench_accuracy_pending = false;

void clkdiv_hp_bench_run_speed() {}
void clkdiv_hp_bench_run_accuracy() {}
void print_clkdiv_hp_bench() {}

#endif
