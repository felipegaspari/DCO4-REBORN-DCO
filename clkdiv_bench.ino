#include "include_all.h"
#include <math.h>

// Fixed-voice HP0 vs HP1 clkdiv speed / accuracy. Private clones (does not touch
// live voice / PIO). Needs RUNNING_AVERAGE only for paced bench_out_* TX.
// Debug cmds 32 / 33. Existing CLKDIV_BENCHMARK (float vs double) is separate.

#if defined(RUNNING_AVERAGE)

volatile bool clkdiv_hp_bench_speed_pending = false;
volatile bool clkdiv_hp_bench_accuracy_pending = false;

enum : uint8_t {
  CDB_HP1 = 0,
  CDB_HP0,
  CDB_HP_METHODS
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
static uint32_t cdb_jump_n = 0;
static bool cdb_jump_ready = false;

static uint32_t cdb_seq_osc_n() {
  return (uint32_t)(CDB_HZ_MAX * 100 - CDB_CHZ_MIN + 1);
}

static const char *cdb_method_name(uint8_t method) {
  return (method == CDB_HP1) ? "HP1" : "HP0";
}

static const char *cdb_live_clkdiv_name() {
#if HIGH_PRECISION_CLKDIV
  return "HP1";
#else
  return "HP0";
#endif
}

static const char *cdb_voice_name() {
#ifdef USE_FLOAT_VOICE_TASK
  return "FLOAT";
#else
  return "FIXED";
#endif
}

// HP1 clone: Q8 Hz denom → 64/32 div (matches hp1_total_cycles_q8).
static inline uint32_t cdb_hp1_total_cycles(uint32_t sys_hz, int64_t freq_q24) {
  if (freq_q24 <= 0) return 0;
  uint32_t freq_q8 = (uint32_t)((freq_q24 + (int64_t)(1 << 15)) >> 16);
  if (freq_q8 == 0) freq_q8 = 1;
  uint64_t num = ((uint64_t)sys_hz << 8) + (uint64_t)(freq_q8 / 2u);
  return (uint32_t)(num / freq_q8);
}

// HP0 clone: Q4 convert + 32-bit div (matches !HIGH_PRECISION_CLKDIV vt_clk_div).
static inline uint32_t cdb_hp0_total_cycles(uint32_t sys_hz, int64_t freq_q24) {
  if (freq_q24 <= 0) return 0;
  uint32_t freq_q4 = (uint32_t)((freq_q24 + (1LL << 19)) >> 20);
  if (freq_q4 == 0) freq_q4 = 1;
  return (sys_hz * 16u + (freq_q4 / 2u)) / freq_q4;
}

// Live vt_clk_div is fully inlined (3 oscs / frame). Keep clones inline — noinline
// call tax flattened HP0 vs HP1 on cmd 32.
static inline uint32_t cdb_clk_div_hp1(uint32_t sys_hz, int64_t freq_q24,
                                       uint32_t y, uint32_t w, uint32_t k) {
  return pio_clk_div_for_y(cdb_hp1_total_cycles(sys_hz, freq_q24) + CDB_CORRECTION, y, w, k);
}

static inline uint32_t cdb_clk_div_hp0(uint32_t sys_hz, int64_t freq_q24,
                                       uint32_t y, uint32_t w, uint32_t k) {
  return pio_clk_div_for_y(cdb_hp0_total_cycles(sys_hz, freq_q24) + CDB_CORRECTION, y, w, k);
}

static inline int64_t cdb_hz_to_q24(double hz) {
  return (int64_t)llround(hz * 16777216.0);
}

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
      cdb_jump_q24[n++] = f;
    }
  }
  if (CDB_NOTE_N > 0 && n < CDB_JUMP_MAX)
    cdb_jump_q24[n++] = sNotePitches_q24[CDB_NOTE_N - 1u];
  while (n % 3u != 0 && n < CDB_JUMP_MAX)
    cdb_jump_q24[n++] = (n > 0) ? cdb_jump_q24[n - 1u] : (int64_t)(1LL << 24);
  cdb_jump_n = n;
  cdb_jump_ready = true;
}

