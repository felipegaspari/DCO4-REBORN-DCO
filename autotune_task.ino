// =============================================================================
// DCO AUTOTUNE & CALIBRATION TASK MANAGER (autotune_task.ino)
// =============================================================================
// This file centralizes all calibration routines: manual, automated, and sweeps.
// It completely replaces the legacy 'voice_task_autotune' function.

// -----------------------------------------------------------------------------
// 1. Hardware Calibration Driver
// -----------------------------------------------------------------------------
// Drives a single oscillator at a specific frequency and amplitude (Range PWM).
void autotune_drive_core(uint8_t osc, float freqHz, uint16_t ampValue) {
    PIO pioN = pio[VOICE_TO_PIO[osc]];
    uint8_t sm1N = VOICE_TO_SM[osc];
  
    uint32_t total_cycles = (freqHz > 0.0f) ? (uint32_t)fminf(((float)sysClock_Hz / freqHz) + 0.5f, 4.0e9f) : 0u;
    uint32_t clk_div = total_cycles ? pio_clk_div_for_y(total_cycles, osc_last_y[osc], osc_ramp_weight(osc), osc_period_overhead(osc)) : 0u;
  
    pio_sm_set_enabled(pioN, sm1N, true);
    pio_sm_put(pioN, sm1N, clk_div);
    pio_sm_exec(pioN, sm1N, pio_encode_pull(false, false));
  
    write_range_pwm(osc, ampValue);
  }
  
// =============================================================================
// 2. Manual Calibration Loop (UI / Trimpot / PW editing)
// =============================================================================
void autotune_manual_task() {
  static uint8_t lastManualStage = 0xFF;
  static uint8_t lastDCO         = 0xFF;

  if (calSyncNeutralRequested) {
    calSyncNeutralRequested = false;
    setSyncMode();
  }

  currentDCO = cal_manual_osc();
  const uint8_t pwCh = cal_pw_channel(currentDCO);
  
  bool is440Stage = cal_stage_is_440(manualCalibrationStage) || (manualCalibrationStep == 1);
  bool isPwEdit   = cal_stage_is_pw_edit(manualCalibrationStage);
  bool isSquare   = cal_stage_is_square(manualCalibrationStage);
  
  // SURGICAL FIX: Enable pulse for both Square wave stages AND PW edit stages
  bool wantPulse  = (isSquare || isPwEdit);

  // =========================================================================
  // 1. STAGE TRANSITION DETECTOR
  // =========================================================================
  bool stageChanged = (manualCalibrationStage != lastManualStage);

  if (stageChanged) {
    bool oscChanged = (currentDCO != lastDCO);

    // Mute all inactive oscillator range levels
    for (int i = 0; i < NUM_OSCILLATORS; i++) {
      if (i != currentDCO) {
        PIO pioN = pio[VOICE_TO_PIO[i]];
        uint8_t sm1N = VOICE_TO_SM[i];
        pio_sm_set_enabled(pioN, sm1N, false);
        pio_sm_put(pioN, sm1N, 0);
        pio_sm_exec(pioN, sm1N, pio_encode_pull(false, false));
        write_range_pwm(i, 0);
      }
    }

    // Analog Discharge Drain pause when switching physical oscillators
    if (oscChanged && lastDCO != 0xFF) {
      delay(150); 
    }

    // Configure Audio Waveform MUX (routes Saw/Tri/Pulse to sound chain)
    update_CV_outs_manual_calibration();

    lastManualStage    = manualCalibrationStage;
    lastDCO            = currentDCO;
    g_lastDrivenFreqHz = 0.0f;
  }

  // =========================================================================
  // 2. CONFIGURE PULSE WIDTH HARDWARE (Live Update)
  // =========================================================================
  if (wantPulse && osc_has_pw(currentDCO)) {
    // Pulse / PW Edit Stage: Actively drive active channel with its PW_CENTER value
    // and mute all other PW channels
    apply_pw_center_solo(pwCh);
  } else {
    // Saw / Tri Stage: Mute ALL Pulse channels to completely isolate the analog waveform
    for (uint8_t ch = 0; ch < NUM_PW_CHANNELS; ch++) {
      if (PW_PINS[ch] != PW_PIN_UNASSIGNED) {
        pwm_set_chan_level(PW_PWM_SLICES[ch], pwm_gpio_to_channel(PW_PINS[ch]), 0);
        PW[ch] = 0;
      }
    }
  }

  // =========================================================================
  // 3. CONTINUOUS LIVE UPDATE (Calculate Target Pitch & Amplitude)
  // =========================================================================
  if (is440Stage) {
    VOICE_NOTES[0] = manual_cal_reference_note;
    DCO_calibration_current_note = manual_cal_reference_note;

    if (ampComp440[currentDCO] != 0) {
      ampCompCalibrationVal = ampComp440[currentDCO];
    } else {
      float scale = note_to_freq(manual_cal_reference_note) / note_to_freq(manual_DCO_calibration_start_note);
      ampCompCalibrationVal = (uint16_t)((initManualAmpCompCalibrationValPreset + manualCalibrationOffset[currentDCO]) * scale + 0.5f);
    }
  } else {
    VOICE_NOTES[0] = manual_DCO_calibration_start_note;
    DCO_calibration_current_note = manual_DCO_calibration_start_note;
    ampCompCalibrationVal = initManualAmpCompCalibrationValPreset + manualCalibrationOffset[currentDCO];
  }

  float freqHz = note_to_freq(DCO_calibration_current_note);

  // 4. Drive target oscillator
  autotune_drive_core(currentDCO, freqHz, ampCompCalibrationVal);

  if (stageChanged) {
    delay(100); 
  }

  // Stream live gap telemetry to UI / Screen
  DCO_calibration_debug();

  if (pwCvProbeRequested) {
    pwCvProbeRequested = false;
    run_pw_cv_probe();
  }
}
  
