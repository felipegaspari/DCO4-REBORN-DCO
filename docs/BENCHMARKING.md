# Hot-path benchmarking

How to measure where the DCO's realtime budget goes, and how to read the numbers without
fooling yourself.

- Flags: [`ENGINE_OPTIONS.md`](ENGINE_OPTIONS.md)
- Code: [`../bench.h`](../bench.h), probes in [`../DCO.ino`](../DCO.ino) and [`../voices.ino`](../voices.ino)

---

## 1. Turning it on

Uncomment in [`../DCO.ino`](../DCO.ino):

```c
#define RUNNING_AVERAGE         // profiler on (main stage probes)
#define RUNNING_AVERAGE_FINE    // plus the tiny per-stage probes
#define RUNNING_AVERAGE_PERIOD  // only loop / loop1 periods (no stage probes)
```

With `RUNNING_AVERAGE` off, every `BENCH_*` macro expands to nothing. There is no runtime
cost and no storage in the shipping build.

**`RUNNING_AVERAGE_PERIOD`** (needs `RUNNING_AVERAGE`): stage `BENCH_BEGIN`/`END` compile
out; only `BENCH_PERIOD` for `loop period` / `loop1 period` collects. Use this for a true
loop-time baseline without intermediate probe tax. The dump banner says `period only`.
Do not combine with FINE expecting stage rows — PERIOD wins and FINE is ignored.

**Period-only vs full MAIN.** Same preset, same play: compare `loop` / `loop1` mean **and
max**. The gap is mostly instrumentation tax (each child's `BENCH_END` updates stats after
its end timestamp, so that cost sits in the parent `(unattributed)`). Example shape: period
loop1 ~tens of µs mean vs full MAIN ~3× slower when many MAIN children are under
`voice_task`. Use period-only for “is the synth faster?”; use full MAIN to rank **where**
time goes — ignore inflated absolute loop means and large parent `(unattributed)` when many
MAIN kids are present (not only when FINE is on).

Reports go out the USB CDC port. They are **off by default even with profiling compiled in**
— ask for them with a debug command.

## 2. Getting a report

On the **Diagnostics** tab of [`../tools/dco_control`](../tools/dco_control/README.md), the
**Hot-path profiler** buttons send `PARAM_DEBUG_COMMAND` (id 160):

| Button | Value | Effect |
|--------|-------|--------|
| Dump profiler once | `10` | Dump once |
| Reset profiler | `11` | Reset all accumulators |
| Toggle ~1 Hz dump | `12` | Toggle the ~1 Hz automatic dump |

A dump is asynchronous. Setting the request flag makes each core snapshot and clear its own
probes at its next loop iteration; core 0 then **formats** the report into a RAM buffer and
drains it over USB in small chunks across later `loop()` turns. **Core 1 never prints.**
The tables land in the tool's Board output pane.

While that paced TX is active, both cores' `BENCH_PERIOD` probes (loop period / loop1 period)
skip samples so dump traffic cannot inflate period `max`. Cycle-probe maxes (voice_task,
amp-comp, …) keep collecting — those are real jitter, not print artifacts.

One-shot and periodic dumps wait until the collection window is **≥ 1 s** since the last
reset or dump (`BENCH_MIN_WINDOW_US`). Requesting a dump earlier just keeps sampling until
that elapses — the report then shows a ~1000 ms window. Amp-comp method acks still print
immediately (they do not wait on this gate).

## 3. Reading the output

The banner includes engine flags (compile-time + live runtime selectors) so rebuilds / method switches are obvious:

```
=================== DCO BENCH ===================
clk_sys 225 MHz   probe overhead N cyc   fine probes off
build: mcu=RP2350 voice=FLOAT pitch=FLOAT amp=FLOAT amp_method=FLOAT_QUAD clkdiv=HP1 note_retrig=EXACT_Y
```

Fields: `mcu` (board package), `voice` / `amp` (`USE_FLOAT_VOICE_TASK` / `USE_FLOAT_AMP_COMP`), `pitch` (`PITCH_INTERP_MODE`), `amp_method` (live `amp_comp_method`), `clkdiv` (`HIGH_PRECISION_CLKDIV` → `HP0`/`HP1`; ignored at runtime when `voice=FLOAT`), `note_retrig` (live mode).