// Timed 3-osc vt_clk_div clone. `q24` already integer — no convert tax.
static void cdb_speed_run(uint8_t method, const int64_t *q24, uint32_t nQ24,
                          uint32_t repeats, uint32_t sys_hz,
                          uint32_t y, uint32_t w, uint32_t k,
                          uint32_t *outFrames, uint64_t *outUs) {
  uint32_t t0 = micros();
  uint32_t frames = 0;
  volatile uint32_t sink = 0;

  if (method == CDB_HP1) {
    for (uint32_t r = 0; r < repeats; ++r) {
      for (uint32_t i = 0; i + 2u < nQ24; i += 3u) {
        sink += cdb_clk_div_hp1(sys_hz, q24[i], y, w, k);
        sink += cdb_clk_div_hp1(sys_hz, q24[i + 1], y, w, k);
        sink += cdb_clk_div_hp1(sys_hz, q24[i + 2], y, w, k);
        ++frames;
      }
    }
  } else {
    for (uint32_t r = 0; r < repeats; ++r) {
      for (uint32_t i = 0; i + 2u < nQ24; i += 3u) {
        sink += cdb_clk_div_hp0(sys_hz, q24[i], y, w, k);
        sink += cdb_clk_div_hp0(sys_hz, q24[i + 1], y, w, k);
        sink += cdb_clk_div_hp0(sys_hz, q24[i + 2], y, w, k);
        ++frames;
      }
    }
  }

  uint32_t t1 = micros();
  *outFrames = frames;
  *outUs = (uint64_t)(t1 - t0);
  (void)sink;
}

