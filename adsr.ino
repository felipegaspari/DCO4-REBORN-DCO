// Boot: build Bézier/log tables and apply initial A/D/S/R to EnvDCO + EnvVCA + EnvVCF.
void init_ADSR() {
  adsrBezierInitTables(ADSR_1_CC, ARRAY_SIZE, _curve_tables);

  for (int i = 0; i < LIN_TO_EXP_TABLE_SIZE; i++) {
    linToLogLookup[i] = linearToLogarithmic(i, 10, maxADSRControlValue);
  }

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr1_voice.setAttack(ADSR1_attack);
    ADSRVoices[i].adsr1_voice.setDecay(ADSR1_decay);
    ADSRVoices[i].adsr1_voice.setSustain(ADSR1_sustain);
    ADSRVoices[i].adsr1_voice.setRelease(ADSR1_release);
    ADSRVoices[i].adsr1_voice.setResetAttack(ADSRRestart);

    ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
    ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
    ADSRVoices[i].adsr_vca_voice.setSustain(ADSR_VCA_sustain);
    ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
    ADSRVoices[i].adsr_vca_voice.setResetAttack(VCAADSRRestart);
  }

  adsr_vcf_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf_voice.setSustain(ADSR_VCF_sustain);
  adsr_vcf_voice.setRelease(ADSR_VCF_release);
  adsr_vcf_voice.setResetAttack(VCFADSRRestart);

  adsr_vcf2_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf2_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf2_voice.setSustain(ADSR_VCF_sustain);
  adsr_vcf2_voice.setRelease(ADSR_VCF_release);
  adsr_vcf2_voice.setResetAttack(VCFADSRRestart);
}

// ~10 kHz: note edges → EnvDCO + EnvVCA + EnvVCF; sample levels.
inline void ADSR_update() {
  for (int i = 0; i < NUM_VOICES; i++) {
    if (noteEnd[i] == 1) {
      ADSRVoices[i].adsr1_voice.noteOff();
      ADSRVoices[i].adsr_vca_voice.noteOff();
      adsr_vcf_voice.noteOff();
      adsr_vcf2_voice.noteOff();
      noteEnd[i] = 0;
    } else if (noteStart[i] == 1) {
      // A/D/R (and sustain) stay current via ADSR_set_parameters / init / curve helpers.
      // Re-set* here used to cost ~9x 64-bit divides per note and spiked ADSR_update max.
      ADSRVoices[i].adsr1_voice.noteOff();
      ADSRVoices[i].adsr1_voice.noteOn();

      ADSRVoices[i].adsr_vca_voice.noteOff();
      ADSRVoices[i].adsr_vca_voice.noteOn();

      adsr_vcf_voice.noteOff();
      adsr_vcf_voice.noteOn();
      adsr_vcf2_voice.noteOff();
      adsr_vcf2_voice.noteOn();

      noteStart[i] = 0;
    }
    ADSR1Level[i] = ADSRVoices[i].adsr1_voice.getWave();
    ADSR1Level_q15[i] = ADSRVoices[i].adsr1_voice.levelQ15();
    ADSR_VCA_Level[i] = ADSRVoices[i].adsr_vca_voice.getWave();
    ADSR_VCA_Level_q15[i] = ADSRVoices[i].adsr_vca_voice.levelQ15();
    ADSR_VCF_Level = adsr_vcf_voice.getWave();
    ADSR_VCF_Level_q15 = adsr_vcf_voice.levelQ15();
    ADSR_VCF2_Level = adsr_vcf2_voice.getWave();
    ADSR_VCF2_Level_q15 = adsr_vcf2_voice.levelQ15();
  }
  ADSR_set_parameters();
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
      ADSRVoices[i].adsr1_voice.setAttack(ADSR1_attack);
  }
  if (ch & ADSR_DIRTY_DCO_D) {
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr1_voice.setDecay(ADSR1_decay);
  }
  if (ch & ADSR_DIRTY_DCO_S) {
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr1_voice.setSustain(ADSR1_sustain);
  }
  if (ch & ADSR_DIRTY_DCO_R) {
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr1_voice.setRelease(ADSR1_release);
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
    for (int i = 0; i < NUM_VOICES; i++)
      ADSRVoices[i].adsr_vca_voice.setSustain(ADSR_VCA_sustain);
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
    adsr_vcf_voice.setSustain(ADSR_VCF_sustain);
    adsr_vcf2_voice.setSustain(ADSR_VCF_sustain);
  }
  if (ch & ADSR_DIRTY_VCF_R) {
    adsr_vcf_voice.setRelease(ADSR_VCF_release);
    adsr_vcf2_voice.setRelease(ADSR_VCF_release);
  }
}

