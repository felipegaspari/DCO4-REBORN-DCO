// Configure range PWM (per DCO, DIV_COUNTER) and PW PWM (per voice, DIV_COUNTER_PW). Called from setup1().
void init_pwm()
{
  for (int i = 0; i < NUM_OSCILLATORS; i++)
  {
    gpio_set_function(RANGE_PINS[i], GPIO_FUNC_PWM);
    RANGE_PWM_SLICES[i] = pwm_gpio_to_slice_num(RANGE_PINS[i]);
    RANGE_PWM_CHANNELS[i] = pwm_gpio_to_channel(RANGE_PINS[i]);
    pwm_set_wrap(RANGE_PWM_SLICES[i], DIV_COUNTER);
    pwm_set_enabled(RANGE_PWM_SLICES[i], true);
  }

  for (int i = 0; i < NUM_OSCILLATORS; i++)
  {
    gpio_set_function(PW_PINS[i], GPIO_FUNC_PWM);
    PW_PWM_SLICES[i] = pwm_gpio_to_slice_num(PW_PINS[i]);
    pwm_set_wrap(PW_PWM_SLICES[i], DIV_COUNTER_PW);
    pwm_set_enabled(PW_PWM_SLICES[i], true);
  }

#ifdef ENABLE_CV_OUTS
  init_cv_pwm();
#endif
}

#ifdef ENABLE_CV_OUTS

// Init cutoff / resonance / VCA PWM (12-bit CV domain). Reso1 shares slice with RANGE OSC2.
void init_cv_pwm() {
  for (int i = 0; i < NUM_FILTERS; i++) {
    gpio_set_function(CUTOFF_PINS[i], GPIO_FUNC_PWM);
    CUTOFF_PWM_SLICES[i] = pwm_gpio_to_slice_num(CUTOFF_PINS[i]);
    CUTOFF_PWM_CHANS[i] = pwm_gpio_to_channel(CUTOFF_PINS[i]);
    // Cut1+Reso0 share slice 2 — both CV, wrap 4095.
    pwm_set_wrap(CUTOFF_PWM_SLICES[i], DIV_COUNTER_CV);
    pwm_set_enabled(CUTOFF_PWM_SLICES[i], true);

    gpio_set_function(RESO_PINS[i], GPIO_FUNC_PWM);
    RESO_PWM_SLICES[i] = pwm_gpio_to_slice_num(RESO_PINS[i]);
    RESO_PWM_CHANS[i] = pwm_gpio_to_channel(RESO_PINS[i]);
    // Reso1 (GP7) shares slice 3 with RANGE OSC2 (GP22): keep DIV_COUNTER wrap from init_pwm.
    if (RESO_PWM_SLICES[i] != RANGE_PWM_SLICES[1]) {
      pwm_set_wrap(RESO_PWM_SLICES[i], DIV_COUNTER_CV);
    }
    pwm_set_enabled(RESO_PWM_SLICES[i], true);
  }

  gpio_set_function(VCA_PIN, GPIO_FUNC_PWM);
  VCA_PWM_SLICE = pwm_gpio_to_slice_num(VCA_PIN);
  VCA_PWM_CHAN = pwm_gpio_to_channel(VCA_PIN);
  pwm_set_wrap(VCA_PWM_SLICE, DIV_COUNTER_CV);
  pwm_set_enabled(VCA_PWM_SLICE, true);

  // Dist Mix shares slice 5 with VCA (wrap already 4095). Drive is alone on slice 4 B.
  // Dual-MCU (ENABLE_VOICE_AUX): RP2040 owns these pins — do not claim them here.
#ifndef ENABLE_VOICE_AUX
  gpio_set_function(DIST_DRIVE_PIN, GPIO_FUNC_PWM);
  DIST_DRIVE_PWM_SLICE = pwm_gpio_to_slice_num(DIST_DRIVE_PIN);
  DIST_DRIVE_PWM_CHAN = pwm_gpio_to_channel(DIST_DRIVE_PIN);
  pwm_set_wrap(DIST_DRIVE_PWM_SLICE, DIV_COUNTER_CV);
  pwm_set_enabled(DIST_DRIVE_PWM_SLICE, true);

  gpio_set_function(DIST_MIX_PIN, GPIO_FUNC_PWM);
  DIST_MIX_PWM_SLICE = pwm_gpio_to_slice_num(DIST_MIX_PIN);
  DIST_MIX_PWM_CHAN = pwm_gpio_to_channel(DIST_MIX_PIN);
  if (DIST_MIX_PWM_SLICE != VCA_PWM_SLICE) {
    pwm_set_wrap(DIST_MIX_PWM_SLICE, DIV_COUNTER_CV);
  }
  pwm_set_enabled(DIST_MIX_PWM_SLICE, true);
#endif

  init_level_pwm();
}

// True if this slice already owns a RANGE or PW wrap that must not become 4095.
static bool level_pwm_slice_shares_voice_wrap(uint8_t slice) {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    if (slice == RANGE_PWM_SLICES[i]) return true;
  }
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    if (slice == PW_PWM_SLICES[i]) return true;
  }
  return false;
}

