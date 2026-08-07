#ifndef __BENCH_H__
#define __BENCH_H__

// Realtime profiler for the DCO hot path.
//
// Enabled by RUNNING_AVERAGE in DCO.ino; every macro below compiles to nothing when it is
// off, so the shipping build carries no cost. RUNNING_AVERAGE_FINE additionally enables the
// probes around the smallest stages (a few multiplies each) — useful for A/B-ing how much
// the instrumentation itself distorts the totals, since every probe is an optimisation
// barrier the compiler cannot move work across. RUNNING_AVERAGE_PERIOD keeps only
// BENCH_PERIOD (loop / loop1); all BENCH_BEGIN/END stage probes and path counters compile
// out — use that for a true loop baseline without intermediate probe tax. PERIOD overrides FINE.
//
// Time source: SysTick, read as a free-running 24-bit down-counter clocked from clk_sys.
// RP2040's Cortex-M0+ has no DWT cycle counter, so SysTick is the only single-cycle source
// available on both chips. Resolution is one clk_sys cycle (4.4 ns at 225 MHz).
//
// Range: 24 bits wraps after 2^24 cycles — ~74.5 ms at 225 MHz, ~126 ms at 133 MHz. Anything
// that can outlast that must use BENCH_PERIOD(), which reads the 1 us timer instead. The
// report flags any probe whose max lands near the wrap so a silent overflow is visible.

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "pico/platform.h"
#include "hardware/clocks.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/timer.h"

// Defined in voices.ino; prints the CLKDIV_BENCHMARK comparison when that flag is on.
void print_clkdiv_bench();
// Defined in amp_comp_bench.ino; prints AMP_COMP_BENCHMARK speed/accuracy when pending.
void print_amp_comp_bench();
// Defined in pitch_interp_bench.ino; pitch interp speed/accuracy when RUNNING_AVERAGE.
extern volatile bool pitch_interp_bench_speed_pending;
extern volatile bool pitch_interp_bench_accuracy_pending;
void print_pitch_interp_bench();

extern volatile bool pio_probe_report_pending;
void pio_probe_report_flush();

static inline const char *bench_pitch_interp_mode_name() {
#if PITCH_INTERP_MODE == PITCH_INTERP_FLOAT
  return "FLOAT";
#elif PITCH_INTERP_MODE == PITCH_INTERP_FLOAT_FAST
  return "FLOAT_FAST";
#elif PITCH_INTERP_MODE == PITCH_INTERP_RATIO_Q16
  return "RATIO_Q16";
#elif PITCH_INTERP_MODE == PITCH_INTERP_Q12
  return "Q12";
#else
  return "?";
#endif
}

// Set to 0 to fall back to the 1 us timer for every probe, e.g. if SysTick turns out to be
// claimed by the core (a FreeRTOS build uses it; plain arduino-pico drives millis()/micros()
// from time_us_64() and leaves it alone).
#ifndef BENCH_USE_SYSTICK
#define BENCH_USE_SYSTICK 1
#endif