```
-- Core 1  (window 1002.113 ms) --
probe                       count      mean       min       max      total   %win
loop1 period                48211     20.78     18.00    412.00  1001800.0   99.9
  ADSR_update                9980      1.42      1.31      8.90    14171.6    1.4
  update_CV_outs             9980      3.07      2.88     11.40    30638.6    3.0
  voice_task TOTAL          48211      9.84      9.11     74.20   474396.2   47.3
    clkdiv math             48211      0.58      0.51      2.13    27962.3    2.7
    PIO put/exec            48211      1.94      1.88      6.40    93529.3    9.3
    note-on retrigger          12     18.40     17.90     31.20      220.8    0.02
    (unattributed)              -         -         -         -    31004.1    3.0
  (unattributed)                -         -         -         -   482593.6   48.1
```

**`%win` is the column that matters.** Probes fire at wildly different rates — `ADSR_update`
runs on a 100 µs gate, `note-on retrigger` only on note-on, and the per-oscillator stages run
once per voice. A mean in isolation says nothing about a stage's share of the CPU, and means
from different probes cannot be added together. `total` (count x mean) and `%win` can.
The dump prints the full parent/child tree (including MAIN children under `note-on retrigger`);
rows with `count == 0` are omitted.

**`note-on retrigger`** is not a flag flip: it stops OSC1/OSC2, optionally reloads exact period
(Y / clk_div), restarts in sync, and writes RANGE PWM — typically tens of µs mean with
**~0 %win** because it fires only on note-on. Mode A/B: debug **26** `EXACT_Y` (default) vs
**27** `SYNC_JMP` (jmp only on running SMs; no disable / Y load / `enable_in_sync`). Dump/ack
lines include `note_retrig=…`.

Probes (MAIN; rare hits keep steady-state tax small):

| Probe | Parent | When |
|-------|--------|------|
| `retrig period split` | `voice_task` | EXACT_Y note-on frames, next to `phase align` |
| `retrig SM apply` | `note-on retrigger` | EXACT_Y: disable + fused noclear load + jmp + enable; SYNC_JMP: jmp only |
| `retrig RANGE PWM` | `note-on retrigger` | every note-on flag (even if `oscSync == 0`) |

Do **not** split SM apply into disable/load/jmp/enable — cold XIP on the rare note-on block
slides between those children and mis-ranks them (e.g. fake slow `jmp` after a faster load).
Dense note-ons (count ≫ 4) before trusting means. Attack-frame cost also includes **ADSR**
`noteStart` and **portamento** note-on reset outside this parent.

**Core 0** siblings under `loop period` include `MIDI read`, `serial panel/USB`, and `LFO1`
(plus gated `LFO2` / `drift LFOs` / `FIFO push`). Large Core0 **max** almost always lands in
`MIDI read` or `serial panel/USB` — compare those maxes to see which owns the spike (USB CDC
RX bursts vs MIDI library).

**`(unattributed)`** is the parent's total minus everything its children claimed. It is not
vanished time:

- **MAIN children already put `BENCH_END` tax in the parent gap** — not only FINE. With many
  MAIN kids under `voice_task`, a large `(unattributed)` is mostly probe bookkeeping. Confirm
  with `RUNNING_AVERAGE_PERIOD` (true loop1) vs full MAIN before chasing that hole as DSP.
- Remaining gap with FINE off after MAIN kids: glue + FINE-only work (`PW arithmetic`).
- With **FINE on**, tax grows further; A/B `voice_task TOTAL` with FINE off before chasing.

**Gate counts vs loop count.** If `ADSR_update` / PW rows have the same count as
`voice_task TOTAL`, the core 1 loop is slower than their 99/100 µs gates, so the gates fire
every iteration. When the loop is faster (typical with FINE off), those counts drop to about
half — that is correct, not double-counting.

**Sub-µs stages.** Cycle probes whose mean is under 1 µs print mean/min/max as cycles (suffix
`c`); `total` and `%win` stay in microseconds so the budget still adds up. A row of
`0.00` µs was a display floor, not dead code.

**`min` / `max`** are the jitter. For a realtime loop the max is usually more actionable than
the mean; a stage with a 0.6 µs mean and a 40 µs max is a worse problem than a steady 5 µs one.