static void init_one_level_pwm(uint8_t pin, uint8_t& slice, uint8_t& chan) {
  gpio_set_function(pin, GPIO_FUNC_PWM);
  slice = pwm_gpio_to_slice_num(pin);
  chan = pwm_gpio_to_channel(pin);
  // OSC1 level (GP16) shares slice 0 with RANGE OSC3; OSC2 (GP18) shares slice 1 with PW.
  if (!level_pwm_slice_shares_voice_wrap(slice)) {
    pwm_set_wrap(slice, DIV_COUNTER_CV);
  }
  pwm_set_enabled(slice, true);
}

static uint16_t scale_level_cv_to_wrap(uint16_t level_cv, uint8_t slice) {
  // Shared-slice paths keep voice wrap; scale 0..4095 into that domain.
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    if (slice == RANGE_PWM_SLICES[i]) {
      return (uint16_t)(((uint32_t)level_cv * DIV_COUNTER) / DIV_COUNTER_CV);
    }
  }
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    if (slice == PW_PWM_SLICES[i]) {
      return (uint16_t)(((uint32_t)level_cv * DIV_COUNTER_PW) / DIV_COUNTER_CV);
    }
  }
  return level_cv;
}

void init_level_pwm() {
  init_one_level_pwm(OSC1_LEVEL_PIN, OSC1_LEVEL_PWM_SLICE, OSC1_LEVEL_PWM_CHAN);
  init_one_level_pwm(OSC2_LEVEL_PIN, OSC2_LEVEL_PWM_SLICE, OSC2_LEVEL_PWM_CHAN);
  init_one_level_pwm(OSC3_LEVEL_PIN, OSC3_LEVEL_PWM_SLICE, OSC3_LEVEL_PWM_CHAN);
  init_one_level_pwm(SUB_LEVEL_PIN, SUB_LEVEL_PWM_SLICE, SUB_LEVEL_PWM_CHAN);
  write_level_pwm();
}

void write_level_pwm_raw(uint16_t osc1, uint16_t osc2, uint16_t osc3, uint16_t sub) {
  pwm_set_chan_level(OSC1_LEVEL_PWM_SLICE, OSC1_LEVEL_PWM_CHAN,
                     scale_level_cv_to_wrap(osc1, OSC1_LEVEL_PWM_SLICE));
  pwm_set_chan_level(OSC2_LEVEL_PWM_SLICE, OSC2_LEVEL_PWM_CHAN,
                     scale_level_cv_to_wrap(osc2, OSC2_LEVEL_PWM_SLICE));
  pwm_set_chan_level(OSC3_LEVEL_PWM_SLICE, OSC3_LEVEL_PWM_CHAN,
                     scale_level_cv_to_wrap(osc3, OSC3_LEVEL_PWM_SLICE));
  pwm_set_chan_level(SUB_LEVEL_PWM_SLICE, SUB_LEVEL_PWM_CHAN,
                     scale_level_cv_to_wrap(sub, SUB_LEVEL_PWM_SLICE));
}

void write_level_pwm() {
  write_level_pwm_raw(OSC1Level, OSC2Level, OSC3Level, SubLevel);
}

// Push raw compare values to the cutoff / per-filter resonance / VCA / dist slices.
void write_cv_pwm_raw(uint16_t cutoff, const uint16_t resonance[NUM_FILTERS], uint16_t vca,
                      uint16_t dist_drive, uint16_t dist_mix) {
  for (int i = 0; i < NUM_FILTERS; i++) {
    pwm_set_chan_level(CUTOFF_PWM_SLICES[i], CUTOFF_PWM_CHANS[i], cutoff);

    uint16_t reso_level = resonance[i];
    if (RESO_PWM_SLICES[i] == RANGE_PWM_SLICES[1]) {
      // Shared wrap DIV_COUNTER with RANGE OSC2 — scale 0..4095 → 0..DIV_COUNTER.
      reso_level = (uint16_t)(((uint32_t)resonance[i] * DIV_COUNTER) / DIV_COUNTER_CV);
    }
    pwm_set_chan_level(RESO_PWM_SLICES[i], RESO_PWM_CHANS[i], reso_level);
  }
  pwm_set_chan_level(VCA_PWM_SLICE, VCA_PWM_CHAN, vca);
#ifndef ENABLE_VOICE_AUX
  pwm_set_chan_level(DIST_DRIVE_PWM_SLICE, DIST_DRIVE_PWM_CHAN, dist_drive);
  pwm_set_chan_level(DIST_MIX_PWM_SLICE, DIST_MIX_PWM_CHAN, dist_mix);
#else
  (void)dist_drive;
  (void)dist_mix;
#endif
}

// Push soft VCF/VCA/reso/dist levels to Pico PWM compares (panel Dist Drive; matrix may override).
void write_cv_pwm() {
  write_cv_pwm_raw(VCF_PWM[0], RESONANCE_PWM, VCA_PWM[0], DIST_DRIVE, DIST_MIX);
}

#endif  // ENABLE_CV_OUTS
