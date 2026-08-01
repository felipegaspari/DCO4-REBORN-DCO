// Configure range PWM (per DCO, DIV_COUNTER) and PW PWM (per voice, DIV_COUNTER_PW). Called from setup1().
void init_pwm()
{
  for (int i = 0; i < NUM_OSCILLATORS; i++)
  {
    gpio_set_function(RANGE_PINS[i], GPIO_FUNC_PWM);
    RANGE_PWM_SLICES[i] = pwm_gpio_to_slice_num(RANGE_PINS[i]);
    pwm_set_wrap(RANGE_PWM_SLICES[i], DIV_COUNTER);
    pwm_set_enabled(RANGE_PWM_SLICES[i], true);
  }

  for (int i = 0; i < NUM_VOICES_TOTAL; i++)
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
}

// Push raw compare values to the cutoff / resonance / VCA / dist slices.
void write_cv_pwm_raw(uint16_t cutoff, uint16_t resonance, uint16_t vca,
                      uint16_t dist_drive, uint16_t dist_mix) {
  for (int i = 0; i < NUM_FILTERS; i++) {
    pwm_set_chan_level(CUTOFF_PWM_SLICES[i], CUTOFF_PWM_CHANS[i], cutoff);

    uint16_t reso_level = resonance;
    if (RESO_PWM_SLICES[i] == RANGE_PWM_SLICES[1]) {
      // Shared wrap DIV_COUNTER with RANGE OSC2 — scale 0..4095 → 0..DIV_COUNTER.
      reso_level = (uint16_t)(((uint32_t)resonance * DIV_COUNTER) / DIV_COUNTER_CV);
    }
    pwm_set_chan_level(RESO_PWM_SLICES[i], RESO_PWM_CHANS[i], reso_level);
  }
  pwm_set_chan_level(VCA_PWM_SLICE, VCA_PWM_CHAN, vca);
  pwm_set_chan_level(DIST_DRIVE_PWM_SLICE, DIST_DRIVE_PWM_CHAN, dist_drive);
  pwm_set_chan_level(DIST_MIX_PWM_SLICE, DIST_MIX_PWM_CHAN, dist_mix);
}

// Push soft VCF/VCA/reso/dist levels to Pico PWM compares.
void write_cv_pwm() {
  write_cv_pwm_raw(VCF_PWM[0], RESONANCE_PWM, VCA_PWM[0], DIST_DRIVE, DIST_MIX);
}

#endif  // ENABLE_CV_OUTS
