// Panel sustain (MIDI/CC domain) → library setSustain units.
// NATIVE_Q15=1: 0..ADSR_Q15_PEAK. NATIVE_Q15=0: DAC counts (panel_full == peak).
static inline int adsr_sustain_for_set(uint16_t panel, uint16_t panel_full) {
#if ADSR_BEZIER_NATIVE_Q15
  if (panel_full == 0) return 0;
  // CV panel scale 4096 → (panel * ADSR_Q15_PEAK) >> 12 (no divide).
  if (panel_full == ADSR_CV_SCALE)
    return (int)(((uint32_t)panel * (uint32_t)ADSR_Q15_PEAK) >> 12);
  return (int)(((uint32_t)panel * (uint32_t)ADSR_Q15_PEAK) / (uint32_t)panel_full);
#else
  (void)panel_full;
  return (int)panel;
#endif
}

// Boot: build Bézier tables and apply initial A/D/S/R to EnvDCO + EnvVCA + EnvVCF.
void init_ADSR() {
#if ADSR_BEZIER_NATIVE_Q15
  adsrBezierInitTables((float)ADSR_Q15_PEAK, ARRAY_SIZE, _curve_tables);
#else
  adsrBezierInitTables(ADSR_1_CC, ARRAY_SIZE, _curve_tables);
#endif

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr3_voice.setAttack(ADSR3_attack);
    ADSRVoices[i].adsr3_voice.setDecay(ADSR3_decay);
    ADSRVoices[i].adsr3_voice.setSustain(adsr_sustain_for_set(ADSR3_sustain, ADSR_CV_SCALE));
    ADSRVoices[i].adsr3_voice.setRelease(ADSR3_release);
    ADSRVoices[i].adsr3_voice.setResetAttack(ADSRRestart);

    ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
    ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
    ADSRVoices[i].adsr_vca_voice.setSustain(adsr_sustain_for_set(ADSR_VCA_sustain, ADSR_CV_SCALE));
    ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
    ADSRVoices[i].adsr_vca_voice.setResetAttack(VCAADSRRestart);
  }

  adsr_vcf_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf_voice.setSustain(adsr_sustain_for_set(ADSR_VCF_sustain, ADSR_CV_SCALE));
  adsr_vcf_voice.setRelease(ADSR_VCF_release);
  adsr_vcf_voice.setResetAttack(VCFADSRRestart);

  adsr_vcf2_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf2_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf2_voice.setSustain(adsr_sustain_for_set(ADSR_VCF_sustain, ADSR_CV_SCALE));
  adsr_vcf2_voice.setRelease(ADSR_VCF_release);
  adsr_vcf2_voice.setResetAttack(VCFADSRRestart);
}

// ~10 kHz: note edges → EnvDCO + EnvVCA + EnvVCF; sample levels.
// Each noteOn/noteOff/getWave reads micros()/millis() itself — do not share one
// timestamp across edges + getWave (unsigned delta underflow skips A/R).
void __not_in_flash_func(ADSR_update)() {
  #ifdef ENABLE_MB_MOD_STREAM
    for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
      noteStart[i] = 0;
      noteEnd[i] = 0;
    }
    return;
  #endif
  
    // Static phase toggle: flips between 0 and 1 on every function call
    static uint8_t phase = 0;
    phase ^= 1; // 0 -> 1 -> 0 -> 1...
  
    // Loop starts at 'phase' (0 or 1) and jumps by 2
    // Pass 0: processes voices 0, 2, ...
    // Pass 1: processes voices 1, 3, ...
    for (int i = phase; i < NUM_VOICES; i += 2) {
      if (noteEnd[i] == 1) {
        ADSRVoices[i].adsr3_voice.noteOff();
        ADSRVoices[i].adsr_vca_voice.noteOff();
        adsr_vcf_voice.noteOff();
        adsr_vcf2_voice.noteOff();
        noteEnd[i] = 0;
      } else if (noteStart[i] == 1) {
        ADSRVoices[i].adsr3_voice.noteOn();
        ADSRVoices[i].adsr_vca_voice.noteOn();
        adsr_vcf_voice.noteOn();
        adsr_vcf2_voice.noteOn();
  
        noteStart[i] = 0;
      }
  #if ADSR_BEZIER_NATIVE_Q15
      ADSR3Level_q15_volatile[i] = (int16_t)ADSRVoices[i].adsr3_voice.getWave();
      ADSR_VCA_Level_q15_volatile[i] = (int16_t)ADSRVoices[i].adsr_vca_voice.getWave();
  #else
      ADSR3Level[i] = ADSRVoices[i].adsr3_voice.getWave();
      ADSR3Level_q15_volatile[i] = ADSRVoices[i].adsr3_voice.levelQ15();
      ADSR_VCA_Level[i] = ADSRVoices[i].adsr_vca_voice.getWave();
      ADSR_VCA_Level_q15_volatile[i] = ADSRVoices[i].adsr_vca_voice.levelQ15();
  #endif
    }
  
    // Shared EnvVCF / EnvVCF2 stay outside the loop (runs every tick)
  #if ADSR_BEZIER_NATIVE_Q15
    ADSR_VCF_Level_q15_volatile = (int16_t)adsr_vcf_voice.getWave();
    ADSR_VCF2_Level_q15_volatile = (int16_t)adsr_vcf2_voice.getWave();
  #else
    ADSR_VCF_Level = adsr_vcf_voice.getWave();
    ADSR_VCF_Level_q15_volatile = adsr_vcf_voice.levelQ15();
    ADSR_VCF2_Level = adsr_vcf2_voice.getWave();
    ADSR_VCF2_Level_q15_volatile = adsr_vcf2_voice.levelQ15();
  #endif
  }

