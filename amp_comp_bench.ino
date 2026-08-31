#include "globals.h"

// Amp-comp speed / accuracy benches. Needs AMP_COMP_BENCHMARK + RUNNING_AVERAGE.
// Results go through bench_out_* and paced Core 0 TX (never Serial from Core 1).
// Cmds 24/25 install a linear synthetic cal (not LittleFS), measure, then restore.

#if defined(AMP_COMP_BENCHMARK) && defined(RUNNING_AVERAGE) && defined(USE_FLOAT_AMP_COMP)

volatile bool amp_comp_bench_speed_pending = false;
volatile bool amp_comp_bench_accuracy_pending = false;

static constexpr int AMP_COMP_BENCH_METHODS = 4; // FLOAT_QUAD, LUT, Q16_FAST, Q24_ULTRA
// One osc is enough to rank methods (synthetic cal is identical per osc). Cuts wall time ~3×.
static constexpr int AMP_COMP_BENCH_OSCS = 1;
// Shared centi-Hz grid with accuracy: 1.00 … AMP_COMP_MAX_HZ inclusive.
static constexpr int32_t AMP_COMP_BENCH_CHZ_MIN = 100;

// Snapshot of live FS-derived breakpoints so benches can restore after synth.
static float   s_liveFreqHz[NUM_OSCILLATORS][ampCompTableSize + 1];
static int32_t s_liveAmp[NUM_OSCILLATORS][ampCompTableSize + 1];
static float   s_liveHighestFreq[NUM_OSCILLATORS];
static bool    s_liveSnapValid = false;

// ---------------------------------------------------------------------------
// Zero-tax string formatters avoiding newlib-nano %f drops
// ---------------------------------------------------------------------------
static inline void fmt_u_dec1(char *buf, size_t sz, uint64_t val_x10) {
  snprintf(buf, sz, "%lu.%lu", (unsigned long)(val_x10 / 10u), (unsigned long)(val_x10 % 10u));
}

static inline void fmt_u_dec2(char *buf, size_t sz, uint64_t val_x100) {
  snprintf(buf, sz, "%lu.%02lu", (unsigned long)(val_x100 / 100u), (unsigned long)(val_x100 % 100u));
}

static inline void fmt_u_dec3(char *buf, size_t sz, uint64_t val_x1000) {
  snprintf(buf, sz, "%lu.%03lu", (unsigned long)(val_x1000 / 1000u), (unsigned long)(val_x1000 % 1000u));
}

static const char* amp_comp_bench_method_name(int m) {
  switch(m) {
    case 0: return "FLOAT_QUAD";
    case 1: return "LUT (flt)";
    case 2: return "Q16_FAST";
    case 3: return "Q24_ULTRA";
    default: return "UNKNOWN";
  }
}

static void amp_comp_bench_snapshot_live() {
  memcpy(s_liveFreqHz, ampCompFrequencyHz, sizeof(s_liveFreqHz));
  memcpy(s_liveAmp, ampCompArray, sizeof(s_liveAmp));
  memcpy(s_liveHighestFreq, highestFreqFoundHz, sizeof(s_liveHighestFreq));
  s_liveSnapValid = true;
}

static void amp_comp_bench_restore_live() {
  if (!s_liveSnapValid) return;
  memcpy(ampCompFrequencyHz, s_liveFreqHz, sizeof(s_liveFreqHz));
  memcpy(ampCompArray, s_liveAmp, sizeof(s_liveAmp));
  memcpy(highestFreqFoundHz, s_liveHighestFreq, sizeof(s_liveHighestFreq));
  precompute_amp_comp_for_engine();
}

// Linear fake cal: Hz 1 … AMP_COMP_MAX_HZ, levels 1 … DIV_COUNTER (same for all oscs).
static void amp_comp_bench_install_synthetic_linear() {
  const int n = ampCompTableSize; // 22 breakpoints; slot n is sentinel from precompute
  for (uint8_t o = 0; o < NUM_OSCILLATORS; ++o) {
    highestFreqFoundHz[o] = (float)AMP_COMP_MAX_HZ;
    for (int i = 0; i < n; ++i) {
      // Inclusive endpoints: i=0 → 1 Hz / level 1; i=n-1 → MAX_HZ / DIV_COUNTER
      float t = (n <= 1) ? 0.0f : (float)i / (float)(n - 1);
      float hz = 1.0f + t * ((float)AMP_COMP_MAX_HZ - 1.0f);
      float lvl = 1.0f + t * ((float)DIV_COUNTER - 1.0f);
      ampCompFrequencyHz[o][i] = hz;
      ampCompArray[o][i] = (int32_t)lroundf(lvl);
    }
    ampCompFrequencyHz[o][n] = (float)AMP_COMP_MAX_HZ;
    ampCompArray[o][n] = (int32_t)DIV_COUNTER;
  }
  precompute_amp_comp_for_engine();
}

