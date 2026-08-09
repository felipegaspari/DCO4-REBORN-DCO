// Compile mo-lfo into the sketch TU (Arduino IDE does not link _build_libs/*.cpp).
#include "_build_libs/mo-lfo/mo-lfo.cpp"

// Init LFO1 and LFO2 instances. Called from setup() (Core 0).
void init_LFOs() {
  init_LFO1();
  init_LFO2();
}

// Init all per-oscillator drift LFOs. Called from setup() (Core 0).
void init_DRIFT_LFOs() {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    init_DRIFT_LFO(LFO_DRIFT_CLASS[i], i);
  }
}

// Configure one drift LFO. Wave bus is full-scale Q15 (dacSize unused).
void init_DRIFT_LFO(lfo &LFO, byte LFONumber) {
  LFO_DRIFT_SPEED_OFFSET[LFONumber] = (float)(1.00f - (float)((float)analogDriftSpread * 0.005) + (float)((float)analogDriftSpread * 0.00125f * (float)LFONumber)) * (float)expConverterFloat((float)analogDriftSpeed, 5000);
  LFO.setWaveForm(LFO_DRIFT_WAVEFORM);
  LFO.setAmplQ15(MO_LFO_Q15_ONE);
  LFO.setMode(false);               // free-running (Hz)
  LFO.setMode0Freq(LFO_DRIFT_SPEED_OFFSET[LFONumber], micros());
}

// Configure main detune LFO (LFO1). Amplitude is Q15 full-scale; depths live in *_q24.
void init_LFO1() {
  LFO1_class.setWaveForm(LFO1Waveform);
  LFO1_class.setAmplQ15(MO_LFO_Q15_ONE);
  LFO1_class.setMode(false);
  LFO1_class.setMode0Freq(0.5f);
}


// Configure secondary LFO (LFO2: PW / OSC2 detune, etc.).
void init_LFO2() {
  LFO2_class.setWaveForm(LFO2Waveform);
  LFO2_class.setAmplQ15(MO_LFO_Q15_ONE);
  LFO2_class.setMode(false);
  LFO2_class.setMode0Freq(5.0f);
}

// Update LFO1 Q15 level + per-osc pitch mods (depth_q24 is full-scale octave travel).
void __not_in_flash_func(LFO1)() {
  LFO1Level = LFO1_class.getWaveQ15(micros());
  // Common case: only global LFO1→DCO — one mul, broadcast to all osc slots.
  if (LFO1toOSC1_q24 == 0 && LFO1toOSC2_q24 == 0 && LFO1toOSC3_q24 == 0) {
    const int32_t m = applyDepthQ24(LFO1Level, LFO1toDCO_q24);
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC1] = m;
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC2] = m;
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC3] = m;
  } else {
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC1] =
      applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC1_q24);
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC2] =
      applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC2_q24);
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC3] =
      applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC3_q24);
  }
}

// Update LFO2 Q15 level + OSC2/3 pitch mods (fine + coarse folded).
// DMB after LFO1()+LFO2() stores so Core1 snapshots a coherent mailbox (not SIO FIFO).
void __not_in_flash_func(LFO2)() {
  LFO2Level = LFO2_class.getWaveQ15(micros());
  lfo2_pitch_mod_q24[LFO2_PITCH_OSC2] =
    applyDepthQ24(LFO2Level, LFO2toOSC2_q24 + LFO2toOSC2_coarse_q24);
  lfo2_pitch_mod_q24[LFO2_PITCH_OSC3] =
    applyDepthQ24(LFO2Level, LFO2toOSC3_q24 + LFO2toOSC3_coarse_q24);
  __dmb();
}

// Update per-oscillator drift LFO levels as Q15 (negate wave = legacy polarity).
// Publish mailbox for Core 1: three stores then DMB (not SIO FIFO).
void __not_in_flash_func(DRIFT_LFOs)() {
  unsigned long currentMicros = micros();
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    LFO_DRIFT_LEVEL[i] = (int16_t)(-LFO_DRIFT_CLASS[i].getWaveQ15(currentMicros));
  }
  __dmb();
}
