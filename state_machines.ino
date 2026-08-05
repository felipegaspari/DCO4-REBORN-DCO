// Which oscillator has its reset driven by another, and which one drives it.
// syncMode 1: OSC2's sideset drives OSC1's reset pin, so OSC1 is the slave.
// syncMode 2: OSC1's sideset drives OSC2's reset pin, so OSC2 is the slave.
// OSC3 is always free-running.
static int sync_slave_osc() {
  if (syncMode == 1) return 0;
  if (syncMode == 2) return 1;
  return -1;
}

static int sync_master_osc() {
  if (syncMode == 1) return 1;
  if (syncMode == 2) return 0;
  return -1;
}

// Give the slave a lower state machine index than its master. When two SMs write the
// same pin on the same cycle the higher-numbered one wins, so a master that outranks
// its slave never loses a sync edge to a tie.
void assign_sm_mapping() {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    VOICE_TO_SM[i] = i;
  }

  int slave = sync_slave_osc();
  int master = sync_master_osc();
  if (slave >= 0 && master >= 0 && slave > master) {
    VOICE_TO_SM[slave] = master;
    VOICE_TO_SM[master] = slave;
  }
}

// Load the oscillator programs into pio0 and start all voice SMs. Called from setup1().
void init_pio() {
  // Both oscillator programs stay resident in pio0 (12 + 13 of 32 instruction slots),
  // so switching an SM between hard and soft sync is a re-init rather than a reload.
  pio_offset_free = pio_add_program(pio[0], &frequency_sync_4_jumps_program);
  pio_offset_sync = pio_add_program(pio[0], &frequency_sync_poll_program);

  // pio1 carries the sub-oscillator; pio2 stays free for ENABLE_PIO_MIDI.
  subosc_offset_div2 = pio_add_program(pio[SUBOSC_PIO], &subosc_div2_program);
  subosc_offset_div4 = pio_add_program(pio[SUBOSC_PIO], &subosc_div4_program);

  assign_sm_mapping();
  start_voice_sms();
  set_subosc_divide(subOscDivide);
}

// Configure every oscillator state machine on pio0 and start them on the same cycle.
// Safe to call again whenever the sync topology changes.
void start_voice_sms() {
  int slave = sync_slave_osc();
  int master = sync_master_osc();
  bool softSync = (softSyncChunks > 0) && (slave >= 0) && (master >= 0);

  uint32_t enableMask = 0;

  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    uint8_t sm = VOICE_TO_SM[i];

    // Hard sync: the master's sideset drives the slave's reset pin as well as its own,
    // discharging the slave's integrator on the master's cycle while the slave's SM
    // keeps its own schedule. This only works because every SM is on pio0 — a GPIO's
    // function select names one block, so oscillators on separate blocks could not
    // share a pin.
    //
    // Soft sync drives nothing extra: the slave polls the master's pin instead, so the
    // master leaves its sideset on its own reset pin.
    uint8_t sidesetPin = RESET_PINS[i];
    if (!softSync && master >= 0 && i == master) {
      sidesetPin = RESET_PINS[slave];
    }

    pio_sm_set_enabled(pio[0], sm, false);
    pio_sm_clear_fifos(pio[0], sm);

    if (softSync && i == slave) {
      frequency_sync_poll_init(pio[0], sm, pio_offset_sync, RESET_PINS[i], sidesetPin,
                               RESET_PINS[master]);
      osc_uses_sync_program[i] = true;
    } else {
      frequency_sync_4_jumps(pio[0], sm, pio_offset_free, RESET_PINS[i], sidesetPin);
      osc_uses_sync_program[i] = false;
    }

    // Preload the reset pulse width into Y, then restore the divider that Y's write
    // consumed out of the OSR.
    osc_load_period_stopped(i, pioPulseLength, osc_last_clk_div[i]);

    enableMask |= (1u << sm);
  }

  // Same-cycle start. Separate pio_sm_set_enabled calls used to leave a few hundred
  // nanoseconds of skew between oscillators, which capped phase-align accuracy.
  pio_enable_sm_mask_in_sync(pio[0], enableMask);
}