// Same 0.01 Hz grid as accuracy. Fill q24 outside t0/t1 (chunk scratch).
static void cdb_speed_run_seq(uint8_t method, uint32_t sys_hz,
                              uint32_t y, uint32_t w, uint32_t k,
                              uint32_t *outFrames, uint64_t *outUs) {
  int64_t chunk[CDB_CHUNK];
  const int32_t cHzMax = CDB_HZ_MAX * 100;
  uint32_t frames = 0;
  uint64_t totalUs = 0;
  volatile uint32_t sink = 0;

  for (int32_t cHz = CDB_CHZ_MIN; cHz <= cHzMax; ) {
    uint32_t n = 0;
    while (n < CDB_CHUNK && cHz <= cHzMax) {
      chunk[n++] = cdb_hz_to_q24((double)cHz * 0.01);
      ++cHz;
    }
    while (n % 3u != 0 && n < CDB_CHUNK) {
      chunk[n] = chunk[n - 1u];
      ++n;
    }

    uint32_t t0 = micros();
    if (method == CDB_HP1) {
      for (uint32_t i = 0; i + 2u < n; i += 3u) {
        sink += cdb_clk_div_hp1(sys_hz, chunk[i], y, w, k);
        sink += cdb_clk_div_hp1(sys_hz, chunk[i + 1], y, w, k);
        sink += cdb_clk_div_hp1(sys_hz, chunk[i + 2], y, w, k);
        ++frames;
      }
    } else {
      for (uint32_t i = 0; i + 2u < n; i += 3u) {
        sink += cdb_clk_div_hp0(sys_hz, chunk[i], y, w, k);
        sink += cdb_clk_div_hp0(sys_hz, chunk[i + 1], y, w, k);
        sink += cdb_clk_div_hp0(sys_hz, chunk[i + 2], y, w, k);
        ++frames;
      }
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
  double refMeanUs = (frames[CDB_HP1] > 0)
                         ? ((double)totalUs[CDB_HP1] / (double)frames[CDB_HP1])
                         : 1.0;
  if (refMeanUs < 1e-9) refMeanUs = 1e-9;

  bench_out_printf("-- pattern=%s  unique=%lu repeats=%lu oscs=3\n",
                   pattern, (unsigned long)unique, (unsigned long)repeats);
  bench_out_println("method   frames    totalUs   meanUs  meanNs/osc  pctVsHP1");
  bench_out_println("-------- -------- --------- -------- ---------- ----------");
  for (uint8_t method = 0; method < CDB_HP_METHODS; ++method) {
    double meanUs = (frames[method] > 0)
                        ? ((double)totalUs[method] / (double)frames[method])
                        : 0.0;
    double meanNsOsc = meanUs * 1000.0 / 3.0;
    double pct = 100.0 * meanUs / refMeanUs;
    bench_out_printf("%-8s %8lu %9lu %8.3f %10.1f %10.1f\n",
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

  bench_out_println("=== CLKDIV HP BENCH ===");
  bench_out_printf("live_clkdiv=%s voice=%s (private clones; does not touch PIO)\n",
                   cdb_live_clkdiv_name(), cdb_voice_name());
#ifdef USE_FLOAT_VOICE_TASK
  bench_out_println("note=float voice ignores HIGH_PRECISION_CLKDIV; this A/B is fixed-path only");
#endif
  bench_out_println("speed=flag-path oscs=3  HP1=Q8 64/32 inline  HP0=Q4+32-bit inline");
  bench_out_printf("seq=0.01Hz 1..%d (same grid as accuracy)  jump=10 steps/semitone (~10× notes)\n",
                   CDB_HZ_MAX);
  bench_out_println("compare dump10 clkdiv math mean ≈ meanUs/frame; ranking from pctVsHP1 (jump)");
  bench_out_printf("y=%lu w=%lu k=%lu (free-run) sys=%lu Hz\n",
                   (unsigned long)y, (unsigned long)w, (unsigned long)k,
                   (unsigned long)sys_hz);

  {
    uint32_t frames[CDB_HP_METHODS] = {0};
    uint64_t totalUs[CDB_HP_METHODS] = {0};
    for (uint8_t method = 0; method < CDB_HP_METHODS; ++method) {
      cdb_speed_run_seq(method, sys_hz, y, w, k, &frames[method], &totalUs[method]);
    }
    cdb_speed_print_table("seq", seqUnique, 1u, frames, totalUs);
  }
  {
    uint32_t frames[CDB_HP_METHODS] = {0};
    uint64_t totalUs[CDB_HP_METHODS] = {0};
    for (uint8_t method = 0; method < CDB_HP_METHODS; ++method) {
      cdb_speed_run(method, cdb_jump_q24, cdb_jump_n, jumpRepeats, sys_hz, y, w, k,
                    &frames[method], &totalUs[method]);
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
  } acc[CDB_HP_METHODS];

  memset(acc, 0, sizeof(acc));

  uint32_t disagreeN = 0;
  uint32_t maxHpDelta = 0;

  for (int32_t cHz = CDB_CHZ_MIN; cHz <= cHzMax; ++cHz) {
    double hz = (double)cHz * 0.01;
    int64_t freq_q24 = cdb_hz_to_q24(hz);
    uint32_t gold_total = (uint32_t)llround((double)sys_hz / hz);
    uint32_t gold_div = pio_clk_div_for_y(gold_total + CDB_CORRECTION, y, w, k);

    uint32_t divs[CDB_HP_METHODS];
    divs[CDB_HP1] = cdb_clk_div_hp1(sys_hz, freq_q24, y, w, k);
    divs[CDB_HP0] = cdb_clk_div_hp0(sys_hz, freq_q24, y, w, k);

    uint32_t hpDelta = (divs[CDB_HP0] > divs[CDB_HP1])
                           ? (divs[CDB_HP0] - divs[CDB_HP1])
                           : (divs[CDB_HP1] - divs[CDB_HP0]);
    if (hpDelta != 0) ++disagreeN;
    if (hpDelta > maxHpDelta) maxHpDelta = hpDelta;

    const int band = cdb_hz_band(hz);
    for (uint8_t method = 0; method < CDB_HP_METHODS; ++method) {
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
      uint32_t dDelta = (divs[method] > gold_div)
                            ? (divs[method] - gold_div)
                            : (gold_div - divs[method]);
      if (dDelta > a.maxDivDelta) a.maxDivDelta = dDelta;
      cdb_hist_add(a.hist, c);
      a.bandSum[band] += c;
      a.bandN[band]++;
      if (c > a.bandMax[band]) a.bandMax[band] = c;
    }
  }

  uint32_t nSamples = acc[CDB_HP1].n;

  bench_out_println("=== CLKDIV HP ACCURACY ===");
  bench_out_printf("live_clkdiv=%s voice=%s | gold=double round total_cycles + pio_clk_div_for_y\n",
                   cdb_live_clkdiv_name(), cdb_voice_name());
  bench_out_printf("Q4≈±1/32 Hz  Q8≈±1/512 Hz before PIO quant (Y locked, not note-on split)\n");
  bench_out_printf("y=%lu w=%lu k=%lu (free-run) sys=%lu Hz\n",
                   (unsigned long)y, (unsigned long)w, (unsigned long)k,
                   (unsigned long)sys_hz);
  bench_out_printf("grid=0.01Hz 1..%d | n=%lu | cents vs target Hz after Y-locked clk_div\n",
                   CDB_HZ_MAX, (unsigned long)nSamples);

  bench_out_println("\n-- vs target Hz --");
  bench_out_println("method   mean¢    max¢   p50¢   p95¢   p99¢  >0.1¢%  >0.5¢%  >1.0¢%  max|Δdiv|");
  for (uint8_t method = 0; method < CDB_HP_METHODS; ++method) {
    Acc &a = acc[method];
    double mean = (a.n > 0) ? (a.sumCents / (double)a.n) : 0.0;
    double p50 = cdb_hist_percentile(a.hist, a.n, 50.0);
    double p95 = cdb_hist_percentile(a.hist, a.n, 95.0);
    double p99 = cdb_hist_percentile(a.hist, a.n, 99.0);
    double pct01 = (a.n > 0) ? (100.0 * (double)a.worse01 / (double)a.n) : 0.0;
    double pct05 = (a.n > 0) ? (100.0 * (double)a.worse05 / (double)a.n) : 0.0;
    double pct10 = (a.n > 0) ? (100.0 * (double)a.worse10 / (double)a.n) : 0.0;
    bench_out_printf("%-8s %6.3f %7.3f %6.3f %6.3f %6.3f %6.2f %6.2f %6.2f %10lu\n",
                     cdb_method_name(method),
                     mean, a.maxCents, p50, p95, p99, pct01, pct05, pct10,
                     (unsigned long)a.maxDivDelta);
    bench_out_printf("         max@%.2f Hz\n", (double)a.hzAtMax);
  }

  bench_out_println("\n-- vs target by Hz-band (low<100, mid<1000, high) --");
  bench_out_println("method   low mean/max¢      mid mean/max¢      high mean/max¢");
  for (uint8_t method = 0; method < CDB_HP_METHODS; ++method) {
    Acc &a = acc[method];
    double m0 = (a.bandN[0] > 0) ? (a.bandSum[0] / (double)a.bandN[0]) : 0.0;
    double m1 = (a.bandN[1] > 0) ? (a.bandSum[1] / (double)a.bandN[1]) : 0.0;
    double m2 = (a.bandN[2] > 0) ? (a.bandSum[2] / (double)a.bandN[2]) : 0.0;
    bench_out_printf("%-8s %7.3f/%7.3f   %7.3f/%7.3f   %7.3f/%7.3f\n",
                     cdb_method_name(method),
                     m0, a.bandMax[0], m1, a.bandMax[1], m2, a.bandMax[2]);
  }

  double disagreePct = (nSamples > 0) ? (100.0 * (double)disagreeN / (double)nSamples) : 0.0;
  bench_out_println("\n-- HP0 vs HP1 clk_div --");
  bench_out_printf("disagree=%lu / %lu (%.3f%%)  max|Δdiv|=%lu\n",
                   (unsigned long)disagreeN, (unsigned long)nSamples, disagreePct,
                   (unsigned long)maxHpDelta);

  bench_out_println("\n-- human --");
  bench_out_printf("Y-locked free-run (y=%lu w=%lu k=%lu): cents vs target Hz = Q quant + PIO, not gold Δdiv.\n",
                   (unsigned long)y, (unsigned long)w, (unsigned long)k);
  bench_out_println("p50/p95/p99 use 0.5¢ hist bins (0.250=bin0); trust mean / max / >x¢%.");
  bench_out_println("max|Δdiv| at ~1 Hz is huge clk_div counts, not pitch. Disagree% ≠ audible error.");

  for (uint8_t method = 0; method < CDB_HP_METHODS; ++method) {
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
    bench_out_printf("  low <100 Hz:      mean %.3f¢  max %.3f¢  (Q4 vs Q8)\n",
                     m0, a.bandMax[0]);
    bench_out_printf("  mid 100-1000 Hz:  mean %.3f¢  max %.3f¢\n", m1, a.bandMax[1]);
    bench_out_printf("  high >1000 Hz:    mean %.3f¢  max %.3f¢  (PIO ±2 cyc, not HP)\n",
                     m2, a.bandMax[2]);
    bench_out_printf("  playing range:    max %.3f¢ (mid+high)\n", playMax);
  }

  {
    Acc &h1 = acc[CDB_HP1];
    Acc &h0 = acc[CDB_HP0];
    double hp1Play = (h1.bandMax[1] > h1.bandMax[2]) ? h1.bandMax[1] : h1.bandMax[2];
    double hp0Play = (h0.bandMax[1] > h0.bandMax[2]) ? h0.bandMax[1] : h0.bandMax[2];
    bench_out_println("\nverdict:");
    if (hp0Play <= hp1Play * 1.5 + 0.05) {
      bench_out_printf("  playing range (mid+high): HP0 ≈ HP1 (%.3f¢ vs %.3f¢ max) — PIO-limited.\n",
                       hp0Play, hp1Play);
    } else {
      bench_out_printf("  playing range (mid+high): HP1 better (%.3f¢ vs HP0 %.3f¢ max).\n",
                       hp1Play, hp0Play);
    }
    bench_out_printf("  low-Hz tail: HP1 max %.3f¢ vs HP0 %.3f¢ at <100 Hz (Q8 vs Q4).\n",
                     h1.bandMax[0], h0.bandMax[0]);
    bench_out_printf("  HP0 vs HP1 clk_div disagree %.1f%% — integer mismatch, not cents.\n",
                     disagreePct);
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
