# DCO File Index

Purpose of **every file**, and for each source function: **what it does**, **who calls it**, and **when**.

- Deep narrative: [`REFERENCE_AI.md`](REFERENCE_AI.md)
- Engine flags: [`ENGINE_OPTIONS.md`](ENGINE_OPTIONS.md)
- Hot-path profiling: [`BENCHMARKING.md`](BENCHMARKING.md)
- Repo entry / doc index: [`../README.md`](../README.md)

Headers with no bodies are marked **no function definitions**.  
**Dead** = no live callers. **Unreachable** = call site exists but cannot run as currently gated. **`#if 0`** = compiled out.

---

## Call-flow overview

```mermaid
flowchart TD
  fw0["Arduino Core0"] --> setup0["setup()"]
  fw0 --> loop0["loop()"]
  fw1["Arduino Core1"] --> setup1["setup1()"]
  fw1 --> loop1["loop1()"]

  setup0 --> initSerial["init_serial / init_midi / init_LFOs"]
  setup1 --> initCore1["init_FS / init_ADSR / init_pwm / init_pio / init_voices"]

  loop0 --> midiRead["MIDI_*.read → handle* → note_on/off"]
  loop0 --> serialTask["serial_panel_task → handlers"]
  loop0 --> lfo["LFO1 every iter; LFO2/DRIFT ~100µs"]
  lfo --> fifo["FIFO Q24 detune → Core1"]

  serialTask --> updParam["update_parameters → apply_param_*"]
  updParam -->|calibrationFlag| calBranch

  loop1 --> millis["millisTimer()"]
  loop1 --> calBranch{"calibrationFlag?"}
  calBranch -->|auto| dcoCal["DCO_calibration() blocking"]
  calBranch -->|manual| manCal["voice_task_autotune + DCO_calibration_debug"]
  calBranch -->|play| play["ADSR_update ~10kHz + voice_task_main"]
  fifo --> play
```

| Context tag | Meaning |
|-------------|---------|
| Framework | Arduino invokes `setup` / `loop` / `setup1` / `loop1` |
| Boot Core0 / Core1 | Inside `setup` / `setup1` |
| Every `loop` / `loop1` | Realtime forever loops |
| MIDI callback | Dispatched from `MIDI_*.read()` in `loop` |
| Serial2 | Parser command on the Input link (DCO `Serial2`) |
| Param table | Only via `paramTable[]` / `param_router_apply` |
| Auto-cal / Manual-cal | `loop1` calibration branches |
| Hot path | Inside `voice_task` / `voice_task_float` |

---

## 1. Entry / build / globals

### `DCO.ino`

Main sketch: dual-core setup/loops, USB init (product DCO3-MONO), engine flags (**pitch ids** / **board defaults** / **overrides** / **guards** at top). Monosynth: 1 voice × 3 oscillators.

**Functions**
- `setup()` — Core 0 init: serial, MIDI, LFOs, pins, USB strings, cal pin.
  - **Called from:** Arduino framework (Core 0).
  - **When:** Boot once.
- `setup1()` — Core 1 init: PID, FS, ADSR, `mod_matrix_init`, amp-comp precompute, PWM, PIO, voices; clears cal flags.
  - **Called from:** Arduino framework (Core 1).
  - **When:** Boot once. (`init_DCO_calibration` block below is unreachable — see that function.)
- `loop()` — Core 0: MIDI read, Serial2 pump, LFO1; ~100 µs LFO2 + drift + FIFO push.
  - **Called from:** Arduino framework (Core 0).
  - **When:** Forever.
- `loop1()` — Core 1: `millisTimer`; auto/manual cal **or** ADSR + FIFO pop + `voice_task_main`; ends with `bench_service(1)`.
  - **Called from:** Arduino framework (Core 1).
  - **When:** Forever.

**Key macros:** `USE_FLOAT_VOICE_TASK`, `USE_FLOAT_AMP_COMP`, `PITCH_INTERP_MODE` (`FLOAT` / `RATIO_Q16` / `Q12`), `HIGH_PRECISION_CLKDIV`, `AMP_COMP_METHOD_DEFAULT`, `RUNNING_AVERAGE`, `RUNNING_AVERAGE_FINE`.

### `bench.h`