// ~200 Hz: push dirty EnvDCO / EnvVCA / EnvVCF A/D/S/R to all voices.
inline void ADSR_set_parameters() {
  static uint8_t tick = 0;
  if (++tick < 50) return;
  tick = 0;

  uint16_t ch = adsr_params_dirty;
  if (!ch) return;
  adsr_params_dirty = 0;

  if (ch & ADSR_DIRTY_DCO_A) {
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr3_voice.setAttack(ADSR3_attack);
  }
  if (ch & ADSR_DIRTY_DCO_D) {
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr3_voice.setDecay(ADSR3_decay);
  }
  if (ch & ADSR_DIRTY_DCO_S) {
    const int s = adsr_sustain_for_set(ADSR3_sustain, ADSR_CV_SCALE);
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr3_voice.setSustain(s);
  }
  if (ch & ADSR_DIRTY_DCO_R) {
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr3_voice.setRelease(ADSR3_release);
  }

  if (ch & ADSR_DIRTY_VCA_A) {
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
  }
  if (ch & ADSR_DIRTY_VCA_D) {
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
  }
  if (ch & ADSR_DIRTY_VCA_S) {
    const int s = adsr_sustain_for_set(ADSR_VCA_sustain, ADSR_CV_SCALE);
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr_vca_voice.setSustain(s);
  }
  if (ch & ADSR_DIRTY_VCA_R) {
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
  }

  if (ch & ADSR_DIRTY_VCF_A) {
    adsr_vcf_voice.setAttack(ADSR_VCF_attack);
    adsr_vcf2_voice.setAttack(ADSR_VCF_attack);
  }
  if (ch & ADSR_DIRTY_VCF_D) {
    adsr_vcf_voice.setDecay(ADSR_VCF_decay);
    adsr_vcf2_voice.setDecay(ADSR_VCF_decay);
  }
  if (ch & ADSR_DIRTY_VCF_S) {
    const int s = adsr_sustain_for_set(ADSR_VCF_sustain, ADSR_CV_SCALE);
    adsr_vcf_voice.setSustain(s);
    adsr_vcf2_voice.setSustain(s);
  }
  if (ch & ADSR_DIRTY_VCF_R) {
    adsr_vcf_voice.setRelease(ADSR_VCF_release);
    adsr_vcf2_voice.setRelease(ADSR_VCF_release);
  }
}

void ADSR3_set_restart() {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr3_voice.setResetAttack(ADSRRestart);
  }
}

void ADSR_VCA_set_restart() {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr_vca_voice.setResetAttack(VCAADSRRestart);
  }
}

void ADSR_VCF_set_restart() {
  adsr_vcf_voice.setResetAttack(VCFADSRRestart);
}

void ADSR_VCF2_set_restart() {
  adsr_vcf2_voice.setResetAttack(VCFADSRRestart);
}