// Change only the reset pulse width, keeping the divider the SM was already running.
// Stops and restarts the SM, so this is for parameter changes rather than the audio path.
void osc_set_reset_pulse(uint8_t osc, uint32_t y) {
  uint8_t sm = VOICE_TO_SM[osc];

  pio_sm_set_enabled(pio[0], sm, false);
  osc_load_period_stopped(osc, y, osc_last_clk_div[osc]);
  pio_sm_set_enabled(pio[0], sm, true);
}

// Report the sync topology and, importantly, which PIO block owns each reset pin.
//
// This is the check that catches the bug this rework fixes: a GPIO's function select can
// name only one block, so when the oscillators lived on pio0/pio1/pio2 the master's
// pio_gpio_init() silently re-pointed the slave's reset pin at the master's block and the
// slave stopped driving its own core. Every RESET pin must read back as PIO0 here.
void pio_topology_report() {
  int slave = sync_slave_osc();
  int master = sync_master_osc();

  Serial.printf("[pio topology] syncMode=%u softSyncChunks=%u slave=%d master=%d\n",
                syncMode, softSyncChunks, slave, master);

  bool allOnPio0 = true;
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    gpio_function_t fn = gpio_get_function(RESET_PINS[i]);
    bool onPio0 = (fn == GPIO_FUNC_PIO0);
    if (!onPio0) allOnPio0 = false;

    Serial.printf("  OSC%d reset=GP%-2u sm=%u program=%s funcsel=%d%s\n",
                  i + 1, RESET_PINS[i], VOICE_TO_SM[i],
                  osc_uses_sync_program[i] ? "poll" : "free",
                  (int)fn, onPio0 ? "" : "  <-- NOT PIO0");
  }

  Serial.printf("  reset pin ownership: %s\n",
                allOnPio0 ? "OK (all PIO0)" : "BROKEN (a pin was stolen by another block)");

  if (master >= 0 && slave >= 0) {
    Serial.printf("  master OSC%d is sm=%u, slave OSC%d is sm=%u -> tie-break %s\n",
                  master + 1, VOICE_TO_SM[master], slave + 1, VOICE_TO_SM[slave],
                  VOICE_TO_SM[master] > VOICE_TO_SM[slave] ? "OK (master outranks slave)"
                                                           : "WRONG (master can lose edges)");
  }
}

// Bench helper: park one oscillator at a fixed clk_div and report what the period model
// predicts, so a frequency counter on its RESET pin can be compared directly.
//
// PIO_PERIOD_OVERHEAD_FREE = 12 is derived by counting instructions (see globals.h); this
// is how to confirm it on hardware. Run it at two widely separated clk_div values and
// feed both readings to pio_solve_period_model().
void pio_period_probe(uint8_t osc, uint32_t clk_div) {
  uint8_t sm = VOICE_TO_SM[osc];

  pio_sm_set_enabled(pio[0], sm, false);
  osc_load_period_stopped(osc, pioPulseLength, clk_div);
  pio_sm_set_enabled(pio[0], sm, true);

  uint32_t w = osc_ramp_weight(osc);
  uint32_t k = osc_period_overhead(osc);
  uint32_t predicted = pioPulseLength + w * clk_div + k;

  Serial.printf("[period probe] osc=%u sm=%u pin=%u\n", osc, sm, RESET_PINS[osc]);
  Serial.printf("  Y=%lu clk_div=%lu weight=%lu overhead=%lu\n",
                (unsigned long)pioPulseLength, (unsigned long)clk_div,
                (unsigned long)w, (unsigned long)k);
  Serial.printf("  predicted period = %lu cycles = %.4f Hz\n",
                (unsigned long)predicted, (double)sysClock_Hz / (double)predicted);
}