static void amp_comp_bench_with_synthetic(void (*fn)()) {
  amp_comp_bench_snapshot_live();
  amp_comp_bench_install_synthetic_linear();
  fn();
  amp_comp_bench_restore_live();
}

static void amp_comp_bench_reset_win_cache() {
  for (int i = 0; i < NUM_OSCILLATORS; ++i) ampWinCache[i] = -1;
}

static void amp_comp_bench_run_speed_body() {
  const int32_t cHzMax = AMP_COMP_MAX_HZ * 100;

  uint32_t totalCalls[AMP_COMP_BENCH_METHODS] = {0};
  uint64_t totalUs[AMP_COMP_BENCH_METHODS] = {0};

  for (uint8_t method = 0; method < AMP_COMP_BENCH_METHODS; ++method) {
    amp_comp_bench_reset_win_cache();
    uint32_t t0 = micros();
    uint32_t calls = 0;
    volatile uint16_t sink = 0;

    for (uint8_t o = 0; o < AMP_COMP_BENCH_OSCS; ++o) {
      if (method == 0) {
        for (int32_t cHz = AMP_COMP_BENCH_CHZ_MIN; cHz <= cHzMax; ++cHz) {
          float f = (float)cHz * 0.01f;
          sink = (uint16_t)(sink + get_chan_level_float_quad(f, o));
          ++calls;
        }
      } else if (method == 1) {
        for (int32_t cHz = AMP_COMP_BENCH_CHZ_MIN; cHz <= cHzMax; ++cHz) {
          float f = (float)cHz * 0.01f;
          sink = (uint16_t)(sink + get_chan_level_lut(f, o));
          ++calls;
        }
      } else if (method == 2) {
        for (int32_t cHz = AMP_COMP_BENCH_CHZ_MIN; cHz <= cHzMax; ++cHz) {
          float f = (float)cHz * 0.01f;
          uint32_t freq_q16 = (uint32_t)(f * 65536.0f);
          sink = (uint16_t)(sink + get_chan_level_q16_fast(freq_q16, o));
          ++calls;
        }
      } else if (method == 3) {
        for (int32_t cHz = AMP_COMP_BENCH_CHZ_MIN; cHz <= cHzMax; ++cHz) {
          float f = (float)cHz * 0.01f;
          int64_t freq_q24 = (int64_t)(f * 16777216.0f);
          sink = (uint16_t)(sink + get_chan_level_q24_ultra_accurate(freq_q24, o));
          ++calls;
        }
      }
    }

    uint32_t t1 = micros();
    totalCalls[method] = calls;
    totalUs[method] = (uint64_t)(t1 - t0);
    (void)sink;
  }

  uint64_t refUs = totalUs[0];
  if (refUs < 1) refUs = 1;

  bench_out_println("=== AMP COMP BENCH ===");
  bench_out_println("cal=SYNTHETIC linear Hz/level 1..MAX / 1..DIV_COUNTER");
  bench_out_printf("grid=0.01Hz 1..%lu oscs=%d (same as accuracy)\n",
                   (unsigned long)AMP_COMP_MAX_HZ, AMP_COMP_BENCH_OSCS);
  bench_out_printf("live_method=%s (restored after sweep)\n",
                   amp_comp_method_name(amp_comp_method));
  bench_out_println("method           calls    totalUs   meanNs  pctVsFloat");
  bench_out_println("---------------- -------- --------- -------- ----------");
  for (uint8_t method = 0; method < AMP_COMP_BENCH_METHODS; ++method) {
    uint64_t mean_ns_x100 = (totalCalls[method] > 0) ? ((totalUs[method] * 100000ULL + (totalCalls[method] / 2)) / totalCalls[method]) : 0;
    uint64_t pct_x10 = (refUs > 0) ? ((totalUs[method] * 1000ULL + (refUs / 2)) / refUs) : 0;

    char mean_str[24], pct_str[24];
    fmt_u_dec2(mean_str, sizeof(mean_str), mean_ns_x100);
    fmt_u_dec1(pct_str, sizeof(pct_str), pct_x10);

    bench_out_printf("%-16s %8lu %9lu %8s %10s\n",
                     amp_comp_bench_method_name(method),
                     (unsigned long)totalCalls[method],
                     (unsigned long)totalUs[method],
                     mean_str,
                     pct_str);
  }
  bench_out_println("======================");
}