void ADSR1_set_restart() {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr1_voice.setResetAttack(ADSRRestart);
  }
}

void ADSR_VCA_set_restart() {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr_vca_voice.setResetAttack(VCAADSRRestart);
  }
}

void ADSR_VCF_set_restart() {
  adsr_vcf_voice.setResetAttack(VCFADSRRestart);
  adsr_vcf2_voice.setResetAttack(VCFADSRRestart);
}

// EnvVCA attack curve → engine; timing params must be re-applied after a curve change.
void ADSR_VCA_change_attack_curve(uint8_t adsrCurveAttack) {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr_vca_voice.adsrCurveAttack(adsrCurveAttack);
    ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
    ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
    ADSRVoices[i].adsr_vca_voice.setSustain(ADSR_VCA_sustain);
    ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
    ADSRVoices[i].adsr_vca_voice.setResetAttack(VCAADSRRestart);
  }
}

// EnvVCA decay curve → engine.
void ADSR_VCA_change_decay_curve(uint8_t adsrCurveDecay) {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr_vca_voice.adsrCurveDecay(adsrCurveDecay);
    ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
    ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
    ADSRVoices[i].adsr_vca_voice.setSustain(ADSR_VCA_sustain);
    ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
    ADSRVoices[i].adsr_vca_voice.setResetAttack(VCAADSRRestart);
  }
}

// EnvVCF attack curve → engine.
void ADSR_VCF_change_attack_curve(uint8_t adsrCurveAttack) {
  adsr_vcf_voice.adsrCurveAttack(adsrCurveAttack);
  adsr_vcf_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf_voice.setSustain(ADSR_VCF_sustain);
  adsr_vcf_voice.setRelease(ADSR_VCF_release);
  adsr_vcf_voice.setResetAttack(VCFADSRRestart);

  adsr_vcf2_voice.adsrCurveAttack(adsrCurveAttack);
  adsr_vcf2_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf2_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf2_voice.setSustain(ADSR_VCF_sustain);
  adsr_vcf2_voice.setRelease(ADSR_VCF_release);
  adsr_vcf2_voice.setResetAttack(VCFADSRRestart);
}

// EnvVCF decay curve → engine.
void ADSR_VCF_change_decay_curve(uint8_t adsrCurveDecay) {
  adsr_vcf_voice.adsrCurveDecay(adsrCurveDecay);
  adsr_vcf_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf_voice.setSustain(ADSR_VCF_sustain);
  adsr_vcf_voice.setRelease(ADSR_VCF_release);
  adsr_vcf_voice.setResetAttack(VCFADSRRestart);

  adsr_vcf2_voice.adsrCurveDecay(adsrCurveDecay);
  adsr_vcf2_voice.setAttack(ADSR_VCF_attack);
  adsr_vcf2_voice.setDecay(ADSR_VCF_decay);
  adsr_vcf2_voice.setSustain(ADSR_VCF_sustain);
  adsr_vcf2_voice.setRelease(ADSR_VCF_release);
  adsr_vcf2_voice.setResetAttack(VCFADSRRestart);
}

void ADSR1_change_curves() {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr1_voice.setAttack(ADSR1_attack);
    ADSRVoices[i].adsr1_voice.setDecay(ADSR1_decay);
    ADSRVoices[i].adsr1_voice.setSustain(ADSR1_sustain);
    ADSRVoices[i].adsr1_voice.setRelease(ADSR1_release);
    ADSRVoices[i].adsr1_voice.setResetAttack(ADSRRestart);
  }
}
