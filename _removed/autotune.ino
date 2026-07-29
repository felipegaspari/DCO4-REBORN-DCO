
// --- reset_even_pw_to_center ---

// Helper: set PW for all even-indexed voices to the center value.
static void reset_even_pw_to_center() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    PW[i] = DIV_COUNTER_PW / 2;
    pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), PW[i]);
  }
}

// --- reset_even_pw_to_0 ---

// Helper: set all voice PW PWM channels to 0. Currently unused.
static void reset_even_pw_to_0() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), 0);
  }
}

// --- reset_even_pw_to_mid_point ---

// Helper: set all voice PW PWM to mid wrap. Currently unused.
static void reset_even_pw_to_mid_point() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), DIV_COUNTER_PW / 2);
  }
}

// --- lastDCO assign sites ---
  lastDCODifference = 50000;
  lastGapFlipCount = 0;
  lastPIDgap = 50000;
  lastampCompCalibrationVal = 0;
  lastDCODifference = 50000;
  lastGapFlipCount = 0;
  lastPIDgap = 50000;
  lastampCompCalibrationVal = 0;
  lastDCODifference = 50000;
  lastGapFlipCount = 0;
  lastPIDgap = 50000;
  lastampCompCalibrationVal = 0;
