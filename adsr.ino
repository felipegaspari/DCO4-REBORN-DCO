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

    ADSRVoices[i].adsr_vcf_voice.setAttack(ADSR_VCF_attack);
    ADSRVoices[i].adsr_vcf_voice.setDecay(ADSR_VCF_decay);
    ADSRVoices[i].adsr_vcf_voice.setSustain(ADSR_VCF_sustain);
    ADSRVoices[i].adsr_vcf_voice.setRelease(ADSR_VCF_release);
    ADSRVoices[i].adsr_vcf_voice.setResetAttack(VCFADSRRestart);
  }
}

// ~10 kHz: note edges → EnvDCO + EnvVCA + EnvVCF; sample levels.
inline void ADSR_update() {
  tADSR = millis();
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    if (noteEnd[i] == 1) {
      ADSRVoices[i].adsr1_voice.noteOff();
      ADSRVoices[i].adsr_vca_voice.noteOff();
      ADSRVoices[i].adsr_vcf_voice.noteOff();
      noteEnd[i] = 0;
    } else if (noteStart[i] == 1) {
      ADSRVoices[i].adsr1_voice.noteOff();
      ADSRVoices[i].adsr1_voice.setAttack(ADSR1_attack);
      ADSRVoices[i].adsr1_voice.setDecay(ADSR1_decay);
      ADSRVoices[i].adsr1_voice.setRelease(ADSR1_release);
      ADSRVoices[i].adsr1_voice.noteOn();

      ADSRVoices[i].adsr_vca_voice.noteOff();
      ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
      ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
      ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
      ADSRVoices[i].adsr_vca_voice.noteOn();

      ADSRVoices[i].adsr_vcf_voice.noteOff();
      ADSRVoices[i].adsr_vcf_voice.setAttack(ADSR_VCF_attack);
      ADSRVoices[i].adsr_vcf_voice.setDecay(ADSR_VCF_decay);
      ADSRVoices[i].adsr_vcf_voice.setRelease(ADSR_VCF_release);
      ADSRVoices[i].adsr_vcf_voice.noteOn();

      noteStart[i] = 0;
    }
    ADSR1Level[i] = ADSRVoices[i].adsr1_voice.getWave();
    ADSR_VCA_Level[i] = ADSRVoices[i].adsr_vca_voice.getWave();
    ADSR_VCF_Level[i] = ADSRVoices[i].adsr_vcf_voice.getWave();
  }
  ADSR_set_parameters();
}

// Debounced sustain (and EnvVCA/EnvVCF A/D/R when changed) push.
inline void ADSR_set_parameters() {
  if ((tADSR - tADSR_params) > 5) {
    static uint16_t last_attack = 0xFFFF, last_decay = 0xFFFF, last_sustain = 0xFFFF, last_release = 0xFFFF;
    static uint16_t last_vca_a = 0xFFFF, last_vca_d = 0xFFFF, last_vca_s = 0xFFFF, last_vca_r = 0xFFFF;
    static uint16_t last_vcf_a = 0xFFFF, last_vcf_d = 0xFFFF, last_vcf_s = 0xFFFF, last_vcf_r = 0xFFFF;

    bool dco_a = (ADSR1_attack != last_attack);
    bool dco_d = (ADSR1_decay != last_decay);
    bool dco_s = (ADSR1_sustain != last_sustain);
    bool dco_r = (ADSR1_release != last_release);
    bool vca_a = (ADSR_VCA_attack != last_vca_a);
    bool vca_d = (ADSR_VCA_decay != last_vca_d);
    bool vca_s = (ADSR_VCA_sustain != last_vca_s);
    bool vca_r = (ADSR_VCA_release != last_vca_r);
    bool vcf_a = (ADSR_VCF_attack != last_vcf_a);
    bool vcf_d = (ADSR_VCF_decay != last_vcf_d);
    bool vcf_s = (ADSR_VCF_sustain != last_vcf_s);
    bool vcf_r = (ADSR_VCF_release != last_vcf_r);

    if (dco_a || dco_d || dco_s || dco_r || vca_a || vca_d || vca_s || vca_r || vcf_a || vcf_d || vcf_s || vcf_r) {
      for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
        if (dco_a) ADSRVoices[i].adsr1_voice.setAttack(ADSR1_attack);
        if (dco_d) ADSRVoices[i].adsr1_voice.setDecay(ADSR1_decay);
        if (dco_s) ADSRVoices[i].adsr1_voice.setSustain(ADSR1_sustain);
        if (dco_r) ADSRVoices[i].adsr1_voice.setRelease(ADSR1_release);

        if (vca_a) ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
        if (vca_d) ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
        if (vca_s) ADSRVoices[i].adsr_vca_voice.setSustain(ADSR_VCA_sustain);
        if (vca_r) ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);

        if (vcf_a) ADSRVoices[i].adsr_vcf_voice.setAttack(ADSR_VCF_attack);
        if (vcf_d) ADSRVoices[i].adsr_vcf_voice.setDecay(ADSR_VCF_decay);
        if (vcf_s) ADSRVoices[i].adsr_vcf_voice.setSustain(ADSR_VCF_sustain);
        if (vcf_r) ADSRVoices[i].adsr_vcf_voice.setRelease(ADSR_VCF_release);
      }
      last_attack = ADSR1_attack; last_decay = ADSR1_decay;
      last_sustain = ADSR1_sustain; last_release = ADSR1_release;
      last_vca_a = ADSR_VCA_attack; last_vca_d = ADSR_VCA_decay;
      last_vca_s = ADSR_VCA_sustain; last_vca_r = ADSR_VCA_release;
      last_vcf_a = ADSR_VCF_attack; last_vcf_d = ADSR_VCF_decay;
      last_vcf_s = ADSR_VCF_sustain; last_vcf_r = ADSR_VCF_release;
    }
    tADSR_params = tADSR;
  }
}

void ADSR1_set_restart() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr1_voice.setResetAttack(ADSRRestart);
  }
}

void ADSR_VCA_set_restart() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr_vca_voice.setResetAttack(VCAADSRRestart);
  }
}

void ADSR_VCF_set_restart() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr_vcf_voice.setResetAttack(VCFADSRRestart);
  }
}

void ADSR1_change_curves() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr1_voice.setAttack(ADSR1_attack);
    ADSRVoices[i].adsr1_voice.setDecay(ADSR1_decay);
    ADSRVoices[i].adsr1_voice.setSustain(ADSR1_sustain);
    ADSRVoices[i].adsr1_voice.setRelease(ADSR1_release);
    ADSRVoices[i].adsr1_voice.setResetAttack(ADSRRestart);
  }
}