// ---------------------------------------------------------------------------
// Probe table — the single source of truth.
//
// Storage, enum ids, core ownership, parent/child nesting and the printed labels are all
// generated from this list, so a probe cannot exist in one place and be missing in another.
// Adding a probe means adding one line here plus the BENCH_BEGIN/BENCH_END pair at the site.
//
//   X(id, core, kind, tier, parent, label)
// ---------------------------------------------------------------------------
#define BENCH_PROBES(X)                                                                       \
  /* --- Core 0: loop() --- */                                                                \
  X(loop0_period,      0, BENCH_US,  BENCH_T_MAIN, BENCH_NONE,          "loop period")         \
  X(loop0_midi,        0, BENCH_CYC, BENCH_T_MAIN, BENCH_loop0_period,  "MIDI read")           \
  X(loop0_serial,      0, BENCH_CYC, BENCH_T_MAIN, BENCH_loop0_period,  "serial panel/USB")    \
  X(loop0_lfo1,        0, BENCH_CYC, BENCH_T_MAIN, BENCH_loop0_period,  "LFO1")                \
  X(loop0_lfo2,        0, BENCH_CYC, BENCH_T_MAIN, BENCH_loop0_period,  "LFO2")                \
  X(loop0_drift,       0, BENCH_CYC, BENCH_T_MAIN, BENCH_loop0_period,  "drift LFOs")          \
  /* --- Core 1: loop1() --- */                                                               \
  X(loop1_period,      1, BENCH_US,  BENCH_T_MAIN, BENCH_NONE,          "loop1 period")        \
  X(loop1_millis,      1, BENCH_CYC, BENCH_T_MAIN, BENCH_loop1_period,  "millisTimer")         \
  X(loop1_noise,       1, BENCH_CYC, BENCH_T_MAIN, BENCH_loop1_period,  "noise_gens")          \
  X(loop1_noise_refill,1, BENCH_CYC, BENCH_T_MAIN, BENCH_loop1_noise,   "noise refill")        \
  X(loop1_adsr,        1, BENCH_CYC, BENCH_T_MAIN, BENCH_loop1_period,  "ADSR_update")         \
  X(loop1_cv_outs,     1, BENCH_CYC, BENCH_T_MAIN, BENCH_loop1_period,  "update_CV_outs")      \
  X(voice_task,        1, BENCH_CYC, BENCH_T_MAIN, BENCH_loop1_period,  "voice_task TOTAL")    \
  /* --- Core 1: inside voice_task --- */                                                     \
  X(vt_pitchbend,      1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "pitch bend")          \
  X(vt_osc_detune,     1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "OSC2/3 detune")       \
  X(vt_portamento,     1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "portamento")          \
  X(vt_adsr_mod,       1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "ADSR modifier")       \
  X(vt_unison_mod,     1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "unison modifier")     \
  X(vt_drift_mod,      1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "drift modifier")      \
  X(vt_modifiers,      1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "modifier sum")        \
  X(vt_freq_scale_x,   1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "table x scaling")     \
  X(vt_ratio_interp,   1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "ratio interpolate")   \
  X(vt_freq_scale_post,1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "apply ratio")         \
  X(vt_clk_div,        1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "clkdiv math")         \
  X(vt_phase_align,    1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "phase align")         \
  /* Exact split on note-on frames next to hot phase_align (not inside note-on block). */ \
  X(vt_retrig_split,   1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "retrig period split") \
  X(vt_chan_level,     1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "amp comp")            \
  X(vt_pio_write,      1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "PIO put/exec")        \
  X(vt_note_retrig,    1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "note-on retrigger")   \
  /* One SM-apply probe: fine MAIN slices mis-attribute cold XIP across load/jmp/enable. */ \
  X(vt_retrig_sm_apply,1, BENCH_CYC, BENCH_T_MAIN, BENCH_vt_note_retrig,"retrig SM apply")     \
  X(vt_retrig_pwm,     1, BENCH_CYC, BENCH_T_MAIN, BENCH_vt_note_retrig,"retrig RANGE PWM")    \
  X(vt_range_pwm,      1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "RANGE PWM")           \
  X(vt_pwm_calc,       1, BENCH_CYC, BENCH_T_FINE, BENCH_voice_task,    "PW arithmetic")       \
  X(vt_pw_update,      1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task,    "PW level + write")

enum BenchId {
#define BENCH_X(id, core, kind, tier, parent, label) BENCH_##id,
  BENCH_PROBES(BENCH_X)
#undef BENCH_X
  BENCH_COUNT
};
#define BENCH_NONE BENCH_COUNT

// Probe unit. BENCH_CYC values are clk_sys cycles, BENCH_US values are microseconds.
#define BENCH_CYC 0
#define BENCH_US  1

// Probe tier. FINE probes only collect when RUNNING_AVERAGE_FINE is defined.
#define BENCH_T_MAIN 0
#define BENCH_T_FINE 1

// ---------------------------------------------------------------------------
// Time source. Kept outside the RUNNING_AVERAGE guard so CLKDIV_BENCHMARK can use it
// on its own; with no probes compiled in, none of this is referenced and none of it
// costs anything at runtime.
// ---------------------------------------------------------------------------

// Cost of one back-to-back bench_now() pair, subtracted from every cycle sample.
uint32_t bench_overhead_cyc = 0;
uint32_t bench_cycles_per_us = 1;

// Wall-clock span each core's current set of samples was collected over, used for the
// "share of the core's time" column.
uint32_t bench_window_start_us[2] = { 0, 0 };
uint32_t bench_window_us[2] = { 0, 0 };

#if BENCH_USE_SYSTICK
#define BENCH_TIMER_MASK 0x00FFFFFFu
// SysTick counts down, so the earlier reading is the larger one.
static inline uint32_t bench_now(void) {
  return systick_hw->cvr;
}
static inline uint32_t bench_span(uint32_t start, uint32_t end) {
  return (start - end) & BENCH_TIMER_MASK;
}
#else
#define BENCH_TIMER_MASK 0xFFFFFFFFu
static inline uint32_t bench_now(void) {
  return timer_hw->timerawl;
}
static inline uint32_t bench_span(uint32_t start, uint32_t end) {
  return end - start;
}
#endif

