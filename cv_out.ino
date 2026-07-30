// Phase 2: Mainboard setPWMOuts math → software VCA/VCF/reso levels (no HW PWM yet).
#include "include_all.h"

// LFO full-scale the Mainboard depth formulas were tuned against. The DCO's own LFOs run
// at smaller amplitudes (LFO2_CC tracks DIV_COUNTER_PW for the PW path) and with the
// opposite sign, so the CV path normalises here rather than touching the shared levels.
static constexpr float CV_LFO_REF_CC = 4095.0f;
static constexpr float CV_DRIFT_REF_CC = 1000.0f;

static inline uint16_t cv_lerp_u16(uint16_t value, uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1) {
  if (x1 == x0) return y0;
  return (uint16_t)(y0 + ((int32_t)(y1 - y0) * (int32_t)(value - x0)) / (int32_t)(x1 - x0));
}

// Boot: build AS2164 VCA linearize table (same control points as Mainboard).
void init_cv_out() {
  generateBezierArray({ 0, 4095 }, { 4095, 0 }, { 150, 1420 }, { -235, 815 }, 4096, AS2164_VCA_linearize_table);
  cv_update_mod_formulas();
}

// Recompute LFO1→VCA / EnvVCF→VCF / LFO2→VCF depth scalars.
// The LFO scalars carry the negative sign that restores the Mainboard's LFO polarity.
void cv_update_mod_formulas() {
  ADSR2toVCF_formula = (1.0f / 512.0f) * (float)ADSR2toVCF;
  LFO2toVCF_formula = -(1.0f / 512.0f) * (float)LFO2toVCF * (CV_LFO_REF_CC / (float)LFO2_CC);
  LFO1toVCA_formula = -(1.0f / 512.0f) * (float)LFO1toVCA * (CV_LFO_REF_CC / (float)LFO1_CC);
}

// Hot path (~10 kHz with ADSR): EnvVCA/EnvVCF + LFO + keytrack/vel → soft CV levels.
void update_CV_outs() {
  static constexpr int DEFAULT_VCA_COMPENSATION = 100;

  if (timer1msFlag) {
    if (RESONANCEAmpCompensation) {
      static constexpr int MAX_RESONANCE = 2300;
      static constexpr int MIN_RESONANCE = 50;
      static constexpr int MAX_VCA_COMPENSATION = 315;
      static constexpr float COMPENSATION_FACTOR = 0.14f;
      // Compensation hits 0 exactly at MAX_RESONANCE; past it the value would go
      // negative and wrap when cast unsigned for the lerp below.
      const int resonance_in = min((int)RESONANCE, MAX_RESONANCE);
      VCAResonanceCompensation =
        (resonance_in >= MIN_RESONANCE)
          ? (int16_t)(MAX_VCA_COMPENSATION - ((resonance_in - MIN_RESONANCE) * COMPENSATION_FACTOR))
          : (int16_t)MAX_VCA_COMPENSATION;
    } else {
      VCAResonanceCompensation = DEFAULT_VCA_COMPENSATION;
    }

    if (VCFKeytrack != 0) {
      for (byte i = 0; i < NUM_VOICES_TOTAL; i++) {
        VCFKeytrackPerVoice[i] =
          1.00f + (float)(VCFKeytrackModifier * map(VOICE_NOTES[i], 0, 150, -60, 90));
      }
    } else {
      for (byte i = 0; i < NUM_VOICES_TOTAL; i++) {
        VCFKeytrackPerVoice[i] = 1.0f;
      }
    }

    if (analogDrift != 0) {
      for (byte i = 0; i < NUM_VOICES_TOTAL; i++) {
        // Monosynth: use osc-0 drift LFO (Mainboard used per-voice drift).
        VCF_DRIFT[i] = (float)LFO_DRIFT_LEVEL[0] *
                       (0.002f * (CV_DRIFT_REF_CC / (float)LFO_DRIFT_CC)) * (float)analogDrift;
      }
    } else {
      for (byte i = 0; i < NUM_VOICES_TOTAL; i++) {
        VCF_DRIFT[i] = 0.0f;
      }
    }
  }

  const int16_t LFO1toVCA_calc = (int16_t)((float)LFO1Level * LFO1toVCA_formula);
  const float LFO2toVCF_mod = (float)LFO2Level * LFO2toVCF_formula;

  for (byte i = 0; i < NUM_VOICES_TOTAL; i++) {
    float VCA_velocityFactor = 1.0f;
    if (velocityToVCAVal != 0) {
      VCA_velocityFactor = 1.0f - ((float)velocityToVCA * (127 - midi_velocity[i]));
    }

    int16_t LFO1toVCA_current = (ADSR_VCA_Level[i] == 0) ? 0 : LFO1toVCA_calc;
    uint16_t VCA_Calculated =
      (uint16_t)constrain((float)(ADSR_VCA_Level[i] + LFO1toVCA_current) * VCA_velocityFactor, 0, 4095);
    VCA_PWM[i] = cv_lerp_u16(AS2164_VCA_linearize_table[VCA_Calculated], 0, 4095,
                             (uint16_t)VCAResonanceCompensation, (uint16_t)(4095 - VCALevel));

    float VCF_velocityFactor = 1.0f;
    if (velocityToVCFVal != 0) {
      VCF_velocityFactor = 1.0f - ((float)velocityToVCF * (127 - midi_velocity[i]));
    }
    float ADSR2toVCFcalculated = (float)ADSR_VCF_Level[i] * ADSR2toVCF_formula;
    float combinedValue = ADSR2toVCFcalculated + LFO2toVCF_mod + (float)CUTOFF + VCF_DRIFT[i];
    float finalValue = combinedValue * VCF_velocityFactor * VCFKeytrackPerVoice[i];
    VCF_PWM[i] = (uint16_t)(4095 - (int)constrain(finalValue, 0, 4095));
  }

  RESONANCE_PWM = RESONANCE;
#ifdef ENABLE_CV_OUTS
  write_cv_pwm();
#endif
}

// Manual calibration (Mainboard setPWMOutsManualCalibration): filter wide open, VCA barely
// cracked, and only the oscillator being calibrated audible. Runs instead of update_CV_outs.
void update_CV_outs_manual_calibration() {
  static constexpr uint16_t CAL_CUTOFF_COMPARE = 0;
  static constexpr uint16_t CAL_RESONANCE_COMPARE = 0;
  static constexpr uint16_t CAL_VCA_COMPARE = 150;
  static constexpr uint16_t CAL_SQR_ON = 50;
  static constexpr uint16_t CAL_SQR_MUTED = 4095;

  byte stage = (byte)manualCalibrationStage;
  if (stage > 2) stage = 2;

  waveSelector_manual_calibration(stage);

  // OSC3 has no SQR DAC channel — keep both muted on that stage.
  SQR1Level = (stage == 0) ? CAL_SQR_ON : CAL_SQR_MUTED;
  SQR2Level = (stage == 1) ? CAL_SQR_ON : CAL_SQR_MUTED;
  mcpUpdate();

#ifdef ENABLE_CV_OUTS
  write_cv_pwm_raw(CAL_CUTOFF_COMPARE, CAL_RESONANCE_COMPARE, CAL_VCA_COMPARE);
#endif
}
