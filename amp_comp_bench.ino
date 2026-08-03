#include "include_all.h"

// Amp-comp speed / accuracy benches. Gated like CLKDIV_BENCHMARK.
// Results go through bench_out_* and paced Core 0 TX (never Serial from Core 1).
// Cmds 24/25 install a linear synthetic cal (not LittleFS), measure, then restore.

#if defined(AMP_COMP_BENCHMARK) && defined(RUNNING_AVERAGE) && defined(USE_FLOAT_AMP_COMP)

volatile bool amp_comp_bench_speed_pending = false;
volatile bool amp_comp_bench_accuracy_pending = false;

static constexpr int AMP_COMP_BENCH_METHODS = 3; // FLOAT_QUAD, LUT, FIXED
// One osc is enough to rank methods (synthetic cal is identical per osc). Cuts wall time ~3×.
static constexpr int AMP_COMP_BENCH_OSCS = 1;
// Shared centi-Hz grid with accuracy: 1.00 … AMP_COMP_MAX_HZ inclusive.
static constexpr int32_t AMP_COMP_BENCH_CHZ_MIN = 100;

// Snapshot of live FS-derived breakpoints so benches can restore after synth.
static float   s_liveFreqHz[NUM_OSCILLATORS][ampCompTableSize + 1];
static int32_t s_liveAmp[NUM_OSCILLATORS][ampCompTableSize + 1];
static bool    s_liveSnapValid = false;

static void amp_comp_bench_snapshot_live() {
  memcpy(s_liveFreqHz, ampCompFrequencyHz, sizeof(s_liveFreqHz));
  memcpy(s_liveAmp, ampCompArray, sizeof(s_liveAmp));
  s_liveSnapValid = true;
}

static void amp_comp_bench_restore_live() {
  if (!s_liveSnapValid) return;
  memcpy(ampCompFrequencyHz, s_liveFreqHz, sizeof(s_liveFreqHz));
  memcpy(ampCompArray, s_liveAmp, sizeof(s_liveAmp));
  precompute_amp_comp_for_engine();
}