// Free-running 1 us reading for the loop-period probes, which can outlast the 24-bit wrap.
static inline uint32_t bench_us_now(void) {
  return timer_hw->timerawl;
}

// Per core, at boot. SysTick is a core-local peripheral: core 0 and core 1 each have their
// own, and arming one does nothing for the other.
inline void bench_init_core() {
  bench_cycles_per_us = clock_get_hz(clk_sys) / 1000000u;
  if (bench_cycles_per_us == 0u) bench_cycles_per_us = 1u;

#if BENCH_USE_SYSTICK
  systick_hw->csr = 0u;
  systick_hw->rvr = BENCH_TIMER_MASK;
  systick_hw->cvr = 0u;
  systick_hw->csr = 0x5u;  // ENABLE | CLKSOURCE=processor, interrupt off
#endif

  // Minimum of a run of back-to-back reads: the cleanest estimate of the probe's own cost,
  // since any larger sample picked up an interrupt. Both cores compute the same value.
  uint32_t best = 0xFFFFFFFFu;
  for (int i = 0; i < 32; ++i) {
    const uint32_t a = bench_now();
    const uint32_t b = bench_now();
    const uint32_t d = bench_span(a, b);
    if (d < best) best = d;
  }
  bench_overhead_cyc = best;

#ifdef RUNNING_AVERAGE
  bench_window_start_us[get_core_num() & 1u] = bench_us_now();
#endif
}

// ---- Paced USB CDC output (core 0, non-blocking) ---------------------------
// Shared by the profiler and PIO debug reports. Never blocks on Serial.write.

volatile bool bench_out_active = false;

#define BENCH_OUT_CAP   6144
#define BENCH_OUT_CHUNK 64

char bench_out_buf[BENCH_OUT_CAP];
uint16_t bench_out_len = 0;
uint16_t bench_out_pos = 0;

inline void bench_out_reset() {
  bench_out_len = 0;
  bench_out_pos = 0;
}

inline void bench_out_puts(const char *s) {
  while (*s != '\0' && bench_out_len + 1u < BENCH_OUT_CAP) {
    bench_out_buf[bench_out_len++] = *s++;
  }
}