Reset happens on every report (and on amp-comp method switch), so each dump describes at
least the last 1 s of collection since the previous reset/dump.

## 4. How it measures

**Time source: SysTick.** A free-running 24-bit down-counter clocked from `clk_sys`, read as
a single load from `systick_hw->cvr`. Resolution is one CPU cycle — 4.4 ns at 225 MHz.

RP2040's Cortex-M0+ has **no DWT cycle counter**, so SysTick is the only single-cycle source
available on both RP2040 and RP2350. SysTick is a per-core peripheral, which is why
`bench_init_core()` is called from both `setup()` and `setup1()`.

**Range limit.** 24 bits wraps after 2^24 cycles: about **74.5 ms** at 225 MHz, **126 ms** at
133 MHz. Any probe that can outlast that must use `BENCH_PERIOD()`, which reads the 1 µs
timer (`timer_hw->timerawl`) instead — that is what the two loop-period probes do. If a
cycle probe's `max` climbs past ~70% of the counter range (~52 ms at 225 MHz), the report
prints a wrap warning under the row rather than showing a plausible-looking wrong number.

**Overhead.** At boot each core times 32 back-to-back counter reads and keeps the minimum,
which is the cleanest estimate of the probe's own cost (anything larger caught an interrupt).
That constant is subtracted from every cycle sample and printed in the report header, so the
measurement floor is visible.

**Storage.** 20 bytes per probe: `{count, min, max, sum}`, integer only. That matters on the
RP2040, where the M0+ has no FPU and every float operation is an `__aeabi_*` library call —
a float accumulator would cost more than most of the stages being measured.

## 5. What it cannot tell you

**Probes are optimisation barriers.** The compiler cannot keep values in registers, reorder,
or merge work across a counter read. An instrumented build does not generate the same code as
the shipping build, and the more probes, the wider the gap. This is what
`RUNNING_AVERAGE_FINE` is for: compare `voice_task TOTAL` with fine probes on and off to see
how much the instrumentation is costing you (tens of microseconds per call is normal with
FINE on). Trust the coarse `%win` numbers more than the fine stage means.

**Interrupts land inside probes.** USB and MIDI activity shows up as occasional large `max`
values that have nothing to do with the measured code. Judge stages by `mean` and `%win`,
and treat an isolated large `max` as a question rather than a verdict.

**The loop-period probes are still 1 µs.** They read the microsecond timer, so a `loop1
period` of 20 µs carries about 5% quantisation on any single sample. The mean over thousands
of samples is fine; the min and max are coarse.

## 6. Adding a probe

Add one line to `BENCH_PROBES` in [`../bench.h`](../bench.h):

```c
X(vt_my_stage, 1, BENCH_CYC, BENCH_T_MAIN, BENCH_voice_task, "my stage")
//  id       core   unit        tier          parent          label
```

then bracket the code:

```c
BENCH_BEGIN(vt_my_stage);
...
BENCH_END(vt_my_stage);
```

Storage, the enum id, core ownership, nesting and the printed label all come from that one
line. Nothing else needs editing — which is the point. The previous profiler kept its probe
list in three places (`voices.h` externs, `voices.ino` definitions, and the print function),
they drifted apart during the 3-oscillator refactor, and the mismatch compiled silently
because an `extern` that is never used needs no definition.

Use `BENCH_FBEGIN` / `BENCH_FEND` instead for a fine-tier stage, and `BENCH_PERIOD(id)` for
an interval between arrivals rather than the duration of a block.

## 7. `CLKDIV_BENCHMARK`

Separate flag, separate question: is `float` good enough for the clock-divider math, or is
`double` worth its cost?

```c
#define CLKDIV_BENCHMARK   // requires RUNNING_AVERAGE
```

Every frame `clkdiv_bench_sample()` in [`../voices.ino`](../voices.ino) runs both candidates
on OSC1 and OSC2, and accumulates:

- Time spent in each, in microseconds.
- Difference in the divider that actually reaches the state machine — after
  `pio_clk_div_for_y()`, not the intermediate cycle count, since its rounding can absorb a
  difference or create one.
- Difference in the frequency each divider produces through the PIO period model
  (`y + weight * clk_div + overhead`), in Hz, which is the number you can hear.

