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

// Configure one drift LFO. Wave bus is full-scale Q15; CC arg is legacy/unused for amplitude.
void init_DRIFT_LFO(lfo &LFO, int CC, byte LFONumber) {
  (void)CC;
  LFO_DRIFT_SPEED_OFFSET[LFONumber] = (float)(1.00f - (float)((float)analogDriftSpread * 0.005) + (float)((float)analogDriftSpread * 0.00125f * (float)LFONumber)) * (float)expConverterFloat((float)analogDriftSpeed, 5000);
  LFO.setWaveForm(LFO_DRIFT_WAVEFORM);
  LFO.setAmplQ15(MO_LFO_Q15_ONE);  // full-scale bipolar Q15 (±1.0)
  LFO.setMode(0);
  LFO.setMode0Freq(LFO_DRIFT_SPEED_OFFSET[LFONumber], micros());
}

// Configure main detune LFO (LFO1). Amplitude is Q15 full-scale; depths live in *_q24.
void init_LFO1() {
  LFO1_class.setWaveForm(LFO1Waveform);
  LFO1_class.setAmplQ15(MO_LFO_Q15_ONE);
  LFO1_class.setMode(0);
  LFO1_class.setMode0Freq(0.5);
}


// Configure secondary LFO (LFO2: PW / OSC2 detune, etc.).
void init_LFO2() {
  LFO2_class.setWaveForm(2);
  LFO2_class.setAmplQ15(MO_LFO_Q15_ONE);
  LFO2_class.setMode(0);
  LFO2_class.setMode0Freq(5);
}

// Update LFO1 Q15 level + per-osc pitch mods (depth_q24 is full-scale octave travel). ~50 µs.
inline void LFO1() {
  LFO1Level = LFO1_class.getWaveQ15(micros());
  lfo1_pitch_mod_q24[LFO1_PITCH_OSC1] =
    lfo::applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC1_q24);
  lfo1_pitch_mod_q24[LFO1_PITCH_OSC2] =
    lfo::applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC2_q24);
  lfo1_pitch_mod_q24[LFO1_PITCH_OSC3] =
    lfo::applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC3_q24);
}

// Update LFO2 Q15 level + OSC2/3 pitch mods (fine + coarse folded). ~50 µs.
inline void LFO2() {
  LFO2Level = LFO2_class.getWaveQ15(micros());
  lfo2_pitch_mod_q24[LFO2_PITCH_OSC2] =
    lfo::applyDepthQ24(LFO2Level, LFO2toOSC2_q24 + LFO2toOSC2_coarse_q24);
  lfo2_pitch_mod_q24[LFO2_PITCH_OSC3] =
    lfo::applyDepthQ24(LFO2Level, LFO2toOSC3_q24 + LFO2toOSC3_coarse_q24);
}

// Update per-oscillator drift LFO levels as Q15 (legacy polarity: half - wave). ~50 µs.
inline void DRIFT_LFOs() {
  unsigned long currentMicros = micros();
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    LFO_DRIFT_LEVEL[i] = (int16_t)(-LFO_DRIFT_CLASS[i].getWaveQ15(currentMicros));
  }
}
