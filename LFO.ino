// Init LFO1 and LFO2 instances. Called from setup() (Core 0).
void init_LFOs() {
  init_LFO1();
  init_LFO2();
}

// Init all per-oscillator drift LFOs. Called from setup() (Core 0).
void init_DRIFT_LFOs() {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    init_DRIFT_LFO(LFO_DRIFT_CLASS[i], LFO_DRIFT_CC, i);
  }
}

// Configure one drift LFO (waveform, amplitude, speed offset from spread/speed params).
void init_DRIFT_LFO(lfo &LFO, int CC, byte LFONumber) {
  LFO_DRIFT_SPEED_OFFSET[LFONumber] = (float)(1.00f - (float)((float)analogDriftSpread * 0.005) + (float)((float)analogDriftSpread * 0.00125f * (float)LFONumber)) * (float)expConverterFloat((float)analogDriftSpeed, 5000);
  LFO.setWaveForm(LFO_DRIFT_WAVEFORM);                  // inicializar forma de onda
  LFO.setAmpl(CC);                                      // establecer amplitud máxima
  LFO.setAmplOffset(0);                                 // sin offset a la amplitud
  LFO.setMode(0);                                       // establecer modo de sincronización a modo0 -> sin sincronización a BPM
  //LFO.setMode0Freq(LFO_DRIFT_SPEED_OFFSET[LFONumber]);  // establecer LFO a 0.5 Hz
  LFO.setMode0Freq(LFO_DRIFT_SPEED_OFFSET[LFONumber], micros());
}

// Configure main detune LFO (LFO1). Called from init_LFOs().
void init_LFO1() {
  LFO1_class.setWaveForm(LFO1Waveform);  // initialize waveform
  LFO1_class.setAmpl(LFO1_CC);           // set amplitude to maximum
  LFO1_class.setAmplOffset(0);           // no offset to the amplitude
  LFO1_class.setMode(0);                 // set sync mode to mode0 -> no sync to BPM
  LFO1_class.setMode0Freq(0.5);          // set LFO to 30 Hz
}


// Configure secondary LFO (LFO2: PW / OSC2 detune, etc.). Called from init_LFOs().
void init_LFO2() {
  LFO2_class.setWaveForm(2);    // initialize waveform
  LFO2_class.setAmpl(LFO2_CC);  // set amplitude to maximum
  LFO2_class.setAmplOffset(0);  // no offset to the amplitude
  LFO2_class.setMode(0);        // set sync mode to mode0 -> no sync to BPM
  LFO2_class.setMode0Freq(5);   // set LFO to 30 Hz
}

// Update LFO1 per-osc pitch mods (LFO1toDCO + per-osc extra, one product each). ~50 µs.
inline void LFO1() {
  LFO1Level = LFO1_class.getWave(micros()) - LFO1_CC_HALF;
  const int32_t lfo1 = LFO1Level;
  lfo1_pitch_mod_q24[LFO1_PITCH_OSC1] = lfo1 * (LFO1toDCO_q24 + LFO1toOSC1_q24);
  lfo1_pitch_mod_q24[LFO1_PITCH_OSC2] = lfo1 * (LFO1toDCO_q24 + LFO1toOSC2_q24);
  lfo1_pitch_mod_q24[LFO1_PITCH_OSC3] = lfo1 * (LFO1toDCO_q24 + LFO1toOSC3_q24);
}

// Update LFO2 pitch mods for OSC2/3 (fine + coarse folded). ~50 µs.
inline void LFO2() {
  LFO2Level = LFO2_class.getWave(micros()) - LFO2_CC_HALF;
  const int32_t lfo2 = LFO2Level;
  lfo2_pitch_mod_q24[LFO2_PITCH_OSC2] = lfo2 * (LFO2toOSC2_q24 + LFO2toOSC2_coarse_q24);
  lfo2_pitch_mod_q24[LFO2_PITCH_OSC3] = lfo2 * (LFO2toOSC3_q24 + LFO2toOSC3_coarse_q24);
}

// Update per-oscillator drift LFO levels into LFO_DRIFT_LEVEL[]. Called with LFO2 ~every 50 µs.
inline void DRIFT_LFOs() {
  unsigned long currentMicros = micros();
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    LFO_DRIFT_LEVEL[i] = LFO_DRIFT_CC_HALF - LFO_DRIFT_CLASS[i].getWave(currentMicros);
  }
}