// EnvVCA attack curve → engine; timing params must be re-applied after a curve change.
void ADSR_VCA_change_attack_curve(uint8_t adsrCurveAttack) {
  const int s = adsr_sustain_for_set(ADSR_VCA_sustain, ADSR_CV_SCALE);
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr_vca_voice.adsrCurveAttack(adsrCurveAttack);
    ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
    ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
    ADSRVoices[i].adsr_vca_voice.setSustain(s);
    ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
    ADSRVoices[i].adsr_vca_voice.setResetAttack(VCAADSRRestart);
  }
}

// EnvVCA decay curve → engine.
void ADSR_VCA_change_decay_curve(uint8_t adsrCurveDecay) {
  const int s = adsr_sustain_for_set(ADSR_VCA_sustain, ADSR_CV_SCALE);
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr_vca_voice.adsrCurveDecay(adsrCurveDecay);
    ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
    ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
    ADSRVoices[i].adsr_vca_voice.setSustain(s);
    ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
    ADSRVoices[i].adsr_vca_voice.setResetAttack(VCAADSRRestart);
  }
}

// EnvVCF attack curve → engine.
void ADSR_VCF_change_attack_curve(uint8_t adsrCurveAttack) {
  const int s = adsr_sustain_for_set(ADSR_VCF_sustain, ADSR_CV_SCALE);
  adsr_vcf_voice.adsrCurveAttack(adsrCurveAttack);
  adsr_vcf_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf_voice.setSustain(s);
  adsr_vcf_voice.setRelease(ADSR_VCF_release);
  adsr_vcf_voice.setResetAttack(VCFADSRRestart);

  adsr_vcf2_voice.adsrCurveAttack(adsrCurveAttack);
  adsr_vcf2_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf2_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf2_voice.setSustain(s);
  adsr_vcf2_voice.setRelease(ADSR_VCF_release);
  adsr_vcf2_voice.setResetAttack(VCFADSRRestart);
}

// EnvVCF attack curve → engine.
void ADSR_VCF2_change_attack_curve(uint8_t adsrCurveAttack) {
  const int s = adsr_sustain_for_set(ADSR_VCF2_sustain, ADSR_CV_SCALE);
  adsr_vcf2_voice.adsrCurveAttack(adsrCurveAttack);
  adsr_vcf2_voice.setAttack(ADSR_VCF2_attack);
  adsr_vcf2_voice.setDecay(ADSR_VCF2_decay);
  adsr_vcf2_voice.setSustain(s);
  adsr_vcf2_voice.setRelease(ADSR_VCF2_release);
  adsr_vcf2_voice.setResetAttack(VCFADSRRestart);
}

// EnvVCF decay curve → engine.
void ADSR_VCF_change_decay_curve(uint8_t adsrCurveDecay) {
  const int s = adsr_sustain_for_set(ADSR_VCF_sustain, ADSR_CV_SCALE);
  adsr_vcf_voice.adsrCurveDecay(adsrCurveDecay);
  adsr_vcf_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf_voice.setSustain(s);
  adsr_vcf_voice.setRelease(ADSR_VCF_release);
  adsr_vcf_voice.setResetAttack(VCFADSRRestart);
}

// EnvVCF decay curve → engine.
void ADSR_VCF2_change_decay_curve(uint8_t adsrCurveDecay) {
  const int s = adsr_sustain_for_set(ADSR_VCF2_sustain, ADSR_CV_SCALE);
  adsr_vcf2_voice.adsrCurveDecay(adsrCurveDecay);
  adsr_vcf2_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf2_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf2_voice.setSustain(s);
  adsr_vcf2_voice.setRelease(ADSR_VCF_release);
  adsr_vcf2_voice.setResetAttack(VCFADSRRestart);
}

void ADSR3_change_curves( uint8_t adsrCurveAttack, uint8_t adsrCurveDecay,uint8_t adsrCurveRelease) {
  const int s = adsr_sustain_for_set(ADSR3_sustain, ADSR_CV_SCALE);
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr3_voice.adsrCurveAttack(adsrCurveAttack);
    ADSRVoices[i].adsr3_voice.adsrCurveDecay(adsrCurveDecay);
    ADSRVoices[i].adsr3_voice.adsrCurveRelease(adsrCurveRelease);
    ADSRVoices[i].adsr3_voice.setAttack(ADSR3_attack);
    ADSRVoices[i].adsr3_voice.setDecay(ADSR3_decay);
    ADSRVoices[i].adsr3_voice.setSustain(s);
    ADSRVoices[i].adsr3_voice.setRelease(ADSR3_release);
    ADSRVoices[i].adsr3_voice.setResetAttack(ADSRRestart);
  }
}