This roughly triples the cost of the divider stage. Bench only.

Only the float engine is instrumented — the comparison is float versus double, and the fixed
engine's 64-bit Q24 path (`HIGH_PRECISION_CLKDIV`) is a separate question that would need its
own candidate added to the same harness.

## 8. Amp-comp methods (`AMP_COMP_BENCHMARK`)

Compare live amp-comp algorithms and check LUT accuracy against the float quadratic.

```c
#define AMP_COMP_BENCHMARK   // requires RUNNING_AVERAGE; useful with USE_FLOAT_AMP_COMP
```

| `PARAM_DEBUG_COMMAND` | Effect |
|----------------------:|--------|
| 20 | Live method → `FLOAT_QUAD` |
| 21 | Live method → `LUT` (speed A/B) |
| 22 | Live method → `FIXED` (RP2040 compile default) |
| 24 | Speed bench all methods → `bench_out_*` paced TX |
| 25 | Accuracy vs `FLOAT_QUAD` → same output path |

Method select (20–22) works without `AMP_COMP_BENCHMARK`. With `RUNNING_AVERAGE`, each press **resets the profiler**, then acks in the Board pane as `amp_comp method=… (profiler reset)`. Play ~1 s with pitch motion, then dump (**10**). On **RP2350 (FPU)** expect **amp comp** mean / `%win` roughly **LUT ≪ FLOAT_QUAD ≲ FIXED** (FIXED can lose to FLOAT in the float-Hz speed bench because each call does `lrintf` → Q8 then integer window math). On **RP2040 soft-float** the old intuition **FLOAT_QUAD ≫ FIXED ≫ LUT** still holds. Confirm with speed bench; absolute `meanNs` moves with Core 1's `live_method=` (contention). Reports 24–25 no-op unless both `AMP_COMP_BENCHMARK` and `RUNNING_AVERAGE` are on (same rule as profiler 10–12 for paced output).

**Synthetic calibration (cmds 24 / 25 only)**

Speed and accuracy do **not** use LittleFS `voiceTables` (which may be empty). They:

1. Snapshot the live breakpoints.
2. Install a **linear fake** table: 22 points, Hz from 1…`AMP_COMP_MAX_HZ`, levels from 1…`DIV_COUNTER` (same on all oscs), then `precompute_amp_comp_for_engine()`.
3. Run the measurement (report lines include `cal=SYNTHETIC`).
4. Restore the snapshot and precompute again so play-mode cal is unchanged.

Do not treat 24/25 as a check of your board’s stored calibration.

**Live A/B with the hot-path profiler**

1. Enable `RUNNING_AVERAGE` (optional `RUNNING_AVERAGE_FINE`). Same preset both sides.
2. Send 21 (LUT) or 20 (float quad), etc. — Board acks and clears old probe averages.
3. Play with pitch modulation ~1 s; dump profiler (**10**).
4. Compare `amp comp` mean / max / `%win`, `voice_task`, and instrumented `loop1 period`.
5. Rebuild with `RUNNING_AVERAGE_PERIOD` and dump again — trust **period-only** `loop1`
   mean **and max** for whether the synth got faster (ignore instrumented loop means).
6. Dump `build:` line includes `amp_method=…`. On RP2350 expect LUT cheapest; FLOAT_QUAD
   competitive with (often faster than) FIXED.

**Dense note-on retrigger A/B** (after flashing note-on MAIN children)

1. `RUNNING_AVERAGE` on, FINE off. Reset (**11**), then play many note-ons for ≥1 s (want
   `note-on retrigger` count ≫ 4). Dump (**10**).
2. Under `note-on retrigger`, read `retrig SM apply` and `retrig RANGE PWM`; split is a
   `voice_task` sibling.
3. Switch mode **26** / **27**, reset, same dense play, dump again. EXACT_Y: SM apply =
   disable+load+jmp+enable; SYNC_JMP: SM apply = jmp only + `RANGE PWM`.

**Speed report** (`=== AMP COMP BENCH ===`): fixed-width comparison table — method, calls, totalUs, meanNs, pctVsFloat (vs `FLOAT_QUAD`). Workload is the **same 0.01 Hz grid as accuracy** (`1.00…AMP_COMP_MAX_HZ`, **one osc** / `AMP_COMP_BENCH_OSCS` — synthetic cal is identical per osc; header `grid=… oscs=`). No vibrato / coarse step. ~700k calls per method; wait for paced Board pane output. Absolute `meanNs` depends on `live_method=` (Core 1 keeps running live amp-comp during the Core 0 one-shot); prefer `pctVsFloat` for ranking.