Hot-path profiler. All storage, ids, nesting and labels are generated from the single
`BENCH_PROBES` X-macro table, so a probe cannot exist at a call site and be missing from the
report. Time source is SysTick read as a 24-bit cycle counter (RP2040's M0+ has no DWT), with
`BENCH_PERIOD()` falling back to the 1 µs timer for spans that can exceed the 24-bit wrap.
Compiles to nothing without `RUNNING_AVERAGE`. Core0 IO is split into `MIDI read` vs
`serial panel/USB`. Note-on: `retrig period split` under voice_task; under note-on,
`retrig SM apply` + `retrig RANGE PWM` (do not slice SM apply — cold XIP mis-ranks kids).

**Subsystem reference:** [`BENCHMARKING.md`](BENCHMARKING.md).

**Functions**
- `bench_init_core()` — Arm this core's SysTick and calibrate the probe overhead.
  - **Called from:** `setup()` and `setup1()`.
  - **When:** Boot, once per core (SysTick is core-local).
- `bench_now()` / `bench_span()` / `bench_us_now()` — Raw counter reads and wrap-safe deltas.
  - **Called from:** `BENCH_*` macros; `clkdiv_bench_sample()`.
- `bench_service()` — Snapshot and clear this core's probes on request.
  - **Called from:** `bench_poll_core0()` for core 0; `loop1()` for core 1.
- `bench_poll_core0()` — Drive the handshake and print the report; calls `print_clkdiv_bench()`.
  - **Called from:** `loop()`.
  - **When:** Every iteration; prints only once both cores have answered a dump request.
- `bench_reset_all()` — Clear every accumulator.
  - **Called from:** `apply_param_debug_command()` value 11.
- `bench_print_report()` / `bench_print_core()` / `bench_print_row()` / `bench_print_unattributed()` — Format the budget table.
  - **Called from:** `bench_poll_core0()`.

### `include_all.h`

Umbrella include. **No function definitions.**

### `globals.h`

Shared constants, pins, state, prototypes. **No function definitions.**

### `tusb_config.h`

TinyUSB `#define`s. **No function definitions.**

### `usb_descriptors.c`

Legacy descriptors — fully commented. **No active function definitions.**

---

## 2. Voice / oscillator / PWM path

### `voices.h`

Declarations / portamento & table state. **No function definitions.** Carries no profiler
declarations: probe storage lives in the `bench.h` table instead, because the extern block
that used to be here drifted out of step with `voices.ino` and compiled anyway.

### `voices.ino`

Real-time voice engine (float/fixed), allocation, pitch tables, amp/PW helpers.

**Functions**
- `init_voices()` — Initial notes, tables, voice mode, first `voice_task_main()`.
  - **Called from:** `setup1()`.
  - **When:** Boot Core1.
- `noteQ16_to_freqQ24()` — Q16 semitone → Q24 Hz.
  - **Called from:** `voice_task()` (slew portamento).
  - **When:** Fixed hot path.
- `float_to_q24()` — Float → Q24.
  - **Called from:** **none (dead)** — defined but unused.
- `noteIndex_to_freqFloat()` — Float note index → Hz.
  - **Called from:** `voice_task_float()` (portamento).
  - **When:** Float hot path.
- `voice_task_main()` — Dispatch to float or fixed voice task.
  - **Called from:** `loop1()` (play path); `init_voices()` (boot kick).
  - **When:** Every play-path `loop1` iter + once at boot.
- `voice_task()` — Fixed-point hot path.
  - **Called from:** `voice_task_main()` when `!USE_FLOAT_VOICE_TASK`.
  - **When:** Fixed-engine builds only.
- `voice_task_float()` — Float hot path (current default).
  - **Called from:** `voice_task_main()` when `USE_FLOAT_VOICE_TASK`.
  - **When:** Every play-path `loop1` iter (current checkout).
- `voice_task_autotune()` — Drive one osc for calibration measurement.
  - **Called from:** `loop1()` (manual cal); `measure_gap_for_amp` / PW & freq search helpers in `PID.ino` / `autotune.ino`; unreachable call in `setup1`.
  - **When:** Manual-cal every `loop1`; nested during auto-cal measurements.
- `get_free_voice_sequential()` — Round-robin free voice.
  - **Called from:** `note_on()` when `polyMode == 1`.
  - **When:** MIDI note-on.
- `get_free_voice()` — Oldest/steal free voice.
  - **Called from:** `note_on()` when `polyMode == 0`.
  - **When:** MIDI note-on.
- `setVoiceMode()` — Apply `voiceMode` → `NUM_VOICES` / `STACK_VOICES`.
  - **Called from:** `init_voices()`; `apply_param_voice_mode()`.
  - **When:** Boot; Serial2 param.
- `setSyncMode()` — Rebuild sync topology via `assign_sm_mapping()` + `start_voice_sms()`; retrigger.
  - **Called from:** `apply_param_sync_mode()`, `apply_param_soft_sync()`.
  - **When:** Serial2 param.
- `get_chan_level_lookup_fast()` — Q8 Hz → range PWM (always compiled; live FIXED / fixed `voice_task`).
  - **Called from:** `voice_task()` directly; `get_chan_level_for_engine()` / method FIXED.
  - **When:** Fixed hot path; float-engine FIXED method / benches.
- `get_chan_level_float_quad()` — Float quadratic reference (Hz).
  - **Called from:** LUT fill; accuracy bench; method FLOAT_QUAD.
  - **When:** `USE_FLOAT_AMP_COMP`.
- `get_chan_level_lut()` — Dense 1 Hz LUT (nearest Hz index).
  - **Called from:** `get_chan_level_for_engine` when method LUT.
  - **When:** `USE_FLOAT_AMP_COMP` (speed A/B; live default = FIXED).
- `get_PW_level_interpolated()` — Map PW counter into calibrated limits/center.
  - **Called from:** `voice_task()` / `voice_task_float()` (99 µs PW update).
  - **When:** Hot path.
- `interpolatePitchMultiplierIntQ16_cached()` — IntQ16 table interp (`PITCH_INTERP_Q12`).
  - **Called from:** `voice_task()` or `voice_task_float()` (A/B glue) when mode is Q12.
  - **When:** Alternate pitch modes (fixed voice or float-voice A/B).
- `interpolateRatioQ16_cached()` — Table → ratio Q16 (`slopeQ20`).
  - **Called from:** `voice_task()` or `voice_task_float()` (A/B glue) when `PITCH_INTERP_MODE == PITCH_INTERP_RATIO_Q16`.
  - **When:** Default fixed pitch mode; optional float-voice A/B.
- `interpolateRatioFloat_cached()` — Natural modifier `[-1,3]` → float ratio (`slopeF`; unscaled tables).
  - **Called from:** `voice_task_float()` when `PITCH_INTERP_MODE == PITCH_INTERP_FLOAT`.
  - **When:** Float hot path (board default on RP2350 / `USE_FLOAT_VOICE_TASK`).
- `initMultiplierTables()` — Build tables/slopes for the active `PITCH_INTERP_MODE` only (uses `expInterpolationSolveY`).
  - **Called from:** `init_voices()`.
  - **When:** Boot Core1.
- `clkdiv_bench_sample()` — Run the float and double divider candidates side by side and accumulate the time and Hz difference.
  - **Called from:** `voice_task_float()`, just before the live divider math.
  - **When:** Float hot path, `CLKDIV_BENCHMARK` only; compiles away otherwise.
- `print_clkdiv_bench()` — Report and clear that comparison.
  - **Called from:** `bench_poll_core0()` (`bench.h`), on core 0.
  - **When:** Each profiler dump.

### `noteList.h`

Note → frequency tables. **No function definitions.**

### `state_machines.h`

Prototypes plus the inline period model: `pio_period_split()` (exact split, remainder into
Y), `pio_clk_div_for_y()` (rounded, for a Y that must not move), `osc_ramp_weight()` /
`osc_period_overhead()` (per-program constants), and the jump-target helpers
`osc_restart_target()` / `osc_ramp_entry_target()`.

### `state_machines.ino`

PIO load, SM setup, sync topology and diagnostics. **All three oscillators live on pio0**
(SM0/1/2) so they can share a reset pin — see `pio_topology_report()`.

**Subsystem reference:** [`PIO_OSCILLATORS.md`](PIO_OSCILLATORS.md).

**Functions**
- `sync_slave_osc()` / `sync_master_osc()` — Resolve `syncMode` into slave/master indices.
  - **Called from:** `assign_sm_mapping()`, `start_voice_sms()`, `pio_topology_report()`.
- `assign_sm_mapping()` — Rewrite `VOICE_TO_SM` so the slave outranks-below its master.
  - **Called from:** `init_pio()`, `setSyncMode()`.
  - **When:** Boot; sync topology change.
- `init_pio()` — Load both oscillator programs into pio0 and the sub-osc programs into pio1.
  - **Called from:** `setup1()`.
  - **When:** Boot Core1.
- `start_voice_sms()` — Per-osc program/pin/sideset selection, Y preload, same-cycle start
  via `pio_enable_sm_mask_in_sync()`.
  - **Called from:** `init_pio()`, `setSyncMode()`.
  - **When:** Boot; sync topology change.
- `osc_load_period_stopped()` / `_noclear` / `osc_load_periods_stopped_noclear()` — `static inline` in `state_machines.h`: push Y + clk_div to a
  **stopped** SM via direct TXF/instr MMIO (Y travels through the OSR, which also feeds the
  chunk reads). Clear variant for boot/topology; fused noclear for note-on EXACT_Y.
  - **Called from:** `start_voice_sms()`, `osc_set_reset_pulse()` (clear); both engine note-on
    EXACT_Y paths (fused noclear).
- `osc_set_reset_pulse()` — Change only Y, reusing `osc_last_clk_div[]`.
  - **Called from:** `apply_param_osc_sync_mode()`.
- `pio_topology_report()` — Print sync roles and assert every RESET pin reads back as PIO0.
  - **Called from:** Bench/diagnostic use.
- `pio_period_probe()` / `pio_solve_period_model()` — Bench helpers for confirming the
  period weight and overhead against a frequency counter.
- `set_subosc_divide()` — (Re)configure the sub-oscillator on pio1.
  - **Called from:** `init_pio()`, `apply_param_subosc_divide()`.

### `pico-dco.pio`

PIO assembly source. **Not C functions.** Programs in use: `frequency_sync_4_jumps`
(free-running, weight 4), `frequency_sync_poll` (soft-sync slave, polls `jmp pin` in the
final chunk, weight 5), `subosc_div2` / `subosc_div4`.

Annotated listings and the period model for each program: [`PIO_OSCILLATORS.md`](PIO_OSCILLATORS.md).

### `pico-dco.pio.h`

PIO C wrappers. **Hand-maintained** — Arduino does not run `pioasm`, so new programs must be
assembled by hand and kept in step with `pico-dco.pio`. The encoding is documented inline
above `frequency_sync_poll`.

**Functions**
- `frequency_program_get_default_config()` — Default config for basic frequency program.
  - **Called from:** `init_sm_pin()`.
  - **When:** Only if `init_sm` path used (currently dead).
- `init_sm_pin()` — Init SM for basic frequency program.
  - **Called from:** `init_sm()`.
  - **When:** Dead path today.
- `frequency_sync_program_get_default_config()` — Default config for sync program.
  - **Called from:** `frequency_sync()`.
  - **When:** Only if that init used (**dead** — not called from project code).
- `frequency_sync()` — Init SM for older sync program.
  - **Called from:** **none (dead)** — production uses `frequency_sync_4_jumps`.
- `frequency_sync_4_jumps_program_get_default_config()` — Default config for production program.
  - **Called from:** `frequency_sync_4_jumps()`.
  - **When:** Boot SM init.
- `frequency_sync_4_jumps()` — Init SM for production 4-jump sync program.
  - **Called from:** `init_sm_sync()`.
  - **When:** Boot.
- `frequency_pulse1_program_get_default_config()` — Default config for pulse1 program.
  - **Called from:** `frequency_pulse1_program_init()`.
  - **When:** Only if pulse1 init used.
- `frequency_pulse1_program_init()` — Init SM for pulse1 program.
  - **Called from:** **none (dead)** in this firmware.

### `PWM.h`

Prototype. **No function definitions.**

### `PWM.ino`

**Functions**
- `init_pwm()` — Configure range + PW PWM slices (`RANGE_PWM_SLICES` / `RANGE_PWM_CHANNELS`);
  under `ENABLE_CV_OUTS` calls `init_cv_pwm()`.
  - **Called from:** `setup1()`.
  - **When:** Boot Core1.
- `init_cv_pwm()` — Cutoff / reso / VCA / dist PWM; calls `init_level_pwm()`.
  - **Called from:** `init_pwm()` when `ENABLE_CV_OUTS`.
- `init_level_pwm()` / `write_level_pwm()` / `write_level_pwm_raw()` — OSC1/2/3 + Sub level PWM → mix VCAs.
  - **Called from:** `init_cv_pwm()`; `mod_matrix_apply_cv()`; manual cal.
- `write_cv_pwm()` / `write_cv_pwm_raw()` — Push soft VCF/VCA/per-filter reso/dist compares.

### `wave_mux.h` / `wave_mux.ino`

Per-osc Saw/Pulse/Tri via dual 74HC595 → DG411. Docs: [`WAVE_MUX.md`](WAVE_MUX.md).

**Functions**
- `init_waveSelector()` — GPIO + all bits off.
  - **Called from:** `setup1()`.
- `update_waveSelector()` — Rebuild 9 bits from `waveEnable[3][3]`.
  - **Called from:** wave-enable apply handlers; cal restore.
- `waveSelector_manual_calibration(stage)` — Solo OSC{stage} Saw.
  - **Called from:** `update_CV_outs_manual_calibration()`.

### `mod_matrix.h` / `mod_matrix.ino`

Sparse mod matrix (8 slots). Docs: [`MOD_MATRIX.md`](MOD_MATRIX.md).

**Functions**
- `mod_matrix_init()` — Clear slots / AT / random.
  - **Called from:** `setup1()`.
- `mod_matrix_set_source/dest/depth()` — Slot table from ParamIds 60–83.
  - **Called from:** `params.ino` apply handlers.
- `mod_matrix_on_note_on()` / `mod_matrix_set_aftertouch()` — Random S&H + AT source.
  - **Called from:** `note_on()` / MIDI AT callback.
- `mod_matrix_accumulate()` / `mod_matrix_apply_cv()` — Sum → level PWM + `RESONANCE_PWM[]` + Dist Drive out.
  - **Called from:** `update_CV_outs()`.

### `amp_comp.h`

Amp-comp tables, LUT, method select, dual precompute/facade.

**Functions**
- `precomputeCoefficients()` — Fixed Q-window precompute (also under float engine for FIXED).
  - **Called from:** `precompute_amp_comp_for_engine()`.
  - **When:** Boot / after auto-cal reload.
- `precomputeCoefficients_float()` — Float Hz quadratic precompute.
  - **Called from:** `precompute_amp_comp_for_engine()` when `USE_FLOAT_AMP_COMP`.
  - **When:** Boot / after auto-cal reload.
- `fill_amp_comp_lut_from_quad()` — Fill `ampCompLut[osc][0..7000]` from float quad.
  - **Called from:** `precompute_amp_comp_for_engine()` when `USE_FLOAT_AMP_COMP`.
  - **When:** Boot / after auto-cal reload.
- `precompute_amp_comp_for_engine()` — Float+LUT+fixed dual build, or fixed-only.
  - **Called from:** `setup1()`; end of `DCO_calibration()`.
  - **When:** Boot Core1; end of auto-cal.
- `amp_comp_set_method()` / `get_chan_level_for_engine()` — Runtime method + Hz → PWM facade.
  - **Called from:** debug cmds 20–22; `voice_task_float()`, `voice_task_autotune()`.
  - **When:** Float play path / cal / A/B.

### `amp_comp_bench.ino`

Amp-comp speed/accuracy one-shots (`AMP_COMP_BENCHMARK` + `RUNNING_AVERAGE`).

**Functions**
- `amp_comp_bench_run_speed()` / `amp_comp_bench_run_accuracy()` / `print_amp_comp_bench()`
  - **Called from:** `bench_poll_core0()` when debug 24/25 pending.
  - **When:** Diagnostics; paced `bench_out_*` TX.

### `pitch_interp_bench.ino`

Pitch-interpolator speed/accuracy one-shots (`RUNNING_AVERAGE`). Self-contained private tables + interpolators. Speed rows: FLOAT / RATIO_Q16 / Q12; accuracy keeps a private Q20 `y` reference (not a live mode).

**Functions**
- `pitch_interp_bench_run_speed()` / `pitch_interp_bench_run_accuracy()` / `print_pitch_interp_bench()`
  - **Called from:** `bench_poll_core0()` when debug 28/29 pending.
  - **When:** Diagnostics; paced `bench_out_*` TX.

---

## 3. Modulation (ADSR / LFO)

### `adsr.h`

Globals / instances. **No function definitions.**

### `adsr.ino`

**Functions**
- `init_ADSR()` — Tables + initial A/D/S/R (uses `linearToLogarithmic`).
  - **Called from:** `setup1()`.
  - **When:** Boot Core1.
- `ADSR_update()` — Advance envelopes from note flags; update levels; lazy params.
  - **Called from:** `loop1()` play path when `(micros delta) > 100`.
  - **When:** ~10 kHz while playing.
- `ADSR_set_parameters()` — Debounced A/D/S/R apply to all voices.
  - **Called from:** `ADSR_update()`.
  - **When:** Nested in ADSR update.
- `ADSR1_set_restart()` — Set restart/legato mode on all voices.
  - **Called from:** **none (dead)**.
- `ADSR1_change_curves()` — Re-apply after curve change.
  - **Called from:** **none (dead)**.
- `ADSR_VCA_set_restart()` / `ADSR_VCF_set_restart()` — Restart/legato for EnvVCA / EnvVCF.
  - **Called from:** `apply_param_vca_adsr_restart()` / `apply_param_vcf_adsr_restart()` (`params.ino`).
- `ADSR_VCA_change_attack_curve()` / `ADSR_VCA_change_decay_curve()` — EnvVCA curve shape, then re-apply A/D/S/R.
  - **Called from:** `apply_param_adsr1_attack_curve()` / `apply_param_adsr1_decay_curve()` (`params.ino`).
- `ADSR_VCF_change_attack_curve()` / `ADSR_VCF_change_decay_curve()` — EnvVCF curve shape, then re-apply A/D/S/R.
  - **Called from:** `apply_param_adsr2_attack_curve()` / `apply_param_adsr2_decay_curve()` (`params.ino`).

### `LFO.h`

Prototypes / instances. **No function definitions.**

### `LFO.ino`

**Functions**
- `init_LFOs()` — Init LFO1 + LFO2.
  - **Called from:** `setup()`.
  - **When:** Boot Core0.
- `init_LFO1()` — Configure main detune LFO.
  - **Called from:** `init_LFOs()`.
  - **When:** Boot.
- `init_LFO2()` — Configure secondary LFO.
  - **Called from:** `init_LFOs()`.
  - **When:** Boot.
- `init_DRIFT_LFOs()` — Init all drift LFOs.
  - **Called from:** `setup()`.
  - **When:** Boot Core0.
- `init_DRIFT_LFO()` — Init one drift LFO (uses `expConverterFloat`).
  - **Called from:** `init_DRIFT_LFOs()`; also re-speed from param applies.
  - **When:** Boot; Serial2 drift params.
- `LFO1()` — Update LFO1 → `DETUNE_INTERNAL_q24`.
  - **Called from:** `loop()` every iteration.
  - **When:** Realtime Core0.
- `LFO2()` — Update LFO2 depths.
  - **Called from:** `loop()` when ~100 µs elapsed.
  - **When:** Realtime Core0.
- `DRIFT_LFOs()` — Update `LFO_DRIFT_LEVEL[]`.
  - **Called from:** `loop()` when ~100 µs elapsed (with LFO2).
  - **When:** Realtime Core0.

---

## 4. Calibration / storage / experimental

### `autotune.h`

Globals / types / prototypes. **No function definitions.**

### `autotune_constants.h`

Constants only. **No function definitions.**

### `autotune_context.h`

**Functions**
- `DCOCalibrationContext::DCOCalibrationContext(...)` — Bind refs for `calibrate_DCO`.
  - **Called from:** `DCO_calibration()` when constructing context.
  - **When:** Auto-cal per oscillator.

### `autotune_measurement.h`

**Functions**
- `measure_gap()` — Wrap `find_gap()` with timeout flag.
  - **Called from:** PW search in `autotune.ino`; `measure_gap_for_amp`, `find_highest_freq`, `find_lowest_freq`, `DCO_calibration_debug`.
  - **When:** Auto-cal / manual-cal measurement.

### `autotune.ino`

**Functions**
- `disable_all_oscillators_and_range_pwm()` — Mute oscs / park RANGE GPIO; calls `reset_pw_to_DIV_COUNTER_PW`.
  - **Called from:** `init_DCO_calibration()`, `DCO_calibration()`, `restart_DCO_calibration()`.
  - **When:** Cal setup (note `init_DCO_calibration` unreachable at boot).
- `reset_pw_to_DIV_COUNTER_PW()` — Shared PW PWM → max wrap.
  - **Called from:** `disable_all_oscillators_and_range_pwm()`.
  - **When:** Cal setup.
- `init_DCO_calibration()` — Legacy/boot cal kickoff.
  - **Called from:** `setup1()` only if `calibrationFlag` — but flag is set `false` just above → **unreachable**.
  - **When:** Would be boot; currently never.
- `DCO_calibration()` — Full auto-cal: PW center/limits once on voice 0, then `calibrate_DCO` + FS write per osc 0..2, reload, precompute; clears `calibrationFlag`.
  - **Called from:** `loop1()` when `calibrationFlag && !manualCalibrationFlag`.
  - **When:** Auto-cal (blocking one-shot).
- `restart_DCO_calibration()` — Reset state between oscillators.
  - **Called from:** `DCO_calibration()` per osc.
  - **When:** Auto-cal.
- `find_PW_for_target_duty()` — Search PW for target duty.
  - **Called from:** `find_PW_center()`.
  - **When:** Auto-cal PW stage.
- `find_PW_center()` — Find ~50% PW center; `update_FS_PWCenter`.
  - **Called from:** `DCO_calibration()` (even DCOs).
  - **When:** Auto-cal.
- `search_PW_limit_from_center()` — Walk PW toward low/high duty limit.
  - **Called from:** `find_PW_limit_v2()`.
  - **When:** Auto-cal.
- `find_PW_limit_v2()` — High-level PW limit; persist low/high via FS.
  - **Called from:** `DCO_calibration()` (LOW then HIGH).
  - **When:** Auto-cal.
- `find_gap()` — Edge-time duty/freq measurement on cal pin.
  - **Called from:** `measure_gap()`; also direct inside `#if 0` legacy PID.
  - **When:** Cal measurement (live via wrapper).
- `DCO_calibration_debug()` — Live gap → `serialSendParam32` for UI.
  - **Called from:** `loop1()` manual-cal branch every iter.
  - **When:** Manual-cal.

### `PID.h`

Globals / prototypes. **No function definitions.**

### `PID.ino`

**Functions**
- `compute_gap_tolerance_for_freq()` — Duty tolerance vs frequency.
  - **Called from:** `calibrate_DCO()`; `find_PW_center()` (via same helper visibility).
  - **When:** Auto-cal.
- `did_sign_change()` — Detect gap error sign flip.
  - **Called from:** `calibrate_DCO()`.
  - **When:** Auto-cal amp search.
- `measure_gap_for_amp()` — Set amp PWM, `voice_task_autotune`, `measure_gap`.
  - **Called from:** `calibrate_DCO()`.
  - **When:** Auto-cal.
- `update_best_from_neighbours()` — Probe neighbour amps; keep best.
  - **Called from:** `calibrate_DCO()`.
  - **When:** Auto-cal.
- `step_amp_from_error()` — Step range PWM from signed error.
  - **Called from:** `calibrate_DCO()`.
  - **When:** Auto-cal.
- `compute_initial_amp_for_note()` — Initial amp guess (uses log/quadratic interp).
  - **Called from:** `calibrate_DCO()`.
  - **When:** Auto-cal.
- `store_note_result()` — Write `[freq,pwm]` into `calibrationData`.
  - **Called from:** `calibrate_DCO()`.
  - **When:** Auto-cal per note.
- `init_PID()` — Init `PID_v1` object/mode.
  - **Called from:** `setup1()`.
  - **When:** Boot (PID still inited even though main cal no longer uses it).
- `PID_dco_calibration()` — Legacy PID note loop.
  - **Called from:** **`#if 0` (compiled out)**.
- `PID_find_highest_freq()` — Legacy PID highest-freq helper.
  - **Called from:** **`#if 0` (compiled out)**.
- `find_highest_freq()` — Search highest usable freq (Hz×100).
  - **Called from:** `calibrate_DCO()`.
  - **When:** Auto-cal span setup.
- `find_lowest_freq()` — Search lowest usable freq (uses `linearInterpolation` / quadratic).
  - **Called from:** `calibrate_DCO()`.
  - **When:** Auto-cal span setup.
- `calibrate_DCO()` — Main per-note amp-table builder.
  - **Called from:** `DCO_calibration()`.
  - **When:** Auto-cal.
- `quadraticInterpolation()` — 3-point quadratic `y(x)`.
  - **Called from:** `compute_initial_amp_for_note()`; `find_lowest_freq()`.
  - **When:** Auto-cal.
- `exponentialInterpolation()` — Exp interpolate → uint16.
  - **Called from:** **none (dead)**.
- `logarithmicInterpolation()` — Log interpolate → uint16.
  - **Called from:** `compute_initial_amp_for_note()`.
  - **When:** Auto-cal.
- `logarithmicInterpolationFloat()` — Log interpolate float.
  - **Called from:** **none (dead)**.
- `logarithmicInterpolationDouble()` — Log interpolate double.
  - **Called from:** **none (dead)**.
- `linearInterpolation()` — Linear interpolate.
  - **Called from:** `find_lowest_freq()`.
  - **When:** Auto-cal.
- `expInterpolationSolveY()` — Solve exp curve for table building.
  - **Called from:** `initMultiplierTables()`.
  - **When:** Boot (pitch tables), not cal.

### `FS.h`

Constants / buffers. **No function definitions.**

### `FS.ino`

**Functions**
- `init_FS()` — Mount LittleFS; load tables into float or Q8 arrays.
  - **Called from:** `setup1()`; end of `DCO_calibration()`.
  - **When:** Boot; after auto-cal write.
- `update_FS_voice()` — Persist one osc amp table.
  - **Called from:** `DCO_calibration()` per osc.
  - **When:** Auto-cal.
- `update_FS_PWCenter()` — Persist PW center.
  - **Called from:** `find_PW_center()`.
  - **When:** Auto-cal.
- `update_FS_PW_High_Limit()` — Persist PW high limit.
  - **Called from:** `find_PW_limit_v2()`.
  - **When:** Auto-cal.
- `update_FS_PW_Low_Limit()` — Persist PW low limit.
  - **Called from:** `find_PW_limit_v2()`.
  - **When:** Auto-cal.
- `update_FS_ManualCalibrationOffset()` — Persist manual offset.
  - **Called from:** `apply_param_manual_calibration_store()`.
  - **When:** Serial2 param (user store).

### `irq_tuner.h` / `irq_tuner.ino`

Experimental; bodies commented. **No active function definitions.**

---

## 5. MIDI, serial, parameters

### `midi.h`

Prototypes / instances. **No function definitions.**

### `midi_cc.h`

MIDI CC control surface: the `MIDI_CC_LINEAR` / `MIDI_CC_EXP_TIME` curves, the `CC_LOCAL_*` codes (224 up) for the values that arrive as `'a'`-`'f'` block frames and so have no `ParamId`, the `MidiCcEntry` layout, and the prototypes for the two functions in `midi.ino`. Includes `midi_cc_map.h`. **No function definitions.**

### `midi_cc_map.h`

**Generated** — `midiCcMap[]`, one row per controller. Emitted by `tools/dco_control/gen_midi_map.py` from `tools/dco_control/params.py`, together with `docs/MIDI_CC_MAP.md` and the Open Stage Control session in `tools/panels/`. Do not edit; change `params.py` and re-run.

### `midi.ino`

**Functions**
- `init_midi()` — Register handlers on USB + Serial1 MIDI.
  - **Called from:** `setup()`.
  - **When:** Boot Core0.
- `handleNoteOn()` — → `note_on()`.
  - **Called from:** MIDI library (registered in `init_midi`).
  - **When:** MIDI callback from `loop` `.read()`.
- `handleNoteOff()` — → `note_off()`.
  - **Called from:** MIDI library.
  - **When:** MIDI callback.
- `handleControlChange()` — CC 42 → pitch-bend range / Q24 multiplier; every other controller → `midi_cc_handle()`.
  - **Called from:** MIDI library.
  - **When:** MIDI callback.
- `midi_cc_handle()` — Find the controller in `midiCcMap[]`, scale it into `lo..hi`, apply the exp curve for envelope times, then `midi_cc_apply()`. Unmapped CCs ignored.
  - **Called from:** `handleControlChange()`.
  - **When:** MIDI callback.
- `midi_cc_apply()` — Dispatch: a `CC_LOCAL_*` target writes its block global here (`cv_update_mod_formulas()` for the filter four, `PW[0] = v / 4`), anything else goes to `update_parameters()`.
  - **Called from:** `midi_cc_handle()`.
  - **When:** MIDI callback.
- `handleProgramChange()` — Stub / empty as implemented.
  - **Called from:** MIDI library.
  - **When:** MIDI callback.
- `handlePitchBend()` — Sets `midi_pitch_bend`.
  - **Called from:** MIDI library.
  - **When:** MIDI callback.
- `note_on()` — Allocate voice(s), set ADSR/note flags (local envelopes only, nothing on the wire).
  - **Called from:** `handleNoteOn()`.
  - **When:** MIDI note-on.
- `note_off()` — Release voice(s), set `noteEnd[]` for the local envelopes.
  - **Called from:** `handleNoteOff()`.
  - **When:** MIDI note-off.

### `Serial.h`

Prototype. **No function definitions.**

### `Serial.ino`

**Functions**
- `init_serial()` — Serial1 MIDI baud (RX 1 / TX 0 @ 31250), Serial2 2.5M Input link against the Input's `Serial1` (RX 21 from Input TX GP0, TX 20 into Input RX GP1), USB CDC.
  - **Called from:** `setup()`.
  - **When:** Boot Core0.
- `input_handle_adsr1()` / `input_handle_adsr2()` / `input_handle_adsr3()` — `'a'`/`'b'`/`'c'` → EnvVCA / EnvVCF / EnvDCO (`ADSR1_*`) times.
  - **Called from:** Serial2 parser command table (`inputSerialCommands[]`).
  - **When:** Serial2 RX.
- `input_handle_filter_block()` — `'d'` → `CUTOFF`, `RESONANCE`, `ADSR2toVCF`, `LFO2toVCF`, then `cv_update_mod_formulas()`.
  - **Called from:** Serial2 parser.
  - **When:** Serial2 RX.
- `input_handle_adsr1_to_vca()` — `'e'` → `ADSR1toVCA`.
  - **Called from:** Serial2 parser.
  - **When:** Serial2 RX.
- `input_handle_pw()` — `'f'` → `PW[0]` (BE, /4 scale).
  - **Called from:** Serial2 parser.
  - **When:** Serial2 RX.
- `input_handle_param16()` — `'p'` → `update_parameters`.
  - **Called from:** Serial2 parser.
  - **When:** Serial2 RX.
- `input_handle_param8()` — `'w'` → `update_parameters`.
  - **Called from:** Serial2 parser.
  - **When:** Serial2 RX.
- `input_handle_preset_name()` — `'q'` → `presetName[]` (8 chars).
  - **Called from:** Serial2 parser.
  - **When:** Serial2 RX.
- `serial_panel_task()` — Non-blocking parser pump (`serial_parser_*`).
  - **Called from:** `loop()` every iteration.
  - **When:** Realtime Core0.
- `serial_usb_task()` — Same pump for the USB CDC port: a second `SerialParserContext` over the *same* `inputSerialCommands` table, so a host tool can send panel frames with no Input board. Guarded by `ENABLE_USB_CONTROL` in `DCO.ino`. See [`tools/dco_control`](../tools/dco_control/README.md).
  - **Called from:** `loop()` every iteration.
  - **When:** Realtime Core0, only when `ENABLE_USB_CONTROL` is defined.
- `serialSendParam32()` — TX `'x'` param32 out Serial2 TX 20 into the Input's `Serial1` RX GP1 (gap 154, cal offset 155; Input relays 154 to Screen). Waits on `availableForWrite() < 1` — a HW UART reports 0/1, not free bytes.
  - **Called from:** `apply_param_manual_calibration_flag()`; `DCO_calibration_debug()`.
  - **When:** Manual-cal param / live gap report.

### `serial_protocol.h`

**Functions**
- `serial_protocol_payload_len()` — Command → payload length.
  - **Called from:** **none (dead)** — lengths hardcoded in `inputSerialCommands[]`.

### `serial_param_protocol.h`

**Functions**
- `decode_i16_be()` — BE int16 decode.
  - **Called from:** `decode_param_p()`.
  - **When:** Serial2 param decode.
- `decode_i32_le()` — LE int32 decode.
  - **Called from:** `decode_param_x()`.
  - **When:** Serial2 param decode.
- `decode_param_p()` — Decode `'p'` frame.
  - **Called from:** `input_handle_param16()`.
  - **When:** Serial2.
- `decode_param_w()` — Decode `'w'` frame.
  - **Called from:** `input_handle_param8()`.
  - **When:** Serial2.
- `decode_param_x()` — Decode `'x'` frame.
  - **Called from:** **none on DCO** — the DCO only sends `'x'`; Input and Screen decode it.
  - **When:** —

### `serial_parser.h`

**Functions**
- `serial_parser_reset()` — Reset parser to idle.
  - **Called from:** `serial_parser_process_byte` / timeout path.
  - **When:** Serial2 parsing.
- `serial_parser_find_cmd()` — Lookup command in table.
  - **Called from:** `serial_parser_process_byte()`.
  - **When:** Serial2 parsing.
- `serial_parser_check_timeout()` — Abort partial frame.
  - **Called from:** `serial_panel_task()` while in payload state.
  - **When:** Every `loop` during RX.
- `serial_parser_process_byte()` — Feed one byte; invoke handler when complete.
  - **Called from:** `serial_panel_task()`.
  - **When:** Every available Serial2 byte.

### `params_def.h`

`enum ParamId` only. **No function definitions.**

### `param_router.h`

**Functions**
- `param_router_apply()` — Table lookup ParamId → apply callback.
  - **Called from:** `update_parameters()`.
  - **When:** Serial2 param frames.

### `params.ino`

**Functions**
- `update_parameters()` — Route param id/value through `paramTable`.
  - **Called from:** `dco_handle_param16/8/32()`.
  - **When:** Serial2 `'p'/'w'/'x'`.
- All `apply_param_*()` below — **Called from:** **param table only** (never direct). **When:** matching Serial2 ParamId.

- `apply_param_osc*_saw/pulse/tri_enable()` — Per-osc wave enables → `update_waveSelector()`.
- `apply_param_osc1_level()` / `apply_param_osc2_level()` / `apply_param_osc3_level()` / `apply_param_sub_level()` — Mix level **bases** (PWM via matrix in `update_CV_outs`).
- `apply_param_mod_slot*_source/dest/depth()` — Mod matrix slots 0–7 (ParamIds 60–83).
- `apply_param_adsr3_to_osc_select()` — ADSR3 routing select.
- `apply_param_lfo1_waveform()` — LFO1 waveform.
- `apply_param_lfo2_waveform()` — LFO2 waveform.
- `apply_param_osc1_interval()` — OSC1 interval.
- `apply_param_osc2_interval()` — OSC2 interval.
- `apply_param_osc2_detune_val()` — OSC2 detune.
- `apply_param_lfo2_to_detune2()` — LFO2→OSC2 detune depth (uses `expConverterFloat`).
- `apply_param_osc_sync_mode()` — Osc sync / phase-align related.
- `apply_param_portamento_time()` — Porta time (uses `expConverter`).
- `apply_param_portamento_mode()` — Time vs slew porta.
- `apply_param_calibration_value()` — Reserved / no-op.
- `apply_param_voice_mode()` — → `setVoiceMode()`.
- `apply_param_unison_detune()` — Unison amount.
- `apply_param_analog_drift_amount()` — Drift depth.
- `apply_param_analog_drift_speed()` — Drift speed (recomputes via `expConverterFloat`).
- `apply_param_analog_drift_spread()` — Drift spread (recomputes speeds).
- `apply_param_sync_mode()` — → `setSyncMode()`.
- `apply_param_soft_sync()` — Hard (sideset) vs soft (polled `jmp pin`) sync → `setSyncMode()`.
- `apply_param_subosc_divide()` — Sub-osc off / ÷2 / ÷4 → `set_subosc_divide()`.
- `apply_param_lfo1_to_dco()` — LFO1→DCO depth (`expConverterFloat`).
- `apply_param_lfo1_speed()` — LFO1 rate.
- `apply_param_lfo2_speed()` — LFO2 rate.
- `apply_param_lfo2_to_pw()` — LFO2→PW depth.
- `apply_param_adsr1_to_pwm()` — ADSR→PWM depth (`PARAM_ADSR3_TO_PWM`).
- `apply_param_adsr1_to_detune1()` — ADSR→detune + Q24 scale.
- `apply_param_adsr1_curve()` — Attack curve hook (light/reserved).
- `apply_param_adsr2_curve()` — Decay curve hook (light/reserved).
- `apply_param_pwm_pots_manual()` — Manual PWM pots flag.
- `apply_param_function_key()` — Function key (reserved/no-op).
- `apply_param_calibration_flag()` — Sets `calibrationFlag` → next `loop1` auto-cal.
- `apply_param_manual_calibration_flag()` — Manual cal mode; may `serialSendParam32` offsets.
- `apply_param_manual_calibration_stage()` — Manual cal stage index.
- `apply_param_manual_calibration_offset()` — Per-osc manual offset.
- `apply_param_manual_calibration_store()` — → `update_FS_ManualCalibrationOffset`.
- `apply_param_debug_command()` — Bench diagnostics (id 160): 1 → `pio_topology_report()`, 2/3 → `pio_period_probe()` at a low/high divider, 10/11/12 → profiler dump / reset / periodic toggle (`RUNNING_AVERAGE`), 20–22 → amp-comp method (FLOAT_QUAD / LUT / FIXED), 24/25 → amp-comp speed/accuracy (`AMP_COMP_BENCHMARK` + `RUNNING_AVERAGE`), 28/29 → pitch-interp speed/accuracy (`RUNNING_AVERAGE`). Period probes only hold with no note playing.

---

## 6. Timing / utilities

### `Timer_millis.h`

Flags only. **No function definitions.**

### `Timer_millis.ino`

**Functions**
- `millisTimer()` — Update soft timer flags (99 µs, ~1 ms, 200 ms, 1000 ms, …).
  - **Called from:** `loop1()` every iteration.
  - **When:** Realtime Core1. (`timer99microsFlag` used in voice PW update; `timer1000msFlag` for `RUNNING_AVERAGE`.)

### `utils.h`

Prototypes. **No function definitions.**

### `utils.ino`

**Functions**
- `uintToStr()` — uint64 → C string.
  - **Called from:** **none (dead)** — only commented use in `irq_tuner.ino`.
- `linearToLogarithmic()` — Linear → log map.
  - **Called from:** `init_ADSR()`.
  - **When:** Boot.
- `linearToExponential()` — Linear 0..4095 → exp 0..maxValue, the Input board's fader curve.
  - **Called from:** `midi_cc_handle()` for `MIDI_CC_EXP_TIME` entries (base 50, max 25000).
  - **When:** MIDI CC on an envelope attack / decay / release.
- `faderExpConverter()` — Fader exp curve.
  - **Called from:** **none (dead)**.
- `expConverterFloat()` — Exp curve → float.
  - **Called from:** `params.ino` applies; `init_DRIFT_LFO()`; also inside dead `controls_formula_update`.
  - **When:** Serial2 params; boot drift init.
- `expConverter()` — Exp curve → uint16.
  - **Called from:** `apply_param_portamento_time()`.
  - **When:** Serial2 param.
- `expConverterReverse()` — Inverse exp.
  - **Called from:** **none (dead)**.
- `expConverterFloatReverse()` — Inverse float exp.
  - **Called from:** **none (dead)**.
- `expConverter2()` — Alternate exp curve.
  - **Called from:** **none (dead)**.
- `formula_update()` — Legacy formula slot update.
  - **Called from:** **none (dead)**.
- `controls_formula_update()` — Map controls to float params.
  - **Called from:** **none (dead)**.
- `led_blinking_task()` — LED heartbeat.
  - **Called from:** **none (dead)** — declared in `globals.h` but never called.

---

## 7. Documentation

All detailed docs live under `docs/` (this file included). Root `README.md` is the entry point.

| File | Purpose |
|------|---------|
| `README.md` (repo root) | Overview, build, documentation index. |
| `docs/DOCUMENTATION_PROCEDURE.md` | Reusable phased guide to document any DCO4 board. |
| `docs/SYSTEM_OVERVIEW.md` | System / UART topology (incl. optional RP2040 voice aux). |
| `docs/DUAL_MCU.md` | RP2350A + RP2040 ownership; Input TX fanout; `ENABLE_VOICE_AUX`; ParamId table. |
| `../VOICE-AUX/` | RP2040 voice-aux sketch (Dist 52/53, mode 54); see `VOICE-AUX/docs/README.md`. |
| `docs/ENGINE_OPTIONS.md` | Float/fixed engine flags. |
| `docs/BENCHMARKING.md` | Hot-path profiler: probes, reading the budget, adding a probe. |
| `docs/DISTORTION.md` | Post-LP Drive/Mix distortion hardware idea and CV prototype. |
| `docs/FILTER_ROUTING.md` | SSI2144 → dist → AS3320 multimode concept; digital stage switching (DG411/4066). |
| `docs/schematics/distortion/` | KiCad 10 project (`distortion.kicad_pro`) for the distortion stage. |
| `docs/REFERENCE_AI.md` | Deep semantic map. |
| `docs/FILE_INDEX.md` | This file — files, functions, call sites. |
| `docs/README_serial_and_params.md` | Shared serial / ParamId how-to, including the MIDI CC path. |
| `docs/MIDI_CC_MAP.md` | **Generated** — MIDI CC implementation chart. |
| `docs/Serial_comms_and_params_reference.txt` | Earlier design notes (partially historical). |
| `docs/AUTOTUNE.md` | Autotune algorithms. |
| `docs/AUTOTUNE_REFACTORED.md` | Autotune refactor structure. |
| `docs/FIXED_POINT_ANALYSIS.md` | **Archive** |
| `docs/FIXED_POINT_PLAN.md` | **Archive** |

---

## 8. Other / non-active

| File | Purpose |
|------|---------|
| `params.ino.backup_old_version` | Old params backup; not compiled. |
| `.gitignore` | Ignores editor/local clutter. |

---

## 9. Vendored libraries (`_build_libs/`)

| Library | Path | Used by |
|---------|------|---------|
| `ADSR_Bezier` | `_build_libs/ADSR_Bezier` (symlink → monorepo root, branch `main`) | `adsr.*` |
| `mo-lfo` | `_build_libs/mo-lfo` | `LFO.*` |
| `MIDI_Library` | `_build_libs/MIDI_Library` | `midi.*` |
| `PID_v1` | `_build_libs/PID_v1` | `PID.*` / `init_PID` |

## 10. Other external dependencies

| Library | Used by |
|---------|---------|
| Adafruit TinyUSB | USB MIDI |
| LittleFS (RP2040 core) | `FS.*` |

---

## Quick “where do I change X?”

| Goal | Start here |
|------|------------|
| Engine float/fixed | `DCO.ino` flags → `voice_task_main` |
| New ParamId | `params_def.h` → `params.ino` table (only call path) |
| Serial command | `serial_input_protocol.h` + `Serial.ino` handlers ← `serial_panel_task` (Serial2) and `serial_usb_task` (USB CDC) in `loop` |
| Control the board with no panel | [`tools/dco_control`](../tools/dco_control/README.md) over USB; needs `ENABLE_USB_CONTROL` |
| Start auto-cal | Param → `apply_param_calibration_flag` → `loop1` → `DCO_calibration` |
| Manual cal UI | `apply_param_manual_calibration_*` → `loop1` manual branch |
| MIDI notes | `loop` → MIDI `.read` → `note_on`/`note_off` → local `noteStart[]`/`noteEnd[]` (no serial note frames) |
| MIDI CC assignments | `tools/dco_control/params.py` `cc=` field → `gen_midi_map.py` → `midi_cc_map.h` + [`MIDI_CC_MAP.md`](MIDI_CC_MAP.md) |
| Play audio path | `loop1` → `ADSR_update` + `voice_task_main` |
| Measure where the time goes | `RUNNING_AVERAGE` in `DCO.ino` → probe table in `bench.h` → debug command 10 |