// Solve period = Y + weight*clk_div + overhead from two frequency-counter readings taken
// with pio_period_probe() at the same Y. Prints the measured weight and overhead so they
// can be compared against PIO_RAMP_WEIGHT_* / PIO_PERIOD_OVERHEAD_*.
void pio_solve_period_model(uint32_t clk_div_a, double measured_hz_a,
                            uint32_t clk_div_b, double measured_hz_b,
                            uint32_t y) {
  if (clk_div_a == clk_div_b || measured_hz_a <= 0.0 || measured_hz_b <= 0.0) {
    Serial.println("[period solve] need two distinct clk_div values and non-zero readings");
    return;
  }

  double period_a = (double)sysClock_Hz / measured_hz_a;
  double period_b = (double)sysClock_Hz / measured_hz_b;

  double weight = (period_a - period_b) / ((double)clk_div_a - (double)clk_div_b);
  double overhead = period_a - (double)y - weight * (double)clk_div_a;

  Serial.printf("[period solve] weight = %.4f (expected %u or %u)\n",
                weight, (unsigned)PIO_RAMP_WEIGHT_FREE, (unsigned)PIO_RAMP_WEIGHT_SYNC);
  Serial.printf("[period solve] overhead = %.2f cycles (expected %u or %u)\n",
                overhead, (unsigned)PIO_PERIOD_OVERHEAD_FREE,
                (unsigned)PIO_PERIOD_OVERHEAD_SYNC);
}

// (Re)configure the sub-oscillator. divide 0 stops it, 2 and 4 give a square one and
// two octaves below OSC1. Needs SUBOSC_PIN wired to a mixer input to be audible.
void set_subosc_divide(uint8_t divide) {
  subOscDivide = divide;

  PIO p = pio[SUBOSC_PIO];
  pio_sm_set_enabled(p, 0, false);

  if (divide == 0) {
    return;
  }

  bool div4 = (divide >= 4);
  subosc_init(p, 0, div4 ? subosc_offset_div4 : subosc_offset_div2,
              RESET_PINS[0], SUBOSC_PIN, div4);
  pio_sm_set_enabled(p, 0, true);
}

// ---- Core-0 → core-1 deferred PIO requests -----------------------------------
// Serial/MIDI handlers on core 0 must not touch PIO while voice_task_main on core 1
// is driving the same state machines.

static volatile uint8_t pio_defer_pending = 0;
static volatile uint8_t pio_defer_subosc_value = 0;

static constexpr uint8_t PIO_DEFER_SYNC   = 1u << 0;
static constexpr uint8_t PIO_DEFER_RESET  = 1u << 1;
static constexpr uint8_t PIO_DEFER_SUBOSC = 1u << 2;

void setSyncMode();  // voices.ino — must run on core 1 only

void pio_defer_request_sync_mode() {
  __atomic_fetch_or(&pio_defer_pending, PIO_DEFER_SYNC, __ATOMIC_SEQ_CST);
}

void pio_defer_request_reset_pulse_all() {
  __atomic_fetch_or(&pio_defer_pending, PIO_DEFER_RESET, __ATOMIC_SEQ_CST);
}

void pio_defer_request_subosc(uint8_t divide) {
  pio_defer_subosc_value = divide;
  __dmb();
  __atomic_fetch_or(&pio_defer_pending, PIO_DEFER_SUBOSC, __ATOMIC_SEQ_CST);
}

void pio_defer_service() {
  const uint8_t pending =
      __atomic_exchange_n(&pio_defer_pending, 0, __ATOMIC_SEQ_CST);
  if (pending == 0) {
    return;
  }

  if (pending & PIO_DEFER_SYNC) {
    setSyncMode();
  }
  if (pending & PIO_DEFER_RESET) {
    for (int i = 0; i < NUM_OSCILLATORS; i++) {
      osc_set_reset_pulse(i, pioPulseLength);
    }
    for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
      note_on_flag[i] = 1;
    }
  }
  if (pending & PIO_DEFER_SUBOSC) {
    set_subosc_divide(pio_defer_subosc_value);
  }
}
