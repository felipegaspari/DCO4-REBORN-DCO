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
out; only `BENCH_PERIOD` for `loop period` / `loop1 period` collects. Path counters
(`amp:` / `ratio:` / `porta:`) are also disabled — no bumps and no dump block. Use this for
a true loop-time baseline without intermediate probe tax. The dump banner says `period only`.
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
build: mcu=RP2350 voice=FLOAT pitch=FLOAT amp=FLOAT cv=FLOAT amp_method=FLOAT_QUAD clkdiv=HP1 note_retrig=EXACT_Y
```

Fields: `mcu` (board package), `voice` / `amp` / `cv` (`USE_FLOAT_VOICE_TASK` / `USE_FLOAT_AMP_COMP` / `USE_FLOAT_CV_OUTS`), `pitch` (`PITCH_INTERP_MODE`), `amp_method` (live `amp_comp_method`), `clkdiv` (`HIGH_PRECISION_CLKDIV` → `HP0`/`HP1`; ignored at runtime when `voice=FLOAT`), `note_retrig` (live mode).

**`update_CV_outs`:** after CV absorption + mod matrix, this probe can dominate Core1 on RP2040 (soft-float) and still matter on RP2350. Compare `update_CV_outs` `%win` and period-only `loop1` mean/max before/after CV/matrix changes; older doc samples (~3 µs / ~3%win) are **stale**. A/B with `#undef USE_FLOAT_CV_OUTS` on RP2350 (`cv=FIXED`).

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

*(Example shape only — re-measure on your build.)*

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

**Path counters** (printed after Core 1, same window). Integer bumps only — not SysTick
probes — so they do not add measurement barriers. Use them to attribute stage `max` spikes:

| Counter | Meaning |
|---|---|
| `ratio hit` | Segment cache already valid |
| `ratio miss_direct` | Miss: trunc `((x+1)*N/4)` + clamp + optional ±1 (live FLOAT_FAST; no hot bsearch) |
| `ratio miss_bsearch` | Unused on slim FAST (should stay 0); kept for struct/dump |
| `ratio clamp` | Modifier at/outside table ends (early out) |
| `walk_steps max/sum` | ±1 fixup count on misses (should stay 0–1) |
| `porta off / note_on / retime / steady_time / steady_slew` | Exclusive portamento path per voice frame |

If `ratio interpolate` max is high **and** miss rate / `walk_steps max` climb under pitch mod,
the spike is algorithmic. If miss rate is ~0 but max stays large, suspect IRQ inside the
probe. Same idea for porta: correlate max with `note_on` / `retime` / `steady_slew` counts.

**Live gate for pitch-path edits (same patch/music test):** accept only if `ratio miss rate`
stays ~70% (not ~90%), `ratio interpolate` mean/max do not worsen, and `loop1` /
`voice_task` means hold. Cmd **28** (esp. jump) ranks find logic but does **not** clear
live — trunc-only and fmaf/x0-reshape won or looked fine in places and lost live. Keep
FLOAT_FAST ±1 even when `walk_steps sum=0` (live ballast). Live `_fast` is
`__attribute__((noinline))` so inlining into `voice_task_float` does not reshape the
whole task. Cmd **29** still gates accuracy (~0¢ vs FLOAT walk).

Segment-find changes (direct index vs walk) do **not** change pitch accuracy — same knot
and lerp. Confirm with pitch accuracy cmd `29` if desired; speed with profiler + path counters.

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
| 20 | Live method → `FLOAT_QUAD` (cached walk) |
| 21 | Live method → `LUT` (speed A/B) |
| 22 | Live method → `FIXED` (RP2040 compile default) |
| 24 | Speed bench all three methods → `bench_out_*` paced TX |
| 25 | Accuracy vs `FLOAT_QUAD` → same output path |

Method select (20–22) works without `AMP_COMP_BENCHMARK`. With `RUNNING_AVERAGE`, each press **resets the profiler**, then acks in the Board pane as `amp_comp method=… (profiler reset)`. Play ~1 s with pitch motion, then dump (**10**). On **RP2350 (FPU)** expect **amp comp** mean / `%win` roughly **LUT ≪ FLOAT_QUAD ≲ FIXED** (FIXED can lose to FLOAT in the float-Hz speed bench because each call does `lrintf` → Q8 then integer window math). On **RP2040 soft-float** the old intuition **FLOAT_QUAD ≫ FIXED ≫ LUT** still holds. Confirm with speed bench; absolute `meanNs` moves with Core 1's `live_method=` (contention). Reports 24–25 no-op unless both `AMP_COMP_BENCHMARK` and `RUNNING_AVERAGE` are on (same rule as profiler 10–12 for paced output).

