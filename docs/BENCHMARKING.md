# Hot-path benchmarking

How to measure where the DCO's realtime budget goes, and how to read the numbers without
fooling yourself.

- Flags: [`ENGINE_OPTIONS.md`](ENGINE_OPTIONS.md)
- Code: [`../bench.h`](../bench.h), probes in [`../DCO.ino`](../DCO.ino) and [`../voices.ino`](../voices.ino)

---

## 1. Turning it on

Uncomment in [`../DCO.ino`](../DCO.ino):

```c
#define RUNNING_AVERAGE        // all main probes
#define RUNNING_AVERAGE_FINE   // plus the tiny per-stage probes
```

With both off, every `BENCH_*` macro expands to nothing. There is no runtime cost and no
storage in the shipping build.

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

Hit **Reset profiler** (or dump once soon after enabling the ~1 Hz toggle) so windows stay
around a second. A multi-minute window is still a valid average, just hard to skim.

## 3. Reading the output

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

**`(unattributed)`** is the parent's total minus everything its children claimed. It is not
vanished time:

- With **FINE off**, under `voice_task` it is mostly the FINE-tier stages that still ran
  (pitch bend, modifiers, PW arithmetic, …) plus any real holes (e.g. RANGE PWM writes under
  the 99 µs gate, which are deliberately left unprobed).
- With **FINE on**, that row also includes probe bookkeeping: each child's `BENCH_END` updates
  stats after its end timestamp, so the cost sits inside the parent but not in any child.
  A/B `voice_task TOTAL` with FINE off to measure that tax before chasing gaps.

**Gate counts vs loop count.** If `ADSR_update` / PW rows have the same count as
`voice_task TOTAL`, the core 1 loop is slower than their 99/100 µs gates, so the gates fire
every iteration. When the loop is faster (typical with FINE off), those counts drop to about
half — that is correct, not double-counting.

**Sub-µs stages.** Cycle probes whose mean is under 1 µs print mean/min/max as cycles (suffix
`c`); `total` and `%win` stay in microseconds so the budget still adds up. A row of
`0.00` µs was a display floor, not dead code.

**`min` / `max`** are the jitter. For a realtime loop the max is usually more actionable than
the mean; a stage with a 0.6 µs mean and a 40 µs max is a worse problem than a steady 5 µs one.

Reset happens on every report, so each dump describes the window since the previous one.

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