**Accuracy report** (`=== AMP COMP ACCURACY ===`): per-method plain-English summary of error vs `FLOAT_QUAD` for `LUT` / `FIXED`. Errors are in **RANGE PWM counts** (full scale = `DIV_COUNTER`), also shown as **% of full**. Grid is every **0.01 Hz** from **1.00 to `AMP_COMP_MAX_HZ`** on **one osc** (same as speed). Each method prints: typical (mean) PWM, worst in-band (freq below max Hz), tip outlier if exact max Hz disagrees by more than 1 PWM, rate of samples **worse than 1 PWM** (`|e| > 1`), and rate of samples **exactly 1 PWM** (`|e| == 1`). Plus `LUT integer-Hz sanity` over `0…AMP_COMP_MAX_HZ` (want 0). LUT indexes by **nearest** integer Hz (not trunc). One-shot is heavy — wait for paced Board pane output.

Implementation: [`../amp_comp_bench.ino`](../amp_comp_bench.ino), wired from `apply_param_debug_command` and `bench_poll_core0()`. dco_control Diagnostics exposes the buttons via `AMP_COMP_COMMANDS` in [`../tools/dco_control/params.py`](../tools/dco_control/params.py).

## 9. Pitch interpolators (cmds 28 / 29)

Compare FLOAT / RATIO_Q16 / Q12 for speed and a dual-reference accuracy report. Tables and interpolators live **only** in [`../pitch_interp_bench.ino`](../pitch_interp_bench.ino) (private BSS, lazy-filled); the live voice path is untouched. No extra compile flag — needs `RUNNING_AVERAGE` for paced `bench_out_*` TX (same as the profiler dump).

| `PARAM_DEBUG_COMMAND` | Effect |
|----------------------:|--------|
| 28 | Speed bench all methods → `bench_out_*` paced TX |
| 29 | Dual-ref accuracy (vs FLOAT + vs private Q20 slope ref) → same output path |

Live hot path still uses compile-time `PITCH_INTERP_MODE` only (`FLOAT` / `RATIO_Q16` / `Q12`). Use 28/29 to rank candidates without rebuilding for each mode.

**Speed report** (`=== PITCH INTERP BENCH ===`): three rows — FLOAT, RATIO_Q16, Q12. Method, calls, totalUs, meanNs, `pctVsFloat` (FLOAT = 100%). **Flag-path** under the compiled voice engine (header `speed=flag-path voice=FIXED|FLOAT`):

- **Fixed voice** (`!USE_FLOAT_VOICE_TASK`): cost to **`ratioQ16`**. `RATIO_Q16` = fused `slopeQ20` path; `Q12` = `interp_y` + live `y→ratio` reciprocal. `FLOAT` = soft-float reference only. Grid: `xInt -10000…30000`. Shared Q24→xQ16 and final `freq×ratio` omitted.
- **Float voice** (`USE_FLOAT_VOICE_TASK`): cost to **float ratio**. `FLOAT` = natural mod → float interp; int modes = `mod×10000` → `lroundf` → int interp → float rescale.

**Accuracy report** (`=== PITCH INTERP ACCURACY ===`): same modifier grid (**one osc**). Private **Q20 `y` lerp** is the int reference only (not a live mode). Errors in **cents**. Sections:

1. **Table quantization floor** — knot `|Q20_ref − FLOAT|` mean/max cents.
2. **vs FLOAT** — RATIO / Q12: mean/max, p50/p95/p99, `>0.5¢` / `>1.0¢` (table gap; methods look similar).
3. **vs Q20 ref on int tables** (slope A/B) — RATIO / Q12 vs private Q20→ratio. Mean/max, percentiles, `>0.01¢` / `>0.1¢`, knot vs mid.
4. **vs Q20 ref by mod-band** — low / mid / high.

Diagnostics buttons: `PITCH_INTERP_COMMANDS` in [`../tools/dco_control/params.py`](../tools/dco_control/params.py).