// -----------------------------------------------------------------------------
// 3. Main Entry Point: Called repeatedly from loop1()
// -----------------------------------------------------------------------------
void autotune_loop_task() {

    // =========================================================================
  // 1. CACHE AND NEUTRALIZE SYNC TOPOLOGY
  // =========================================================================
  // Calibration requires 100% free-running oscillators. If a preset had Hard/Soft 
  // sync enabled, we must sever the PIO pin cross-linking so parked masters 
  // don't freeze the slave state machines.
  manualCalSavedSyncMode     = syncMode;
  manualCalSavedOscPhaseSync = oscPhaseSync;
  manualCalSavedSoftSyncChunks = softSyncChunks;

  softSyncChunks = 0;
  syncMode     = 0;
  oscPhaseSync = 0;
  setSyncMode(); // Reconfigures all PIOs for independent free-running operation



  // Trap Core 1 here as long as calibration is active.
  // This allows delay(), micros(), and Serial to function perfectly!
  while (calibrationFlag || calibrationVerifyRequested) {
    
    if (calibrationFlag) {
      if (manualCalibrationFlag) {
        // Continuous live task for UI interactions
        autotune_manual_task();
      } else {
        // Blocking automated routine (Full, Amp, or PW auto-sweep)
        DCO_calibration(); 
      }
    } else if (calibrationVerifyRequested) {
      calibrationVerifyRequested = false;
      // Blocking verification sweep
      run_calibration_verify_sweep();
    }
    
    // Process serial/MIDI buffers briefly to prevent USB lockups during manual mode
    tight_loop_contents(); 
  }


  // =========================================================================
  // 3. RESTORE SYNC TOPOLOGY & ENGINE STATE
  // =========================================================================
  softSyncChunks = manualCalSavedSoftSyncChunks;
  syncMode     = manualCalSavedSyncMode;
  oscPhaseSync = manualCalSavedOscPhaseSync;
  
  setSyncMode(); // Re-link the PIOs if they were synced prior to calibration

  oscPhaseSync = manualCalSavedOscPhaseSync;
  if (oscPhaseSync < 2) {
    phaseAlignOSC2 = 0;
    pio_defer_request_reset_pulse_all();
  } else {
    if (oscPhaseSync > 8) {
      phaseAlignOSC2 = oscPhaseSync * 2;
    } else {
      switch (oscPhaseSync) {
        case 2: phaseAlignOSC2 = 45;  break;
        case 3: phaseAlignOSC2 = 90;  break;
        case 4: phaseAlignOSC2 = 135; break;
        case 5: phaseAlignOSC2 = 180; break;
        case 6: phaseAlignOSC2 = 225; break;
        case 7: phaseAlignOSC2 = 270; break;
        case 8: phaseAlignOSC2 = 315; break;
        default: break;
      }
    }
  }
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    note_on_flag[i] = 1;
  }

  restore_voice_engine_after_calibration();
}