inline void bench_out_printf(const char *fmt, ...) {
  char tmp[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  bench_out_puts(tmp);
}

inline void bench_out_println(const char *s) {
  bench_out_puts(s);
  bench_out_puts("\n");
}

// True when the host is still sending USB CDC frames (slider drag, etc.).
inline bool bench_cdc_rx_pending() {
#ifdef ENABLE_USB_CONTROL
  return Serial.available() > 0;
#else
  return false;
#endif
}

// Drain up to BENCH_OUT_CHUNK bytes without blocking. Returns true while more remains.
inline bool bench_out_drain_chunk() {
  if (!bench_out_active) return false;
  if (bench_cdc_rx_pending()) return true;

  const uint16_t remain = (uint16_t)(bench_out_len - bench_out_pos);
  if (remain == 0u) {
    bench_out_active = false;
    bench_out_reset();
    return false;
  }

  int avail = Serial.availableForWrite();
  if (avail <= 0) return true;

  uint16_t n = remain;
  if (n > BENCH_OUT_CHUNK) n = (uint16_t)BENCH_OUT_CHUNK;
  if ((uint16_t)avail < n) n = (uint16_t)avail;

  Serial.write(reinterpret_cast<const uint8_t *>(bench_out_buf + bench_out_pos), n);
  bench_out_pos = (uint16_t)(bench_out_pos + n);

  if (bench_out_pos >= bench_out_len) {
    bench_out_active = false;
    bench_out_reset();
    return false;
  }
  return true;
}

#ifdef RUNNING_AVERAGE

struct BenchDesc {
  uint8_t core;
  uint8_t kind;
  uint8_t tier;
  uint8_t parent;
  const char *label;
};

static const BenchDesc bench_desc[BENCH_COUNT] = {
#define BENCH_X(id, core, kind, tier, parent, label) { core, kind, tier, parent, label },
  BENCH_PROBES(BENCH_X)
#undef BENCH_X
};

// 20 bytes per probe. The whole table costs less than a tenth of what one
// RunningAverage(2000) buffer did, and the add is integer-only — which matters on the
// RP2040, where every float operation is an __aeabi_* library call.
struct BenchStat {
  uint32_t n;
  uint32_t min;
  uint32_t max;
  uint64_t sum;
};

BenchStat bench_stats[BENCH_COUNT];
BenchStat bench_snap[BENCH_COUNT];

// Path counters (core 1 voice hot path). Cheap integer bumps — not time probes — so they
// do not add SysTick barriers. Snapshotted with core 1's profiler window to attribute
// ratio/portamento max spikes (algorithmic miss vs IRQ).
struct BenchPathStat {
  uint32_t ratio_hit;
  uint32_t ratio_miss_direct;   // O(1) direct index + tiny fixup
  uint32_t ratio_miss_bsearch;
  uint32_t ratio_clamp;
  uint32_t ratio_walk_steps_sum;
  uint32_t ratio_walk_steps_max;
  uint32_t amp_hit;
  uint32_t amp_miss_walk;
  uint32_t amp_miss_scan;
  uint32_t amp_clamp;
  uint32_t amp_walk_steps_sum;
  uint32_t amp_walk_steps_max;
  uint32_t porta_off;
  uint32_t porta_note_on;
  uint32_t porta_retime;
  uint32_t porta_steady_time;
  uint32_t porta_steady_slew;
};
BenchPathStat bench_path_live;
BenchPathStat bench_path_snap;

#ifdef RUNNING_AVERAGE_PERIOD
// Period-only: no path bumps (same spirit as stage BENCH_BEGIN/END no-ops).
#define BENCH_PATH_INC(field) ((void)0)
static inline void bench_path_walk_steps(uint32_t) {}
static inline void bench_path_amp_walk_steps(uint32_t) {}
#else
#define BENCH_PATH_INC(field) do { bench_path_live.field++; } while (0)
static inline void bench_path_walk_steps(uint32_t steps) {
  bench_path_live.ratio_walk_steps_sum += steps;
  if (steps > bench_path_live.ratio_walk_steps_max) {
    bench_path_live.ratio_walk_steps_max = steps;
  }
}
static inline void bench_path_amp_walk_steps(uint32_t steps) {
  bench_path_live.amp_walk_steps_sum += steps;
  if (steps > bench_path_live.amp_walk_steps_max) {
    bench_path_live.amp_walk_steps_max = steps;
  }
}
#endif

// Cross-core report handshake. Each core snapshots and clears its own probes, so no lock is
// needed and neither core ever reads a counter the other is mid-update on.
volatile bool bench_dump_request = false;
volatile bool bench_core_ready[2] = { false, false };
volatile bool bench_periodic = false;

static inline void bench_stat_add(BenchStat *s, uint32_t d) {
  s->sum += d;
  if (s->n == 0u || d < s->min) s->min = d;
  if (d > s->max) s->max = d;
  s->n++;
}

// Close a cycle probe. Reads the counter first so none of the bookkeeping below lands
// inside the measured span.
static inline void bench_add_cyc(uint8_t id, uint32_t start) {
  const uint32_t d = bench_span(start, bench_now());
  bench_stat_add(&bench_stats[id], (d > bench_overhead_cyc) ? (d - bench_overhead_cyc) : 0u);
}

static inline void bench_add_raw(uint8_t id, uint32_t d) {
  bench_stat_add(&bench_stats[id], d);
}

// Interval between successive arrivals. While a report is draining over USB, only refresh
// the previous timestamp — never record — so dump TX cannot inflate period max on either core.
#define BENCH_PERIOD(id)                                                  \
  do {                                                                    \
    static uint32_t bench_prev_##id = 0u;                                 \
    const uint32_t bench_us_##id = bench_us_now();                        \
    if (bench_prev_##id != 0u && !bench_out_active) {                     \
      bench_add_raw(BENCH_##id, bench_us_##id - bench_prev_##id);         \
    }                                                                     \
    bench_prev_##id = bench_us_##id;                                      \
  } while (0)

#ifdef RUNNING_AVERAGE_PERIOD
#define BENCH_BEGIN(id)  ((void)0)
#define BENCH_END(id)    ((void)0)
#define BENCH_FBEGIN(id) ((void)0)
#define BENCH_FEND(id)   ((void)0)
#else
// volatile start: force a real store/load so back-to-back probes cannot CSE timestamps.
// Do NOT wrap in do/while — voice_task declares locals inside a probe and uses them after END.
#define BENCH_BEGIN(id) volatile uint32_t bench_t_##id = bench_now()
#define BENCH_END(id)   bench_add_cyc(BENCH_##id, bench_t_##id)
#ifdef RUNNING_AVERAGE_FINE
#define BENCH_FBEGIN(id) BENCH_BEGIN(id)
#define BENCH_FEND(id)   BENCH_END(id)
#else
#define BENCH_FBEGIN(id) ((void)0)
#define BENCH_FEND(id)   ((void)0)
#endif
#endif

// Minimum collection time before a dump snapshot (after reset or previous dump).
static constexpr uint32_t BENCH_MIN_WINDOW_US = 1000000u; // 1 s

// Snapshot and clear this core's probes, then hand off to the printer on core 0.
inline void bench_service(uint8_t core) {
  if (!bench_dump_request || bench_core_ready[core]) return;

  const uint32_t now_us = bench_us_now();
  const uint32_t elapsed = now_us - bench_window_start_us[core];
  if (elapsed < BENCH_MIN_WINDOW_US) return;  // keep collecting

  bench_window_us[core] = elapsed;
  bench_window_start_us[core] = now_us;

  for (uint8_t i = 0; i < BENCH_COUNT; ++i) {
    if (bench_desc[i].core != core) continue;
    bench_snap[i] = bench_stats[i];
    bench_stats[i] = BenchStat{};
  }
  // Path counters live on core 1 only (voice_task).
  if (core == 1u) {
    bench_path_snap = bench_path_live;
    bench_path_live = BenchPathStat{};
  }
  bench_core_ready[core] = true;
}

inline void bench_reset_all() {
  for (uint8_t i = 0; i < BENCH_COUNT; ++i) {
    bench_stats[i] = BenchStat{};
    bench_snap[i] = BenchStat{};
  }
  bench_path_live = BenchPathStat{};
  bench_path_snap = BenchPathStat{};
  bench_window_start_us[0] = bench_window_start_us[1] = bench_us_now();
}

// Raw probe value -> microseconds, as hundredths, without touching the FPU. Keeping this
// integer matters on RP2040 where float is emulated in software.
inline uint64_t bench_to_us100(uint64_t raw, uint8_t kind) {
  if (kind == BENCH_US) return raw * 100u;
  return ((raw * 100u) + (bench_cycles_per_us / 2u)) / bench_cycles_per_us;
}

inline void bench_fmt_us(char *out, size_t n, uint64_t raw, uint8_t kind) {
  const uint64_t h = bench_to_us100(raw, kind);
  snprintf(out, n, "%lu.%02lu", (unsigned long)(h / 100u), (unsigned long)(h % 100u));
}

inline void bench_fmt_cyc(char *out, size_t n, uint64_t raw) {
  snprintf(out, n, "%luc", (unsigned long)raw);
}

// Fixed columns so nested indents and large totals stay under the header.
// name(28) count(8) mean(9) min(9) max(9) total(14) win(6)
#define BENCH_ROW_FMT "%-28s %8s %9s %9s %9s %14s %6s"

inline void bench_fmt_win(char *out, size_t n, uint32_t permille) {
  snprintf(out, n, "%lu.%lu",
           (unsigned long)(permille / 10u), (unsigned long)(permille % 10u));
}

inline void bench_print_row(uint8_t id, const char *indent) {
  const BenchDesc &d = bench_desc[id];
  const BenchStat &s = bench_snap[id];
  if (s.n == 0u) return;

  char name[32], mean[16], mn[16], mx[16], tot[20], cnt[12], win[8], line[160];
  snprintf(name, sizeof(name), "%s%s", indent, d.label);
  snprintf(cnt, sizeof(cnt), "%lu", (unsigned long)s.n);

  const uint64_t mean_raw = s.sum / s.n;
  // Sub-microsecond cycle probes print as 0.00 us forever at 225 MHz hundredths; show
  // mean/min/max in cycles instead. total and %win stay in us so the budget still adds.
  const bool as_cyc = (d.kind == BENCH_CYC) && (bench_to_us100(mean_raw, BENCH_CYC) < 100u);
  if (as_cyc) {
    bench_fmt_cyc(mean, sizeof(mean), mean_raw);
    bench_fmt_cyc(mn, sizeof(mn), s.min);
    bench_fmt_cyc(mx, sizeof(mx), s.max);
  } else {
    bench_fmt_us(mean, sizeof(mean), mean_raw, d.kind);
    bench_fmt_us(mn, sizeof(mn), s.min, d.kind);
    bench_fmt_us(mx, sizeof(mx), s.max, d.kind);
  }
  bench_fmt_us(tot, sizeof(tot), s.sum, d.kind);

  const uint32_t window = bench_window_us[d.core];
  const uint64_t total_us = bench_to_us100(s.sum, d.kind) / 100u;
  const uint32_t permille = (window > 0u) ? (uint32_t)((total_us * 1000u) / window) : 0u;
  bench_fmt_win(win, sizeof(win), permille);

  snprintf(line, sizeof(line), BENCH_ROW_FMT, name, cnt, mean, mn, mx, tot, win);
  bench_out_println(line);

  // ~70% of the 24-bit range (~52 ms at 225 MHz). Past that the span is almost certainly
  // a wrap, not a real stage duration — say so rather than printing a plausible lie.
  if (d.kind == BENCH_CYC && s.max > ((BENCH_TIMER_MASK * 7u) / 10u)) {
    bench_out_println("                             ^^ max near 24-bit wrap: use BENCH_PERIOD");
  }
}

// Whatever the parent measured that none of its children claimed. Makes gaps in coverage
// visible instead of quietly absorbing them.
inline void bench_print_unattributed(uint8_t parent, const char *indent) {
  const BenchStat &p = bench_snap[parent];
  if (p.n == 0u) return;

  uint64_t children_us100 = 0;
  bool any = false;
  for (uint8_t i = 0; i < BENCH_COUNT; ++i) {
    if (bench_desc[i].parent != parent || bench_snap[i].n == 0u) continue;
    children_us100 += bench_to_us100(bench_snap[i].sum, bench_desc[i].kind);
    any = true;
  }
  if (!any) return;

  const uint64_t parent_us100 = bench_to_us100(p.sum, bench_desc[parent].kind);
  const uint64_t rest = (parent_us100 > children_us100) ? (parent_us100 - children_us100) : 0u;
  const uint32_t window = bench_window_us[bench_desc[parent].core];
  const uint32_t permille = (window > 0u) ? (uint32_t)(((rest / 100u) * 1000u) / window) : 0u;

  char name[32], tot[20], win[8], line[160];
  snprintf(name, sizeof(name), "%s%s", indent, "(unattributed)");
  snprintf(tot, sizeof(tot), "%lu.%02lu",
           (unsigned long)(rest / 100u), (unsigned long)(rest % 100u));
  bench_fmt_win(win, sizeof(win), permille);
  snprintf(line, sizeof(line), BENCH_ROW_FMT, name, "-", "-", "-", "-", tot, win);
  bench_out_println(line);
}

// Recurse so FINE children of note-on (etc.) appear; fixed 3-level walk stopped at voice_task.
// Indent capped so the 28-char name column stays usable (~8 levels).
inline void bench_print_subtree(uint8_t id, const char *indent) {
  bench_print_row(id, indent);

  char child_indent[16];
  const size_t len = strlen(indent);
  if (len + 2u >= sizeof(child_indent)) {
    return; // do not nest further
  }
  memcpy(child_indent, indent, len);
  child_indent[len] = ' ';
  child_indent[len + 1u] = ' ';
  child_indent[len + 2u] = '\0';

  bool any_child = false;
  for (uint8_t c = 0; c < BENCH_COUNT; ++c) {
    if (bench_desc[c].parent != id) continue;
    any_child = true;
    bench_print_subtree(c, child_indent);
  }
  if (any_child) {
    bench_print_unattributed(id, child_indent);
  }
}

inline void bench_print_core(uint8_t core) {
  char line[160];
  snprintf(line, sizeof(line), "-- Core %u  (window %lu.%03lu ms) --", (unsigned)core,
           (unsigned long)(bench_window_us[core] / 1000u),
           (unsigned long)(bench_window_us[core] % 1000u));
  bench_out_println(line);
  snprintf(line, sizeof(line), BENCH_ROW_FMT,
           "probe", "count", "mean", "min", "max", "total", "%win");
  bench_out_println(line);

  for (uint8_t i = 0; i < BENCH_COUNT; ++i) {
    if (bench_desc[i].core != core || bench_desc[i].parent != BENCH_NONE) continue;
    bench_print_subtree(i, "");
  }
}

// Ratio / amp-comp / portamento path breakdown for the same window as core 1 probes.
inline void bench_print_path_counters() {
  const BenchPathStat &p = bench_path_snap;
  const uint32_t ratio_seg =
      p.ratio_hit + p.ratio_miss_direct + p.ratio_miss_bsearch;
  const uint32_t amp_seg = p.amp_hit + p.amp_miss_walk + p.amp_miss_scan;
  const uint32_t porta_n = p.porta_off + p.porta_note_on + p.porta_retime +
                           p.porta_steady_time + p.porta_steady_slew;
  if (ratio_seg == 0u && p.ratio_clamp == 0u && amp_seg == 0u && p.amp_clamp == 0u &&
      porta_n == 0u) {
    return;
  }

  bench_out_puts("\n");
  bench_out_println("-- Path counters (core 1, same window) --");
  bench_out_printf(
      "ratio: hit=%lu miss_direct=%lu miss_bsearch=%lu clamp=%lu  "
      "walk_steps max=%lu sum=%lu\n",
      (unsigned long)p.ratio_hit, (unsigned long)p.ratio_miss_direct,
      (unsigned long)p.ratio_miss_bsearch, (unsigned long)p.ratio_clamp,
      (unsigned long)p.ratio_walk_steps_max, (unsigned long)p.ratio_walk_steps_sum);
  if (ratio_seg > 0u) {
    const uint32_t miss = p.ratio_miss_direct + p.ratio_miss_bsearch;
    const uint32_t miss_pm = (miss * 1000u) / ratio_seg;
    bench_out_printf("ratio miss rate: %lu.%lu%% of in-table calls\n",
                     (unsigned long)(miss_pm / 10u), (unsigned long)(miss_pm % 10u));
  }
  bench_out_printf(
      "amp: hit=%lu miss_walk=%lu miss_scan=%lu clamp=%lu  "
      "find_steps max=%lu sum=%lu\n",
      (unsigned long)p.amp_hit, (unsigned long)p.amp_miss_walk,
      (unsigned long)p.amp_miss_scan, (unsigned long)p.amp_clamp,
      (unsigned long)p.amp_walk_steps_max, (unsigned long)p.amp_walk_steps_sum);
  if (amp_seg > 0u) {
    const uint32_t miss = p.amp_miss_walk + p.amp_miss_scan;
    const uint32_t miss_pm = (miss * 1000u) / amp_seg;
    bench_out_printf("amp miss rate: %lu.%lu%% of in-band calls\n",
                     (unsigned long)(miss_pm / 10u), (unsigned long)(miss_pm % 10u));
  }
  bench_out_printf(
      "porta: off=%lu note_on=%lu retime=%lu steady_time=%lu steady_slew=%lu\n",
      (unsigned long)p.porta_off, (unsigned long)p.porta_note_on,
      (unsigned long)p.porta_retime, (unsigned long)p.porta_steady_time,
      (unsigned long)p.porta_steady_slew);
}

// Format the report into bench_out_buf (no Serial I/O).
inline void bench_print_report() {
#ifdef RUNNING_AVERAGE_PERIOD
  const char *probe_mode = "period only";
#elif defined(RUNNING_AVERAGE_FINE)
  const char *probe_mode = "fine probes on";
#else
  const char *probe_mode = "fine probes off";
#endif

  char line[192];
  bench_out_puts("\n");
  bench_out_println("=================== DCO BENCH ===================");
  snprintf(line, sizeof(line), "clk_sys %lu MHz   probe overhead %lu cyc   %s",
           (unsigned long)bench_cycles_per_us, (unsigned long)bench_overhead_cyc, probe_mode);
  bench_out_println(line);

  // Compile-time engine flags + live runtime selectors (amp method / note retrig).
#if defined(PICO_RP2350)
  const char *mcu = "RP2350";
#elif defined(PICO_RP2040)
  const char *mcu = "RP2040";
#else
  const char *mcu = "?";
#endif
#ifdef USE_FLOAT_VOICE_TASK
  const char *voice = "FLOAT";
#else
  const char *voice = "FIXED";
#endif
  // Compile-time HIGH_PRECISION_CLKDIV (float voice ignores it at runtime).
#if HIGH_PRECISION_CLKDIV
  const char *clkdiv = "HP1";
#else
  const char *clkdiv = "HP0";
#endif
#ifdef USE_FLOAT_AMP_COMP
  const char *amp = "FLOAT";
#else
  const char *amp = "FIXED";
#endif
#ifdef USE_FLOAT_CV_OUTS
  const char *cv = "FLOAT";
#else
  const char *cv = "FIXED";
#endif
  snprintf(line, sizeof(line),
           "build: mcu=%s voice=%s pitch=%s amp=%s cv=%s amp_method=%s clkdiv=%s note_retrig=%s",
           mcu, voice, bench_pitch_interp_mode_name(), amp, cv,
           amp_comp_method_name(amp_comp_method), clkdiv,
           note_retrig_mode_name(note_retrig_mode));
  bench_out_println(line);
  bench_out_println("Times in us (mean/min/max as cycles when mean < 1 us). %win = share of wall clock.");
  bench_out_puts("\n");
  bench_print_core(0);
  bench_out_puts("\n");
  bench_print_core(1);
#ifndef RUNNING_AVERAGE_PERIOD
  bench_print_path_counters();
#endif
  bench_out_println("=================================================");
  bench_out_puts("\n");
}

// Drain handled by shared bench_out_drain_chunk() above.

// Forward declaration: period-probe report queued from core 1.
extern volatile bool pio_probe_report_pending;

// Core 0 side of the handshake: format into RAM when both cores are ready, then pace
// Serial TX across loop iterations. Printing never happens on core 1.
inline void bench_poll_core0() {
  if (bench_cdc_rx_pending()) {
    return;
  }

  if (pio_probe_report_pending && !bench_out_active) {
    pio_probe_report_flush();
  }

  if (bench_out_drain_chunk()) {
    return;
  }

  if (bench_periodic && !bench_dump_request) {
    static uint32_t last_ms = 0;
    const uint32_t now_ms = millis();
    if (now_ms - last_ms >= 1000u) {
      last_ms = now_ms;
      bench_dump_request = true;
    }
  }

  bench_service(0);

  if (bench_dump_request && bench_core_ready[0] && bench_core_ready[1] &&
      !bench_cdc_rx_pending()) {
    bench_out_reset();
    bench_print_report();
    print_clkdiv_bench();
    // Engine flags already on the build: line in bench_print_report().
    bench_core_ready[0] = false;
    bench_core_ready[1] = false;
    bench_dump_request = false;
    bench_out_active = (bench_out_len > 0u);
    bench_out_drain_chunk();
  }

  // Amp-comp method ack (PARAM_DEBUG_COMMAND 20–22). Wait until paced TX idle.
  if (!bench_out_active && amp_comp_method_ack_pending) {
    amp_comp_method_ack_pending = false;
    bench_out_reset();
    bench_out_printf("amp_comp method=%s (profiler reset)\n",
                     amp_comp_method_name(amp_comp_method));
    bench_out_active = (bench_out_len > 0u);
    bench_out_drain_chunk();
  }

  if (!bench_out_active && note_retrig_mode_ack_pending) {
    note_retrig_mode_ack_pending = false;
    bench_out_reset();
    bench_out_printf("note_retrig=%s\n", note_retrig_mode_name(note_retrig_mode));
    bench_out_active = (bench_out_len > 0u);
    bench_out_drain_chunk();
  }

  // One-shot amp-comp speed/accuracy (PARAM_DEBUG_COMMAND 24/25). Wait until paced TX idle.
  if (!bench_out_active &&
      (amp_comp_bench_speed_pending || amp_comp_bench_accuracy_pending)) {
    bench_out_reset();
    print_amp_comp_bench();
    bench_out_active = (bench_out_len > 0u);
    bench_out_drain_chunk();
  }

  // One-shot pitch-interp speed/accuracy (PARAM_DEBUG_COMMAND 28/29).
  if (!bench_out_active &&
      (pitch_interp_bench_speed_pending || pitch_interp_bench_accuracy_pending)) {
    bench_out_reset();
    print_pitch_interp_bench();
    bench_out_active = (bench_out_len > 0u);
    bench_out_drain_chunk();
  }
}

#else  // !RUNNING_AVERAGE

#define BENCH_BEGIN(id)  ((void)0)
#define BENCH_END(id)    ((void)0)
#define BENCH_PERIOD(id) ((void)0)
#define BENCH_FBEGIN(id) ((void)0)
#define BENCH_FEND(id)   ((void)0)
#define BENCH_PATH_INC(field) ((void)0)
static inline void bench_path_walk_steps(uint32_t) {}
static inline void bench_path_amp_walk_steps(uint32_t) {}

inline void bench_service(uint8_t) {}
inline void bench_reset_all() {}

inline void bench_poll_core0() {
  if (bench_cdc_rx_pending()) {
    return;
  }
  if (pio_probe_report_pending && !bench_out_active) {
    pio_probe_report_flush();
  }
  bench_out_drain_chunk();
}

#endif  // RUNNING_AVERAGE

#endif  // __BENCH_H__