static void amp_comp_bench_run_accuracy_body() {
  struct Acc {
    uint64_t sumAbs;
    uint32_t maxInBand;
    uint32_t hzAtMaxInBand_cHz;
    uint32_t tipMax;
    uint32_t tipErrGt1;
    uint32_t n;
    uint32_t errGt1;
    uint32_t errEq1;
  } acc[AMP_COMP_BENCH_METHODS];

  memset(acc, 0, sizeof(acc));
  amp_comp_bench_reset_win_cache();

  const int32_t cHzMax = AMP_COMP_MAX_HZ * 100;
  
  for (uint8_t o = 0; o < AMP_COMP_BENCH_OSCS; ++o) {
    for (int32_t cHz = AMP_COMP_BENCH_CHZ_MIN; cHz <= cHzMax; ++cHz) {
      float f = (float)cHz * 0.01f;

      uint16_t ref = get_chan_level_float_quad(f, o);

      for (uint8_t method = 1; method < AMP_COMP_BENCH_METHODS; ++method) {
        uint16_t y = 0;
        if (method == 1) {
          y = get_chan_level_lut(f, o);
        } else if (method == 2) {
          uint32_t freq_q16 = (uint32_t)(f * 65536.0f);
          y = get_chan_level_q16_fast(freq_q16, o);
        } else if (method == 3) {
          int64_t freq_q24 = (int64_t)(f * 16777216.0f);
          y = get_chan_level_q24_ultra_accurate(freq_q24, o);
        }

        uint32_t e = (y > ref) ? (uint32_t)(y - ref) : (uint32_t)(ref - y);
        Acc &a = acc[method];
        a.sumAbs += e;
        a.n++;
        if (e > 1) a.errGt1++;
        if (e == 1) a.errEq1++;

        if (cHz >= cHzMax) {
          if (e > a.tipMax) a.tipMax = e;
          if (e > 1) a.tipErrGt1++;
        } else if (e > a.maxInBand) {
          a.maxInBand = e;
          a.hzAtMaxInBand_cHz = (uint32_t)cHz;
        }
      }
    }
  }

  uint32_t lutIntMaxErr = 0;
  for (uint8_t o = 0; o < AMP_COMP_BENCH_OSCS; ++o) {
    for (int32_t hz = 0; hz <= AMP_COMP_MAX_HZ; ++hz) {
      uint16_t ref = get_chan_level_float_quad((float)hz, o);
      uint16_t y = get_chan_level_lut((float)hz, o);
      uint32_t e = (y > ref) ? (uint32_t)(y - ref) : (uint32_t)(ref - y);
      if (e > lutIntMaxErr) lutIntMaxErr = e;
    }
  }

  uint32_t nSamples = acc[1].n;

  bench_out_println("=== AMP COMP ACCURACY ===");
  bench_out_printf("Errors vs FLOAT_QUAD in RANGE PWM counts (full scale = %u)\n",
                   (unsigned)DIV_COUNTER);
  bench_out_printf("cal=SYNTHETIC linear Hz/level 1..MAX / 1..DIV_COUNTER | "
                   "grid=0.01Hz 1..%lu oscs=%d | n=%lu samples/method\n",
                   (unsigned long)AMP_COMP_MAX_HZ, AMP_COMP_BENCH_OSCS, (unsigned long)nSamples);
  bench_out_printf("LUT integer-Hz sanity: max err %lu PWM (want 0)\n",
                   (unsigned long)lutIntMaxErr);

  for (uint8_t method = 1; method < AMP_COMP_BENCH_METHODS; ++method) {
    Acc &a = acc[method];
    uint64_t mean_x100 = (a.n > 0) ? ((a.sumAbs * 100ULL + (a.n / 2)) / a.n) : 0;
    uint64_t meanPct_x1000 = (a.n > 0 && DIV_COUNTER > 0) ? ((a.sumAbs * 100000ULL + ((uint64_t)a.n * DIV_COUNTER / 2)) / ((uint64_t)a.n * DIV_COUNTER)) : 0;
    uint64_t inBandPct_x1000 = (DIV_COUNTER > 0) ? (((uint64_t)a.maxInBand * 100000ULL + (DIV_COUNTER / 2)) / DIV_COUNTER) : 0;
    uint64_t tipPct_x10 = (DIV_COUNTER > 0) ? (((uint64_t)a.tipMax * 1000ULL + (DIV_COUNTER / 2)) / DIV_COUNTER) : 0;
    uint64_t gt1Pct_x1000 = (a.n > 0) ? (((uint64_t)a.errGt1 * 100000ULL + (a.n / 2)) / a.n) : 0;
    uint64_t eq1Pct_x1000 = (a.n > 0) ? (((uint64_t)a.errEq1 * 100000ULL + (a.n / 2)) / a.n) : 0;

    char strA[24], strB[24];

    bench_out_printf("\n--- %s ---\n", amp_comp_bench_method_name(method));

    fmt_u_dec2(strA, sizeof(strA), mean_x100);
    fmt_u_dec3(strB, sizeof(strB), meanPct_x1000);
    bench_out_printf("  typical (mean):     %s PWM   (%s%% full)\n", strA, strB);

    if (a.maxInBand > 0) {
      fmt_u_dec2(strA, sizeof(strA), (uint64_t)a.hzAtMaxInBand_cHz);
      fmt_u_dec3(strB, sizeof(strB), inBandPct_x1000);
      bench_out_printf("  worst in-band:      %lu PWM at %s Hz   (%s%% full)\n",
                       (unsigned long)a.maxInBand, strA, strB);
    } else {
      bench_out_println("  worst in-band:      none >0 PWM");
    }

    if (a.tipErrGt1 > 0) {
      fmt_u_dec1(strA, sizeof(strA), tipPct_x10);
      bench_out_printf("  tip outlier:        %lu PWM at %lu.00 Hz  (%s%% full)  x%lu samples\n",
                       (unsigned long)a.tipMax,
                       (unsigned long)AMP_COMP_MAX_HZ,
                       strA,
                       (unsigned long)a.tipErrGt1);
    }

    fmt_u_dec3(strA, sizeof(strA), gt1Pct_x1000);
    if (a.errGt1 > 0 && a.errGt1 == a.tipErrGt1) {
      bench_out_printf("  worse than 1 PWM:   %lu / %lu  (%s%%)  (all at tip)\n",
                       (unsigned long)a.errGt1, (unsigned long)a.n, strA);
    } else {
      bench_out_printf("  worse than 1 PWM:   %lu / %lu  (%s%%)\n",
                       (unsigned long)a.errGt1, (unsigned long)a.n, strA);
    }

    fmt_u_dec3(strB, sizeof(strB), eq1Pct_x1000);
    bench_out_printf("  exactly 1 PWM:      %lu / %lu  (%s%%)\n",
                     (unsigned long)a.errEq1, (unsigned long)a.n, strB);
  }
  bench_out_println("=========================");
}

void amp_comp_bench_run_speed() {
  amp_comp_bench_with_synthetic(amp_comp_bench_run_speed_body);
}

void amp_comp_bench_run_accuracy() {
  amp_comp_bench_with_synthetic(amp_comp_bench_run_accuracy_body);
}

void print_amp_comp_bench() {
  if (amp_comp_bench_speed_pending) {
    amp_comp_bench_speed_pending = false;
    amp_comp_bench_run_speed();
  }
  if (amp_comp_bench_accuracy_pending) {
    amp_comp_bench_accuracy_pending = false;
    amp_comp_bench_run_accuracy();
  }
}

#else  // !AMP_COMP_BENCHMARK || !RUNNING_AVERAGE || !USE_FLOAT_AMP_COMP

volatile bool amp_comp_bench_speed_pending = false;
volatile bool amp_comp_bench_accuracy_pending = false;

void amp_comp_bench_run_speed() {}
void amp_comp_bench_run_accuracy() {}
void print_amp_comp_bench() {}

#endif