// Linear fake cal: Hz 1 … AMP_COMP_MAX_HZ, levels 1 … DIV_COUNTER (same for all oscs).
static void amp_comp_bench_install_synthetic_linear() {
  const int n = ampCompTableSize; // 22 breakpoints; slot n is sentinel from precompute
  for (uint8_t o = 0; o < NUM_OSCILLATORS; ++o) {
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

static uint16_t amp_comp_call_method(uint8_t method, float freqHz, uint8_t voiceN) {
  switch (method) {
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

static void amp_comp_bench_run_speed_body() {
  // Same 0.01 Hz grid as accuracy (one pass, AMP_COMP_BENCH_OSCS). Heavy one-shot.
  const int32_t cHzMax = AMP_COMP_MAX_HZ * 100;

  uint32_t totalCalls[AMP_COMP_BENCH_METHODS] = {0};
  uint64_t totalUs[AMP_COMP_BENCH_METHODS] = {0};

  for (uint8_t method = 0; method < AMP_COMP_BENCH_METHODS; ++method) {
    uint32_t t0 = micros();
    uint32_t calls = 0;
    volatile uint16_t sink = 0;

    for (uint8_t o = 0; o < AMP_COMP_BENCH_OSCS; ++o) {
      for (int32_t cHz = AMP_COMP_BENCH_CHZ_MIN; cHz <= cHzMax; ++cHz) {
        float f = (float)cHz * 0.01f;
        sink = (uint16_t)(sink + amp_comp_call_method(method, f, o));
        ++calls;
      }
    }

    uint32_t t1 = micros();
    totalCalls[method] = calls;
    totalUs[method] = (uint64_t)(t1 - t0);
    (void)sink;
  }

  double refUs = (double)totalUs[AMP_COMP_FLOAT_QUAD];
  if (refUs < 1.0) refUs = 1.0;

  bench_out_println("=== AMP COMP BENCH ===");
  bench_out_println("cal=SYNTHETIC linear Hz/level 1..MAX / 1..DIV_COUNTER");
  bench_out_printf("grid=0.01Hz 1..%.0f oscs=%d (same as accuracy)\n",
                   (double)AMP_COMP_MAX_HZ, AMP_COMP_BENCH_OSCS);
  bench_out_printf("live_method=%s\n", amp_comp_method_name(amp_comp_method));
  bench_out_println("method       calls    totalUs   meanNs  pctVsFloat");
  bench_out_println("------------ -------- --------- -------- ----------");
  for (uint8_t method = 0; method < AMP_COMP_BENCH_METHODS; ++method) {
    double meanNs = ((double)totalUs[method] * 1000.0) / (double)totalCalls[method];
    double pct = 100.0 * (double)totalUs[method] / refUs;
    bench_out_printf("%-12s %8lu %9lu %8.2f %10.1f\n",
                     amp_comp_method_name(method),
                     (unsigned long)totalCalls[method],
                     (unsigned long)totalUs[method],
                     meanNs,
                     pct);
  }
  bench_out_println("======================");
}

static void amp_comp_bench_run_accuracy_body() {
  struct Acc {
    double sumAbs;
    double maxInBand;
    float hzAtMaxInBand;
    double tipMax;
    uint32_t tipErrGt1;
    uint32_t n;
    uint32_t errGt1;
    uint32_t errEq1;
  } acc[AMP_COMP_BENCH_METHODS];

  memset(acc, 0, sizeof(acc));

  // Inclusive 1.00 … AMP_COMP_MAX_HZ (matches synthetic / typical first cal knot at 1 Hz).
  const int32_t cHzMax = AMP_COMP_MAX_HZ * 100;
  const float tipHz = (float)AMP_COMP_MAX_HZ;
  for (uint8_t o = 0; o < AMP_COMP_BENCH_OSCS; ++o) {
    for (int32_t cHz = AMP_COMP_BENCH_CHZ_MIN; cHz <= cHzMax; ++cHz) {
      float f = (float)cHz * 0.01f; // exact 2-decimal musical grid

      uint16_t ref = get_chan_level_float_quad(f, o);

      for (uint8_t method = 0; method < AMP_COMP_BENCH_METHODS; ++method) {
        if (method == AMP_COMP_FLOAT_QUAD) continue;
        uint16_t y = amp_comp_call_method(method, f, o);
        double e = fabs((double)y - (double)ref);
        Acc &a = acc[method];
        a.sumAbs += e;
        a.n++;
        if (e > 1.0) a.errGt1++;
        if (e == 1.0) a.errEq1++;

        if (f >= tipHz) {
          if (e > a.tipMax) a.tipMax = e;
          if (e > 1.0) a.tipErrGt1++;
        } else if (e > a.maxInBand) {
          a.maxInBand = e;
          a.hzAtMaxInBand = f;
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

  // n is the same for every compared method.
  uint32_t nSamples = 0;
  for (uint8_t method = 0; method < AMP_COMP_BENCH_METHODS; ++method) {
    if (method == AMP_COMP_FLOAT_QUAD) continue;
    nSamples = acc[method].n;
    break;
  }

  bench_out_println("=== AMP COMP ACCURACY ===");
  bench_out_printf("Errors vs FLOAT_QUAD in RANGE PWM counts (full scale = %u)\n",
                   (unsigned)DIV_COUNTER);
  bench_out_printf("cal=SYNTHETIC linear Hz/level 1..MAX / 1..DIV_COUNTER | "
                   "grid=0.01Hz 1..%.0f oscs=%d | n=%lu samples/method\n",
                   (double)AMP_COMP_MAX_HZ, AMP_COMP_BENCH_OSCS, (unsigned long)nSamples);
  bench_out_printf("LUT integer-Hz sanity: max err %lu PWM (want 0)\n",
                   (unsigned long)lutIntMaxErr);

  for (uint8_t method = 0; method < AMP_COMP_BENCH_METHODS; ++method) {
    if (method == AMP_COMP_FLOAT_QUAD) continue;
    Acc &a = acc[method];
    double mean = (a.n > 0) ? (a.sumAbs / (double)a.n) : 0.0;
    double gt1Pct = (a.n > 0) ? (100.0 * (double)a.errGt1 / (double)a.n) : 0.0;
    double eq1Pct = (a.n > 0) ? (100.0 * (double)a.errEq1 / (double)a.n) : 0.0;
    double meanPct = 100.0 * mean / (double)DIV_COUNTER;
    double inBandPct = 100.0 * a.maxInBand / (double)DIV_COUNTER;
    double tipPct = 100.0 * a.tipMax / (double)DIV_COUNTER;

    bench_out_printf("\n--- %s ---\n", amp_comp_method_name(method));
    bench_out_printf("  typical (mean):     %.2f PWM   (%.3f%% full)\n",
                     mean, meanPct);

    if (a.maxInBand > 0.0) {
      bench_out_printf("  worst in-band:      %.0f PWM at %.2f Hz   (%.3f%% full)\n",
                       a.maxInBand, (double)a.hzAtMaxInBand, inBandPct);
    } else {
      bench_out_println("  worst in-band:      none >0 PWM");
    }

    // Tip outliers only when exact AMP_COMP_MAX_HZ disagrees by more than 1 PWM.
    if (a.tipErrGt1 > 0) {
      bench_out_printf("  tip outlier:        %.0f PWM at %.2f Hz  (%.1f%% full)  x%lu samples\n",
                       a.tipMax, (double)tipHz, tipPct,
                       (unsigned long)a.tipErrGt1);
    }

    if (a.errGt1 > 0 && a.errGt1 == a.tipErrGt1) {
      bench_out_printf("  worse than 1 PWM:   %lu / %lu  (%.3f%%)  (all at tip)\n",
                       (unsigned long)a.errGt1, (unsigned long)a.n, gt1Pct);
    } else {
      bench_out_printf("  worse than 1 PWM:   %lu / %lu  (%.3f%%)\n",
                       (unsigned long)a.errGt1, (unsigned long)a.n, gt1Pct);
    }
    bench_out_printf("  exactly 1 PWM:      %lu / %lu  (%.3f%%)\n",
                     (unsigned long)a.errEq1, (unsigned long)a.n, eq1Pct);
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