Live **FLOAT_QUAD** window find uses a per-osc cache then walk (full scan only as rare fallback). Profiler dump **10** prints an `amp:` path line (`hit` / `miss_walk` / `miss_scan` / `clamp`, `find_steps`) — only bumped by live **FLOAT_QUAD** (cmd **20**). `find_steps` is walk length. LUT / FIXED leave those counters at zero. LUT fill / accuracy gold call the same `get_chan_level_float_quad` (precompute resets `ampWinCache` afterward).

**Bench vs live (why cmd 24 ≫ “feel”)**

Cmd **24** is a microscope: ~700k amp-only calls, so `pctVsFloat` gaps look large (LUT often ≪ 100; FIXED often ≫ 100 on float voice). Live `amp comp` is often only ~10–20 `%win` of the voice window — even a large amp speedup moves total loop1 modestly. Large instrumented `(unattributed)` under `voice_task` is probe bookkeeping; do not judge “synth got faster” from that. Subjective sameness is normal: pitch / PIO still dominate.

Under float voice, **FIXED** is often slower than FLOAT_QUAD in the speed bench (each FIXED call does `lrintf` → Q8 then integer window math). Rank live methods by `amp comp` **mean / `%win`**, not by ear.

**Synthetic calibration (cmds 24 / 25 only)**

Speed and accuracy do **not** use LittleFS `voiceTables` (which may be empty). They:

1. Snapshot the live breakpoints.
2. Install a **linear fake** table: 22 points, Hz from 1…`AMP_COMP_MAX_HZ`, levels from 1…`DIV_COUNTER` (same on all oscs), then `precompute_amp_comp_for_engine()`.
3. Run the measurement (report lines include `cal=SYNTHETIC`).
4. Restore the snapshot and precompute again so play-mode cal is unchanged.

Do not treat 24/25 as a check of your board’s stored calibration.

**Live A/B with the hot-path profiler**

1. Enable `RUNNING_AVERAGE` (optional `RUNNING_AVERAGE_FINE`). Same preset both sides.
2. Send method (20–22) — Board acks `amp_comp method=… (profiler reset)`.
3. Play with pitch modulation ~1 s; dump profiler (**10**).
4. Check `build: … amp_method=…`, the `amp:` path line, and `amp comp` **mean / `%win`** (not loop1 feel).
5. Rebuild with `RUNNING_AVERAGE_PERIOD` and dump again — trust **period-only** `loop1`
   mean **and max** for whether the synth got faster (ignore instrumented loop means).

Expected signatures after a fresh dump (same play gesture each time):

| Method | `amp:` path counters | `amp comp` vs FLOAT_QUAD |
|--------|----------------------|--------------------------|
| **20 FLOAT_QUAD** | mostly `hit`, some `miss_walk`, rare `miss_scan`; `find_steps` = walk length | baseline `%win` |
| **21 LUT** | **all zeros** | clearly lower mean / `%win` |
| **22 FIXED** | all zeros | often higher mean / `%win` on float voice |

If after **21** a fresh window still shows non-zero amp path counters, the method did **not** apply (chase param/debug path).

**Dense note-on retrigger A/B** (after flashing note-on MAIN children)

1. `RUNNING_AVERAGE` on, FINE off. Reset (**11**), then play many note-ons for ≥1 s (want
   `note-on retrigger` count ≫ 4). Dump (**10**).
2. Under `note-on retrigger`, read `retrig SM apply` and `retrig RANGE PWM`; split is a
   `voice_task` sibling.
3. Switch mode **26** / **27**, reset, same dense play, dump again. EXACT_Y: SM apply =
   disable+load+jmp+enable; SYNC_JMP: SM apply = jmp only + `RANGE PWM`.

**Speed report** (`=== AMP COMP BENCH ===`): fixed-width comparison table — method, calls, totalUs, meanNs, pctVsFloat (vs `FLOAT_QUAD`). Methods: `FLOAT_QUAD` (cached), `LUT`, `FIXED`. Workload is the **same 0.01 Hz grid as accuracy** (`1.00…AMP_COMP_MAX_HZ`, **one osc** / `AMP_COMP_BENCH_OSCS` — synthetic cal is identical per osc; header `grid=… oscs=`). No vibrato / coarse step. ~700k calls per method; wait for paced Board pane output. Absolute `meanNs` depends on `live_method=` (Core 1 keeps running live amp-comp during the Core 0 one-shot); prefer `pctVsFloat` for ranking (LUT cheapest).

**Accuracy report** (`=== AMP COMP ACCURACY ===`): per-method plain-English summary of error vs `FLOAT_QUAD` (gold) for `LUT` / `FIXED`. Errors are in **RANGE PWM counts** (full scale = `DIV_COUNTER`), also shown as **% of full**. Grid is every **0.01 Hz** from **1.00 to `AMP_COMP_MAX_HZ`** on **one osc** (same as speed). Each method prints: typical (mean) PWM, worst in-band (freq below max Hz), tip outlier if exact max Hz disagrees by more than 1 PWM, rate of samples **worse than 1 PWM** (`|e| > 1`), and rate of samples **exactly 1 PWM** (`|e| == 1`). Plus `LUT integer-Hz sanity` over `0…AMP_COMP_MAX_HZ` (want 0). LUT indexes by **nearest** integer Hz (not trunc). One-shot is heavy — wait for paced Board pane output.

Implementation: [`../amp_comp_bench.ino`](../amp_comp_bench.ino), wired from `apply_param_debug_command` and `bench_poll_core0()`. dco_control Diagnostics exposes the buttons via `AMP_COMP_COMMANDS` in [`../tools/dco_control/params.py`](../tools/dco_control/params.py).

## 9. Pitch interpolators (cmds 28 / 29)

Compare FLOAT / FLOAT_FAST / RATIO_Q16 / Q12 for speed and a dual-reference accuracy report. Tables and interpolators live **only** in [`../pitch_interp_bench.ino`](../pitch_interp_bench.ino) (private BSS, lazy-filled); the live voice path is untouched. No extra compile flag — needs `RUNNING_AVERAGE` for paced `bench_out_*` TX (same as the profiler dump).

| `PARAM_DEBUG_COMMAND` | Effect |
|----------------------:|--------|
| 28 | Speed bench all methods → `bench_out_*` paced TX |
| 29 | Dual-ref accuracy (vs FLOAT walk + vs private Q20 slope ref) → same output path |

Live hot path uses compile-time `PITCH_INTERP_MODE`: `FLOAT` (walk), `FLOAT_FAST` (trunc+clamp±1, `noinline`; RP2350 default), `RATIO_Q16`, or `Q12`. Cmd 28/29 private tables always compare FLOAT / FLOAT_FAST / RATIO / Q12; `live_pitch=` reports the compiled mode. Use 28/29 to rank candidates; **live miss rate / mean gate** above before accepting find edits.

**Speed report** (`=== PITCH INTERP BENCH ===`): two tables (same four methods), each with calls / totalUs / meanNs / `pctVsFloat` (**that table’s FLOAT** = 100%):

| pattern | step | Role |
|---------|------|------|
| `seq` | `0.0001` | Walk-favorable (hit / +1 segment). FLOAT may beat FLOAT_FAST — not a live regression. |
| `jump` | `0.05` (~2.5 segments), repeats to ≈ seq call count | Miss-heavy; ranking should track live mean under pitch mod. |

Live cross-check: profiler path counters (`miss_direct`, `walk_steps max` ≤ 1, `miss_bsearch` = 0, miss rate often ~60–70% with pitch mod).

**Flag-path** under the compiled voice engine (header `speed=flag-path voice=FIXED|FLOAT`):

- **Fixed voice** (`!USE_FLOAT_VOICE_TASK`): cost to **`ratioQ16`**. `RATIO_Q16` = fused `slopeQ20` path; `Q12` = `interp_y` + live `y→ratio` reciprocal. `FLOAT` / `FLOAT_FAST` = soft-float references only. Int rows use `xInt` step 1 (seq) / 500 (jump). Shared Q24→xQ16 and final `freq×ratio` omitted.
- **Float voice** (`USE_FLOAT_VOICE_TASK`): cost to **float ratio**. `FLOAT` = walk find; `FLOAT_FAST` = live trunc+clamp±1 find; int modes = `mod×10000` → `lroundf` → int interp → float rescale.

**Accuracy report** (`=== PITCH INTERP ACCURACY ===`): fine **seq** modifier grid only (**one osc**) — correctness, not speed ranking. Private **Q20 `y` lerp** is the int reference only (not a live mode). Errors in **cents**. Sections:

1. **Table quantization floor** — knot `|Q20_ref − FLOAT|` mean/max cents.
2. **vs FLOAT walk** — FLOAT_FAST (expect ~0¢), RATIO / Q12: mean/max, p50/p95/p99, `>0.5¢` / `>1.0¢`.
3. **vs Q20 ref** — FLOAT / FLOAT_FAST / RATIO / Q12 vs private Q20→ratio. Mean/max, percentiles, `>0.01¢` / `>0.1¢`, knot vs mid. (Float rows ≈ table gap; RATIO/Q12 rank slope A/B.)
4. **vs Q20 ref by mod-band** — low / mid / high (same four methods).

Diagnostics buttons: `PITCH_INTERP_COMMANDS` in [`../tools/dco_control/params.py`](../tools/dco_control/params.py).

## 10. Noise engines (`NOISE_ENGINE`)

Engines live in the [`DCO_Noise`](../../DCO_Noise/) Arduino library. The sketch declares `noise0`…`noise1` with ctor args in [`../noise.h`](../noise.h) (`NOISE_ENGINE` in [`../DCO.ino`](../DCO.ino)); `next()` in `loop1`. PIO white via `dcoNoisePioBegin` / `dcoNoisePioRefill`.

| Value | Class | Hot path |
|------:|-------|----------|
| 0 | `ColoredNoise` | Voss pink / 1-pole brown / white; `PioNoiseWhite` FIFO seed → xorshift peels |
| 1 | `FastNoiseGen` | Economy Voss pink / leaky brown / local xorshift white |
| 2 | `PrimeHybridNoise` | Three prime tables summed (`process`); color at `begin` |
| 3 | `ProNoise32` | Q16.15 Kellett pink / DC-corrected brown / xorshift white |

**Reproduce**

1. `#define RUNNING_AVERAGE` (stage probes on).
2. Set `NOISE_ENGINE` to `0`…`3`; leave `ENABLE_NOISE_OUT` **undefined** for a clean A/B (no pin drain on engines 1–3).
3. Set colors via `noise0`…`noise1` ctor args in `noise.h` (default white / pink; Q15 out).
4. Dump with debug command **10**.

**Probes** (under `noise_gens` / `loop1_noise`)

| Probe | What it times |
|-------|----------------|
| `noise_gens` | Refill helper + `noise0`…`noise1.next()` |
| `noise refill` | `dcoNoisePioRefill()` — no-op unless engine **0** or `ENABLE_NOISE_OUT` |

Per-color cost: configure which slots are white/pink/brown and compare parent `%win` across builds, or temporarily time a single `next()` by hand. Prefer `%win` / `total` for share of the window.

**Reference results (RP2040)**

`RUNNING_AVERAGE`, historical four-gen reference (white / pink / brown / alt white); live fleet is two gens (`noise0`/`noise1`). `ENABLE_NOISE_OUT` off.
Means in µs unless suffixed with `c` (cycles). Counts differ across dump sessions — compare **mean** / **`%win`**, not `total`.

| Engine | `noise_gens` mean | `%win` |
|-------:|------------------:|-------:|
| 0 | 11.62 µs | 5.2 |
| 1 | 7.12 µs | 4.4 |
| 2 | 13.62 µs | 6.9 |
| 3 | 10.00 µs | 6.0 |

Engine 0:

```
probe                       count      mean       min       max      total   %win
noise_gens                  75286     11.62      2.85     34.96    874587.12    5.2
  noise refill              75286      2.80      0.41     10.27    210529.33    1.2
```

Engine 1:

```
probe                       count      mean       min       max      total   %win
noise_gens                 780032      7.12      3.11     24.61   5555627.00    4.4
  noise refill             780032        8c        4c       56c     25999.83    0.0
```

Engine 2:

```
probe                       count      mean       min       max      total   %win
noise_gens                  52928     13.62      6.98     34.63    721196.71    6.9
  noise refill              52928       44c        4c      107c      9356.93    0.0
```

Engine 3:

```
probe                       count      mean       min       max      total   %win
noise_gens                  56302     10.00      5.38     33.42    563379.54    6.0
  noise refill              56302       11c        4c      154c      2688.02    0.0
```

**Reading notes**

- **Engine 0:** PIO cost is mostly **`noise refill`** (~2.80 µs here); filter work sits in the parent around the refill child.
- **Engine 1:** Cheapest parent on this RP2040 set (`noise_gens` ~7.12 µs / 4.4%win); refill is a no-op (~8c).
- **Engine 2:** Table-sum path (~9 KB heap per gen); **highest** `noise_gens` mean / `%win` here (~13.62 µs / 6.9).
- **Engine 3:** Kellett pink is heavy per sample, but with the default mixed colors the **parent** mean (~10 µs) sits between engines 0 and 1.
