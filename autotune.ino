
#include "include_all.h"

// For debug logging and duty computation in gap measurement: track the last
// PW raw value we explicitly programmed for the current DCO, the duty
// target/period assumed by the current PW search routine, and the most
// recently measured period from find_gap().
static uint16_t g_lastPWMeasurementRaw = 0;
static double   g_gapLogCurrentPeriodUs = 0.0;
static double   g_gapLogTargetDutyFraction = 0.5;  // default 50%
static double   g_lastGapMeasuredPeriodUs = 0.0;

// Helper: turn off all oscillators and set their RANGE outputs to a known
// state, while charging their timing capacitors using the original
// PIO+GPIO sequence. This preserves the analogue behaviour you rely on.
static void disable_all_oscillators_and_range_pwm() {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    uint8_t pioNumber = VOICE_TO_PIO[i];
    PIO     pioN      = pio[VOICE_TO_PIO[i]];
    uint8_t smN = VOICE_TO_SM[i];

    // Original "park" frequency used to pre-charge the caps.
    uint32_t clk_div1 = 200;

    // Run the DCO SM at a known slow rate while driving the RANGE PWM.
    pio_sm_set_enabled(pioN, smN, true);
    pio_sm_put(pioN, smN, clk_div1);
    pio_sm_exec(pioN, smN, pio_encode_pull(false, false));

    delay(200);

    // Stop the SM and hold the RANGE pin high as a plain GPIO output.
    pio_sm_set_enabled(pioN, smN, false);
    gpio_init(RANGE_PINS[i]);
    gpio_set_dir(RANGE_PINS[i], GPIO_OUT);
    gpio_put(RANGE_PINS[i], 1);
  }

  // After all RANGE caps are charged, park shared PW PWM at max wrap so the
  // centre search can start from a known state. (Matches original behaviour.)
  reset_pw_to_DIV_COUNTER_PW();
}



// Helper: park shared PW PWM at max wrap (DIV_COUNTER_PW). Called from disable_all_oscillators_and_range_pwm().
static void reset_pw_to_DIV_COUNTER_PW() {
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), DIV_COUNTER_PW);
  }
}


// Initialize legacy PID-based DCO calibration state for oscillator 0.
// Note: the main calibration now uses calibrate_DCO(); this is kept
// for compatibility and reference.
// Currently unreachable at boot: setup1() clears calibrationFlag before the call site.
void init_DCO_calibration() {

  currentDCO = 0;

  VOICE_NOTES[0] = DCO_calibration_start_note;
  DCO_calibration_current_note = DCO_calibration_start_note;

  arrayPos = 0;
  calibrationData[arrayPos] = 0;
  calibrationData[arrayPos + 1] = ampCompLowestFreqVal;
  arrayPos += 2;

  calibrationData[arrayPos] = (uint32_t)(sNotePitches[manual_DCO_calibration_start_note - 12] * 100);
  calibrationData[arrayPos + 1] = initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];

  arrayPos += 2;

  currentNoteCalibrationStart = micros();
  DCOCalibrationStart = millis();
  DCO_calibration_difference = 10000;
  PIDMinGap = 300;

  samplesNumber = 52;

  sampleTime = (1000000 / sNotePitches[DCO_calibration_current_note - 12]) * ((samplesNumber - 1) / 2);

  PIDLimitsFormula = 78;
  PIDOutputLowerLimit = 70;
  PIDOutputHigherLimit = 100;

  // TURN OFF ALL OSCILLATORS and park shared PW voice.
  disable_all_oscillators_and_range_pwm();

  delay(100);

  DCO_calibration_difference = 4000;
  bestGap = 50000;
  bestCandidate = 50000;
  edgeDetectionLastTime = 0;
  PIDMinGapCounter = 0;
  pulseCounter = 0;
}

/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/
// Main DCO amplitude-compensation calibration entry point.
// Monosynth: calibrate shared PW on voice 0 once, then for each oscillator
// run calibrate_DCO() to build a [freq -> range PWM] table and persist via update_FS_voice().
void DCO_calibration() {

  // TURN OFF ALL OSCILLATORS and park shared PW voice.
  disable_all_oscillators_and_range_pwm();

  // PW is per-voice (monosynth: voice 0 only). Calibrate once, then amp-comp per osc.
  currentDCO = 0;
  restart_DCO_calibration();
  DCO_calibration_current_note = manual_DCO_calibration_start_note;
  VOICE_NOTES[0] = DCO_calibration_current_note;
  find_PW_center(0);
  find_PW_limit_v2(PW_LIMIT_LOW);
  find_PW_limit_v2(PW_LIMIT_HIGH);
  pwm_set_chan_level(PW_PWM_SLICES[0], pwm_gpio_to_channel(PW_PINS[0]), PW_CENTER[0]);
  PW[0] = PW_CENTER[0];

  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    currentDCO = i;

    restart_DCO_calibration();

    ampCompCalibrationVal = initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];
    pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO], pwm_gpio_to_channel(RANGE_PINS[currentDCO]), ampCompCalibrationVal);

    DCO_calibration_current_note = DCO_calibration_start_note;
    VOICE_NOTES[0] = DCO_calibration_current_note;

    // uint16_t lowestFrequency = find_lowest_freq();
    // calibrationData[0] = lowestFrequency;

    bool oscAmpCompCalibrationComplete = false;

    // Build a small context for this DCO and run the calibration routine.
    DCOCalibrationContext ctx(
      currentDCO,
      DCO_calibration_current_note,
      calibrationData,
      manualCalibrationOffset,
      initManualAmpCompCalibrationVal
    );
    // Desired duty-cycle error tolerance as a fraction (e.g. 0.005 = 0.5%).
    double dutyErrorFraction = 0.001;
    calibrate_DCO(ctx, dutyErrorFraction);

    for (int j = 0; j < chanLevelVoiceDataSize; j++) {
      Serial.println(calibrationData[j]);
    }

    update_FS_voice(currentDCO);

    Serial.println((String) "DCO " + currentDCO + (String) " calibration finished.");
  }
  calibrationFlag = false;
  init_FS();

  // Rebuild amp-comp tables for the active engine.
  precompute_amp_comp_for_engine();
}
/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/

// Reset per-DCO calibration state and header entries in calibrationData.
// This is called before calibrating each DCO (and reused by VCO calibration).
void restart_DCO_calibration() {

  VOICE_NOTES[0] = DCO_calibration_start_note;
  DCO_calibration_current_note = DCO_calibration_start_note;

  arrayPos = 0;
  calibrationData[arrayPos] = 0;
  calibrationData[arrayPos + 1] = ampCompLowestFreqVal;
  arrayPos += 2;

  calibrationData[arrayPos] = (uint32_t)(sNotePitches[DCO_calibration_current_note - calibration_note_interval - 12] * 100);
  calibrationData[arrayPos + 1] = initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];

  arrayPos += 2;

  currentNoteCalibrationStart = micros();
  DCOCalibrationStart = millis();
  DCO_calibration_difference = 10000;
  PIDMinGap = 300;

  // TURN OFF ALL OSCILLATORS for a clean restart, and pre-charge the
  // RANGE capacitors using the legacy helper.
  disable_all_oscillators_and_range_pwm();

  // IMPORTANT: disable_all_oscillators_and_range_pwm() leaves RANGE_PINS[]
  // as plain GPIO outputs driven HIGH. Before starting calibration for the
  // currentDCO we must restore its RANGE pin back to PWM function so that
  // voice_task_autotune() and subsequent RANGE PWM writes actually appear
  // on the physical pin.
  gpio_set_function(RANGE_PINS[currentDCO], GPIO_FUNC_PWM);

  PIO pioN = pio[VOICE_TO_PIO[currentDCO]];
  uint8_t sm1N = VOICE_TO_SM[currentDCO];
  pio_sm_set_enabled(pioN, sm1N, true);

  delay(100);

  DCO_calibration_difference = 4000;
  bestGap = 50000;
  bestCandidate = 50000;
  edgeDetectionLastTime = 0;
  PIDMinGapCounter = 0;
  pulseCounter = 0;
}

/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/

// Shared search routine used by PW calibration functions (center/low/high).
// It searches for the PW value that yields a duty cycle closest to
// targetDutyFraction at the current calibration note. targetGap is the
// allowed absolute gap (in microseconds) from the ideal duty at that note.
static uint16_t find_PW_for_target_duty(double targetDutyFraction,
                                        uint16_t targetGap,
                                        uint16_t pwMin,
                                        uint16_t pwMax) {

  DCO_calibration_difference = 4000;
  bestGap = 50000;
  bestCandidate = 50000;
  edgeDetectionLastTime = 0;
  PIDMinGapCounter = 0;
  pulseCounter = 0;

  // Fixed-size tables for valid and invalid samples.
  // Valid samples: store (PW, gapDiff = gap - gapTarget).
  // Invalid samples: store (PW, distance in PW units to the nearest valid sample).
  const int kMaxSamples = 40;
  uint16_t validPW[kMaxSamples];
  double   validGapDiff[kMaxSamples];
  int      validCount = 0;
  int      inToleranceCount = 0;  // Number of valid samples within target gap

  uint16_t invalidPW[kMaxSamples];
  uint16_t invalidDistToValid[kMaxSamples];
  int      invalidCount = 0;

  // Precompute period and duty-cycle tolerance (in %) for debug reporting.
  double freqHz = (double)sNotePitches[DCO_calibration_current_note - 12];
  double periodUs = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;

  // Update global logging context for gap measurements during this search.
  g_gapLogCurrentPeriodUs     = periodUs;
  g_gapLogTargetDutyFraction  = targetDutyFraction;
  double toleranceDutyPercent = 0.0;
  double gapTarget = 0.0;
  if (periodUs > 0.0) {
    // Ideal gap for target duty: gap = T*(1 - 2p)
    gapTarget = periodUs * (1.0 - 2.0 * targetDutyFraction);
    double tolDutyFrac = (double)targetGap / (2.0 * periodUs);
    toleranceDutyPercent = tolDutyFrac * 100.0;
  }

  // ---- Phase 1: Coarse scan over PW range to find a sign-change bracket ----
  // Use smaller coarse steps for low/high limit searches (target duty far from 50%)
  // and larger steps for center search (targetDutyFraction ~ 0.5).
  uint16_t coarseDiv = (fabs(targetDutyFraction - 0.5) < 0.05) ? 16 : 32;
  uint16_t coarseStep = (pwMax > pwMin) ? ((pwMax - pwMin) / coarseDiv) : 1;
  if (coarseStep == 0) coarseStep = 1;

  bool havePrev = false;
  double prevGap = 0.0;
  uint16_t prevPW = 0;

  bool haveBracket = false;
  uint16_t pwLow = 0, pwHigh = 0;
  double gapLow = 0.0, gapHigh = 0.0;

  for (uint16_t pw = pwMin; pw <= pwMax; pw = (uint16_t)(pw + coarseStep)) {

    if (millis() - DCOCalibrationStart > 60000) {
      Serial.println("PW center coarse scan timeout (60s)");
      break;
    }

    pwm_set_chan_level(PW_PWM_SLICES[0],
                       pwm_gpio_to_channel(PW_PINS[0]),
                       pw);
    // Keep PW[] and debug tracker in sync so GAP logs show the actual PW tested.
    PW[0]        = pw;
    g_lastPWMeasurementRaw    = pw;
    delay(30);

    GapMeasurement gm = measure_gap(2);
    if (gm.timedOut) {
      // No usable signal at this PW; if we have at least one valid sample,
      // track this as an invalid entry near some valid PW for diagnostic use.
      if (validCount > 0 && invalidCount < kMaxSamples) {
        // Compute distance to nearest valid PW.
        uint16_t bestDist = 0xFFFF;
        for (int vi = 0; vi < validCount; ++vi) {
          uint16_t dist = (validPW[vi] > pw) ? (validPW[vi] - pw) : (pw - validPW[vi]);
          if (dist < bestDist) bestDist = dist;
        }
        invalidPW[invalidCount] = pw;
        invalidDistToValid[invalidCount] = bestDist;
        invalidCount++;
      } else if (validCount > 0 && invalidCount >= kMaxSamples) {
        // Table full: only keep invalids that are closer to valids than the current worst.
        uint16_t bestDist = 0xFFFF;
        for (int vi = 0; vi < validCount; ++vi) {
          uint16_t dist = (validPW[vi] > pw) ? (validPW[vi] - pw) : (pw - validPW[vi]);
          if (dist < bestDist) bestDist = dist;
        }
        // Find worst (largest distance) invalid entry.
        int worstIdx = 0;
        uint16_t worstDist = invalidDistToValid[0];
        for (int ii = 1; ii < invalidCount; ++ii) {
          if (invalidDistToValid[ii] > worstDist) {
            worstDist = invalidDistToValid[ii];
            worstIdx = ii;
          }
        }
        if (bestDist < worstDist) {
          invalidPW[worstIdx] = pw;
          invalidDistToValid[worstIdx] = bestDist;
        }
      }
      continue;  // skip invalid sample
    }

    double gap = (double)gm.value;
    double gapDiff = gap - gapTarget;
    double absGapDiff = abs(gapDiff);

    if (autotuneDebug >= 2 && periodUs > 0.0) {
      double dutyErrorFrac = gap / (2.0 * periodUs);
      double dutyPercent = (0.5 + dutyErrorFrac) * 100.0;
      Serial.println((String)"[PW_CENTER_COARSE] note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" PW_raw=" + pw +
                     (String)" gap=" + gap +
                     (String)"us duty=" + dutyPercent +
                     (String)"% target=50% tol≈" + toleranceDutyPercent + "%");
    }

    if (absGapDiff <= (double)targetGap) {
      inToleranceCount++;
    }

    if (absGapDiff < bestGap) {
      bestGap = absGapDiff;
      bestCandidate = pw;
    }

    // Maintain table of best valid samples.
    if (validCount < kMaxSamples) {
      validPW[validCount] = pw;
      validGapDiff[validCount] = gapDiff;
      validCount++;
    } else {
      // Table full: replace the worst entry if this one is closer to the target.
      int worstIdx = 0;
      double worstAbs = fabs(validGapDiff[0]);
      for (int vi = 1; vi < validCount; ++vi) {
        double curAbs = fabs(validGapDiff[vi]);
        if (curAbs > worstAbs) {
          worstAbs = curAbs;
          worstIdx = vi;
        }
      }
      if (absGapDiff < worstAbs) {
        validPW[worstIdx] = pw;
        validGapDiff[worstIdx] = gapDiff;
      }
    }

    if (havePrev) {
      // Check for sign change between prevGap and gap (relative to target duty)
      if ((gapDiff > 0.0 && prevGap < 0.0) || (gapDiff < 0.0 && prevGap > 0.0)) {
        haveBracket = true;
        pwLow = prevPW;
        gapLow = prevGap;
        pwHigh = pw;
        gapHigh = gap;

        // With two valid samples straddling the target, estimate the crossing
        // point via linear interpolation between prevGap and gap.
        double prevGapDiff = prevGap - gapTarget;
        double curGapDiff  = gap - gapTarget;
        double denom = fabs(prevGapDiff) + fabs(curGapDiff);
        if (denom > 0.0) {
          double t = fabs(prevGapDiff) / denom;  // weight towards the closer side
          uint16_t pwEst = (uint16_t)((double)prevPW + ((double)(pw - prevPW) * t));
          if (pwEst >= pwMin && pwEst <= pwMax) {
            pwm_set_chan_level(PW_PWM_SLICES[0],
                               pwm_gpio_to_channel(PW_PINS[0]),
                               pwEst);
            PW[0]     = pwEst;
            g_lastPWMeasurementRaw = pwEst;
            delay(30);
            GapMeasurement gmEst = measure_gap(2);
            if (!gmEst.timedOut) {
              double gapEst = (double)gmEst.value;
              double gapDiffEst = gapEst - gapTarget;
              double absGapDiffEst = fabs(gapDiffEst);

              if (absGapDiffEst <= (double)targetGap) {
                inToleranceCount++;
              }

              if (absGapDiffEst < bestGap) {
                bestGap = absGapDiffEst;
                bestCandidate = pwEst;
              }
              // Insert estimated point into valid table if it's good enough.
              if (validCount < kMaxSamples) {
                validPW[validCount] = pwEst;
                validGapDiff[validCount] = gapDiffEst;
                validCount++;
              }
            }
          }
        }

        break;
      }
    }

    havePrev = true;
    // For the bracket we keep the raw gap value; we subtract gapTarget only
    // when computing gapDiff.
    prevGap = gap;
    prevPW = pw;
  }

  // If we didn't find a bracket, we still want a fine search around the best
  // coarse candidate so that we gather multiple near-target samples before
  // deciding on a final PW.
  if (!haveBracket) {
    if (autotuneDebug >= 1) {
      Serial.println("PW center: no sign-change bracket found, running local fine scan.");
    }
    uint16_t startPW = (bestCandidate >= pwMin && bestCandidate <= pwMax)
                         ? bestCandidate
                         : (uint16_t)((pwMin + pwMax) / 2);
    uint16_t span = (coarseStep > 0) ? coarseStep * 2 : 4;
    uint16_t fineMin = (startPW > span) ? (startPW - span) : pwMin;
    uint16_t fineMax = (startPW + span < pwMax) ? (startPW + span) : pwMax;
    if (fineMax < fineMin) {
      uint16_t tmp = fineMin;
      fineMin = fineMax;
      fineMax = tmp;
    }
    uint16_t fineStep = (fineMax > fineMin) ? ((fineMax - fineMin) / 16) : 1;
    if (fineStep == 0) fineStep = 1;

    for (uint16_t pw = fineMin; pw <= fineMax; pw = (uint16_t)(pw + fineStep)) {
      if (millis() - DCOCalibrationStart > 60000) {
        Serial.println("PW center local fine scan timeout (60s)");
        break;
      }

      pwm_set_chan_level(PW_PWM_SLICES[0],
                         pwm_gpio_to_channel(PW_PINS[0]),
                         pw);
      PW[0]     = pw;
      g_lastPWMeasurementRaw = pw;
      delay(30);

      GapMeasurement gm = measure_gap(2);
      if (gm.timedOut) {
        continue;
      }

      double gap = (double)gm.value;
      double gapDiff = gap - gapTarget;
      double absGapDiff = fabs(gapDiff);

      if (absGapDiff <= (double)targetGap) {
        inToleranceCount++;
      }

      if (absGapDiff < bestGap) {
        bestGap = absGapDiff;
        bestCandidate = pw;
      }

      if (validCount < kMaxSamples) {
        validPW[validCount] = pw;
        validGapDiff[validCount] = gapDiff;
        validCount++;
      }
    }
  } else {
    // ---- Phase 2: Bisection search within the bracket ----
    for (int iter = 0; iter < 14; ++iter) {
      if (millis() - DCOCalibrationStart > 60000) {
        Serial.println("PW center bisection timeout (60s)");
        break;
      }

      uint16_t pwMid = (uint16_t)((pwLow + pwHigh) / 2);
      pwm_set_chan_level(PW_PWM_SLICES[0],
                         pwm_gpio_to_channel(PW_PINS[0]),
                         pwMid);
      PW[0]     = pwMid;
      g_lastPWMeasurementRaw = pwMid;
      delay(30);

      GapMeasurement gm = measure_gap(2);
      if (gm.timedOut) {
        // No valid data at this midpoint; skip this iteration and try again
        // on the next loop. Global time/iteration guards will still ensure
        // we eventually stop if there is no usable region.
        if (autotuneDebug >= 2) {
          Serial.println("PW center: timeout during bisection, skipping midpoint.");
        }
        continue;
      }

      double gapMid = (double)gm.value;
      double gapDiffMid = gapMid - gapTarget;
      double absGapDiffMid = abs(gapDiffMid);
      if (absGapDiffMid <= (double)targetGap) {
        inToleranceCount++;
      }

      if (absGapDiffMid < bestGap) {
        bestGap = absGapDiffMid;
        bestCandidate = pwMid;
      }

      if (autotuneDebug >= 2 && periodUs > 0.0) {
        double dutyErrorFrac = -gapMid / (2.0 * periodUs);
        double dutyPercent = (0.5 + dutyErrorFrac) * 100.0;
        Serial.println((String)"[PW_CENTER_BISECT] note=" + DCO_calibration_current_note +
                       (String)" DCO=" + currentDCO +
                       (String)" PW_raw=" + pwMid +
                       (String)" gap=" + gapMid +
                       (String)"us duty=" + dutyPercent +
                       (String)"% target=50% tol≈" + toleranceDutyPercent + "%");
      }

      // Do not early-exit on first in-tolerance sample; we want at least a
      // couple of near-target measurements before deciding, or until the
      // bracket can no longer be refined.

      // Maintain the sign-change bracket.
      if ((gapDiffMid > 0.0 && (gapLow - gapTarget) > 0.0) ||
          (gapDiffMid < 0.0 && (gapLow - gapTarget) < 0.0)) {
        pwLow = pwMid;
        gapLow = gapMid;
      } else {
        pwHigh = pwMid;
        gapHigh = gapMid;
      }

      if (pwHigh - pwLow <= 1) {
        // Can't refine further in integer PW space.
        break;
      }
    }
  }

  // Choose the best PW from the valid samples table. We now:
  //  1) Use the smallest gap to the target as the primary ranking.
  //  2) For each candidate (best to worse), run a lock-in phase that demands
  //     3 consecutive in-band readings at that PW.
  //  3) If all candidates fail lock-in, keep the previous PW_CENTER.
  if (validCount > 0) {
    // Try candidates from best gap to worse, without keeping an explicit
    // rejected list: after each failed lock-in, we mark that candidate by
    // inflating its gap difference so it won't be chosen again.
    for (int attempt = 0; attempt < validCount; ++attempt) {
      int   bestIdx = -1;
      double bestAbs = 1e12;
      int   inTolForThisPass = 0;

      // Find current best candidate and count in-band samples.
      for (int vi = 0; vi < validCount; ++vi) {
        double curAbs = fabs(validGapDiff[vi]);
        if (curAbs <= (double)targetGap) {
          inTolForThisPass++;
        }
        if (curAbs < bestAbs) {
          bestAbs = curAbs;
          bestIdx = vi;
        }
      }

      if (bestIdx < 0) {
        break;
      }

      // If the best gap is still extremely large compared to the allowed gap
      // (e.g. > 10x), abort early and keep the previous PW center. We no
      // longer require a minimum number of in-band coarse samples here,
      // because the lock-in phase will enforce stability.
      if (bestAbs > (double)targetGap * 10.0) {
        if (autotuneDebug >= 1) {
          Serial.println((String)"[PW_CENTER_ABORT] note=" + DCO_calibration_current_note +
                         (String)" DCO=" + currentDCO +
                         (String)" bestGap=" + bestAbs +
                         (String)"us (> " + (double)targetGap * 10.0 +
                         (String)"us); keeping PW_center=" + PW_CENTER[0]);
        }
        return PWCalibrationVal;
      }

      uint16_t chosenPW = validPW[bestIdx];
      // Reconstruct the gap for the chosen sample so we can report its duty.
      double chosenGap = gapTarget + validGapDiff[bestIdx];
      double chosenDutyPercent = 0.0;
      if (periodUs > 0.0) {
        double dutyErrorFrac = -chosenGap / (2.0 * periodUs);
        chosenDutyPercent = (0.5 + dutyErrorFrac) * 100.0;
      }

      // Lock-in phase for this candidate PW:
      bool lockedIn = false;
      int consecutiveOk = 0;
      const int kMaxLockInTries = 8;

      for (int li = 0; li < kMaxLockInTries && !lockedIn; ++li) {
        pwm_set_chan_level(PW_PWM_SLICES[0],
                           pwm_gpio_to_channel(PW_PINS[0]),
                           chosenPW);
        PW[0]     = chosenPW;
        g_lastPWMeasurementRaw = chosenPW;
        delay(30);

        GapMeasurement gmLock = measure_gap(2);
        if (gmLock.timedOut || periodUs <= 0.0) {
          consecutiveOk = 0;
          continue;
        }

        double gapLock = (double)gmLock.value;
        double gapDiffLock = gapLock - gapTarget;
        double absGapDiffLock = fabs(gapDiffLock);

        if (absGapDiffLock <= (double)targetGap) {
          consecutiveOk++;
          if (consecutiveOk >= 3) {
            lockedIn = true;
            chosenGap = gapLock;
            if (periodUs > 0.0) {
              double dutyErrorFrac = -chosenGap / (2.0 * periodUs);
              chosenDutyPercent = (0.5 + dutyErrorFrac) * 100.0;
            }
            break;
          }
        } else {
          consecutiveOk = 0;
        }
      }

      if (lockedIn) {
        // Local refinement: probe a small neighbourhood around the locked-in PW
        // (PW-2..PW+2). For each candidate in this window, we also require a
        // mini lock-in: 3 consecutive measurements within the target gap band
        // at that PW before we consider it.
        uint16_t bestLocalPW = chosenPW;
        double   bestLocalGapAbs = bestAbs;

        int16_t startOffset = -2;
        int16_t endOffset   =  2;
        for (int16_t off = startOffset; off <= endOffset; ++off) {
          int32_t testPW32 = (int32_t)chosenPW + off;
          if (testPW32 < (int32_t)pwMin || testPW32 > (int32_t)pwMax) continue;
          uint16_t testPW = (uint16_t)testPW32;

          bool   localLocked = false;
          int    localConsecutiveOk = 0;
          double gapLocal = 0.0;
          const int kMaxLocalLockInTries = 8;

          for (int lli = 0; lli < kMaxLocalLockInTries && !localLocked; ++lli) {
            pwm_set_chan_level(PW_PWM_SLICES[0],
                               pwm_gpio_to_channel(PW_PINS[0]),
                               testPW);
            PW[0]     = testPW;
            g_lastPWMeasurementRaw = testPW;
            delay(30);

            GapMeasurement gmLocal = measure_gap(2);
            if (gmLocal.timedOut || periodUs <= 0.0) {
              localConsecutiveOk = 0;
              continue;
            }

            gapLocal = (double)gmLocal.value;
            double gapDiffLocal = gapLocal - gapTarget;
            double absGapDiffLocal = fabs(gapDiffLocal);

            if (absGapDiffLocal <= (double)targetGap) {
              localConsecutiveOk++;
              if (localConsecutiveOk >= 3) {
                localLocked = true;
                double dutyErrorFracLocal = -gapLocal / (2.0 * periodUs);
                double dutyPercentLocal = (0.5 + dutyErrorFracLocal) * 100.0;
                (void)dutyPercentLocal; // only used implicitly via bestLocalGapAbs

                if (absGapDiffLocal < bestLocalGapAbs) {
                  bestLocalGapAbs = absGapDiffLocal;
                  bestLocalPW     = testPW;
                  chosenGap       = gapLocal;
                }
                break;
              }
            } else {
              localConsecutiveOk = 0;
            }
          }
        }

        chosenPW = bestLocalPW;
        if (periodUs > 0.0) {
          double dutyErrorFrac = -chosenGap / (2.0 * periodUs);
          chosenDutyPercent = (0.5 + dutyErrorFrac) * 100.0;
        }

        if (autotuneDebug >= 1) {
          Serial.println((String)"[PW_CENTER_RESULT] note=" + DCO_calibration_current_note +
                         (String)" DCO=" + currentDCO +
                         (String)" PW_center=" + chosenPW +
                         (String)" duty≈" + chosenDutyPercent +
                         (String)"% bestGap=" + bestLocalGapAbs +
                         (String)"us inTolSamples=" + inTolForThisPass +
                         (String)" totalValid=" + validCount);
        }
        return chosenPW;
      }

      // This candidate failed lock-in; inflate its gap diff so we try the next
      // best one on the following attempt.
      validGapDiff[bestIdx] = (double)targetGap * 20.0;
      if (autotuneDebug >= 1) {
        Serial.println((String)"[PW_CENTER_LOCKIN_REJECT] note=" + DCO_calibration_current_note +
                       (String)" DCO=" + currentDCO +
                       (String)" PW=" + chosenPW +
                       (String)" could not get 3 consecutive in-band readings; trying next candidate.");
      }
    }

    // If we reach here, no candidate passed lock-in.
    if (autotuneDebug >= 1) {
      Serial.println((String)"[PW_CENTER_ABORT] note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" all candidates failed lock-in; keeping PW_center=" +
                     PW_CENTER[0]);
    }
    return PWCalibrationVal;
  } else {
    // No valid samples at all in the searched range: keep the existing PWCalibrationVal
    // and log the situation so the user can investigate.
    if (autotuneDebug >= 1) {
      Serial.println("PW search: no valid samples found; keeping current PWCalibrationVal.");
    }
    return PWCalibrationVal;
  }
}

// Locate PW center for the current DCO's voice by minimizing duty-cycle error
// at a reference note. Mode 0 = low note, mode 1 = higher note refinement.
void find_PW_center(uint8_t mode) {

  DCO_calibration_current_note = manual_DCO_calibration_start_note;
  VOICE_NOTES[0] = DCO_calibration_current_note;
  ampCompCalibrationVal = initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];

  uint16_t targetGap;
  uint8_t voiceTaskMode;
  

  if (mode == 0) {
    targetGap = compute_gap_tolerance_for_freq(sNotePitches[DCO_calibration_current_note - 12], 0.005);
    voiceTaskMode = 2;
  } else {
    DCO_calibration_current_note = 76;
    VOICE_NOTES[0] = DCO_calibration_current_note;
    targetGap = 5;
    voiceTaskMode = 3;
  }

  currentNoteCalibrationStart = micros();
  DCOCalibrationStart = millis();

  PIDOutputLowerLimit = 0;
  PIDOutputHigherLimit = DIV_COUNTER_PW;

  

  if (firstTuneFlag == true) {
    PW[0] = DIV_COUNTER_PW / 2;
    PWCalibrationVal = DIV_COUNTER_PW / 2;
    PW_CENTER[0] = DIV_COUNTER_PW / 2;
  } else {

    PW[0] = PW_CENTER[0];
    PWCalibrationVal = PW_CENTER[0];
  }
  // Center the starting PW
  pwm_set_chan_level(RANGE_PWM_SLICES[currentDCO],
                     pwm_gpio_to_channel(RANGE_PINS[currentDCO]),
                     PW[0]);

  voice_task_autotune(voiceTaskMode, ampCompCalibrationVal);

  uint16_t centerPW = find_PW_for_target_duty(
    kPWCenterDutyFraction,
    targetGap,
    0,
    DIV_COUNTER_PW
  );
  Serial.println("PW center found !!!");
  update_FS_PWCenter(0, centerPW);
  PW_CENTER[0] = centerPW;

  // Apply the newly found PW center immediately to the hardware so that the
  // effect is visible on the pulse waveform as soon as calibration finishes.
  pwm_set_chan_level(PW_PWM_SLICES[0],
                     pwm_gpio_to_channel(PW_PINS[0]),
                     centerPW);
  PW[0]        = centerPW;
  g_lastPWMeasurementRaw    = centerPW;
}


// -----------------------------------------------------------------------------
// New, more reusable PW limit search implementation (v2)
// -----------------------------------------------------------------------------

PWLimitSearchResult search_PW_limit_from_center(
  uint8_t     voiceIdx,
  uint16_t    centerPW,
  PWLimitDir  dir,
  double      periodUs,
  double      targetDuty
) {
  PWLimitSearchResult result;
  result.ok                  = false;
  result.limitPW             = centerPW;
  result.finalDutyPercent    = -1.0;

  if (periodUs <= 0.0) {
    return result;
  }

  // We deliberately keep the same hard bounds convention as the legacy
  // find_PW_limit() so that behaviour is comparable:
  //  - LOW  side scans from center down to 0
  //  - HIGH side scans from center up to DIV_COUNTER_PW
  uint16_t minPW = (dir == PW_LIMIT_LOW)  ? 0           : centerPW;
  uint16_t maxPW = (dir == PW_LIMIT_LOW)  ? centerPW    : DIV_COUNTER_PW;

  // Coarse step size for scanning from center toward the limit. We re-use
  // the order of magnitude of the original heuristic but express the scan in
  // a more compact, symmetric way.
  uint16_t step = DIV_COUNTER_PW / 64;
  if (step == 0) step = 1;

  bool     haveBest   = false;
  uint16_t bestPW     = centerPW;
  double   bestDelta  = 1e12;
  double   bestDuty   = -1.0;   // duty (0..1) at bestPW when known

  unsigned long searchStartMs = millis();

  // Coarse scan: walk from center toward the requested side, tracking the
  // PW that gets closest to the target duty. We stop when we reach the
  // boundary, run out of time, or find a value within tolerance.
  for (uint16_t pw = centerPW; ; ) {
    if (millis() - searchStartMs > 60000UL) {
      // Safety timeout (same order of magnitude as the legacy implementation).
      break;
    }

    if (pw < minPW) pw = minPW;
    if (pw > maxPW) pw = maxPW;

    pwm_set_chan_level(PW_PWM_SLICES[voiceIdx],
                       pwm_gpio_to_channel(PW_PINS[voiceIdx]),
                       pw);
    PW[voiceIdx]           = pw;
    g_lastPWMeasurementRaw = pw;
    delay(30);

    GapMeasurement gm = measure_gap(2);
    if (!gm.timedOut) {
      double gap           = (double)gm.value;
      double dutyErrorFrac = gap / (2.0 * periodUs);
      double duty          = 0.5 + dutyErrorFrac;

      double delta = fabs(duty - targetDuty);
      if (!haveBest || delta < bestDelta) {
        haveBest  = true;
        bestDelta = delta;
        bestPW    = pw;
        bestDuty  = duty;
      }

      if (autotuneDebug >= 2) {
        const char *scanTag =
          (dir == PW_LIMIT_LOW) ? "[PW_LOW_SCAN_V2]" : "[PW_HIGH_SCAN_V2]";
        Serial.println((String)scanTag +
                       (String)" note=" + DCO_calibration_current_note +
                       (String)" DCO=" + currentDCO +
                       (String)" PW_raw=" + pw +
                       (String)" duty=" + (duty * 100.0) + "%" +
                       (String)" targetDuty=" + (targetDuty * 100.0) + "%");
      }

      // If we are already within tolerance, we can stop the coarse scan early.
      if (delta <= kPWLimitDutyTolerance) {
        break;
      }
    }

    // Step toward the boundary.
    if (dir == PW_LIMIT_LOW) {
      if (pw <= minPW + step) {
        break;
      }
      pw = (uint16_t)(pw - step);
    } else {  // PW_LIMIT_HIGH
      if (pw >= maxPW - step) {
        break;
      }
      pw = (uint16_t)(pw + step);
    }
  }

  if (!haveBest) {
    // Never saw a valid measurement; caller should keep previous limit.
    return result;
  }

  // Fine refinement around bestPW: search with step = 1 in a relatively
  // tight window around the best coarse candidate. This keeps the search
  // local so we do not wander too far from the best-known PW.
  uint16_t refineRadius = step / 2;
  if (refineRadius < 4)  refineRadius = 4;
  if (refineRadius > 32) refineRadius = 32;

  uint16_t startPW;
  if (bestPW > refineRadius) {
    startPW = bestPW - refineRadius;
  } else {
    startPW = minPW;
  }
  // Enforce the same [minPW, maxPW] bounds used in the coarse scan so that
  // the refinement phase never crosses to the other side of center.
  if (startPW < minPW) startPW = minPW;

  uint16_t endPW = bestPW + refineRadius;
  if (endPW > maxPW) {
    endPW = maxPW;
  }

  int consecutiveTimeouts = 0;
  for (uint16_t pw = startPW; pw <= endPW; ++pw) {
    pwm_set_chan_level(PW_PWM_SLICES[voiceIdx],
                       pwm_gpio_to_channel(PW_PINS[voiceIdx]),
                       pw);
    PW[voiceIdx]           = pw;
    g_lastPWMeasurementRaw = pw;
    delay(30);

    GapMeasurement gm = measure_gap(2);
    if (gm.timedOut || periodUs <= 0.0) {
      // If we are stepping deeper into the "edge" side and accumulate several
      // consecutive timeouts, stop refining in that direction to avoid
      // spending a long time in a region with no measurable signal.
      if (gm.timedOut) {
        ++consecutiveTimeouts;
        bool goingDeeperLow  = (dir == PW_LIMIT_LOW)  && (pw < bestPW);
        bool goingDeeperHigh = (dir == PW_LIMIT_HIGH) && (pw > bestPW);
        if ((goingDeeperLow || goingDeeperHigh) && consecutiveTimeouts >= 4) {
          break;
        }
      }
      continue;
    }
    consecutiveTimeouts = 0;

    double gap           = (double)gm.value;
    double dutyErrorFrac = gap / (2.0 * periodUs);
    double duty          = 0.5 + dutyErrorFrac;

    double delta = fabs(duty - targetDuty);
    if (delta < bestDelta) {
      bestDelta = delta;
      bestPW    = pw;
      bestDuty  = duty;
    }
  }

  // Final result: start from the best sample seen during coarse+fine.
  result.ok      = true;
  result.limitPW = bestPW;

  if (bestDuty >= 0.0) {
    result.finalDutyPercent = bestDuty * 100.0;
  }

  // Check whether the target duty is actually reachable within tolerance.
  double currentDutyFrac = result.finalDutyPercent / 100.0;
  if (result.finalDutyPercent <= 0.0 ||
      fabs(currentDutyFrac - targetDuty) > kPWLimitDutyTolerance) {
    // Not within tolerance: push all the way to the hardware boundary for
    // this side and treat that as the "best possible" limit. This matches
    // the specification that the target is considered unreachable only after
    // trying the maximum/minimum PW value.
    uint16_t boundaryPW = (dir == PW_LIMIT_LOW) ? minPW : maxPW;

    pwm_set_chan_level(PW_PWM_SLICES[voiceIdx],
                       pwm_gpio_to_channel(PW_PINS[voiceIdx]),
                       boundaryPW);
    PW[voiceIdx]           = boundaryPW;
    g_lastPWMeasurementRaw = boundaryPW;
    delay(30);

    GapMeasurement gmEdge = measure_gap(2);
    if (!gmEdge.timedOut && periodUs > 0.0) {
      double gap           = (double)gmEdge.value;
      double dutyErrorFrac = gap / (2.0 * periodUs);
      double duty          = 0.5 + dutyErrorFrac;
      result.limitPW        = boundaryPW;
      result.finalDutyPercent = duty * 100.0;
    } else {
      // If even the boundary cannot be measured reliably, we still honour the
      // boundary PW as the limit but leave finalDutyPercent as-is.
      result.limitPW = boundaryPW;
    }
  }

  return result;
}

void find_PW_limit_v2(PWLimitDir dir) {
  uint8_t voiceTaskMode = 2;

  // Configure the calibration context in the same way as the legacy
  // find_PW_limit() so that both implementations are comparable.
  DCO_calibration_current_note = manual_DCO_calibration_start_note;
  VOICE_NOTES[0] = DCO_calibration_current_note;
  ampCompCalibrationVal =
    initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];

  currentNoteCalibrationStart = micros();
  DCOCalibrationStart         = millis();

  PIDOutputLowerLimit = 0;
  PIDOutputHigherLimit = DIV_COUNTER_PW;

  double freqHz   = (double)sNotePitches[DCO_calibration_current_note - 12];
  double periodUs = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;

  uint8_t  voiceIdx = 0;
  uint16_t centerPW = PW_CENTER[voiceIdx];

  // Direction-dependent target duty HIGH (porcentaje en nivel alto).
  //  - Low limit:  kPWLowDutyFraction  (≈ 2% HIGH)
  //  - High limit: kPWHighDutyFraction (≈98% HIGH)
  double targetDuty = (dir == PW_LIMIT_LOW)
                      ? kPWLowDutyFraction
                      : kPWHighDutyFraction;

  // Update global logging context for gap measurements during PW-limit search.
  g_gapLogCurrentPeriodUs    = periodUs;
  g_gapLogTargetDutyFraction = targetDuty;

  // Configure the DCO for PW calibration mode.
  voice_task_autotune(voiceTaskMode, ampCompCalibrationVal);
  delay(100);

  PWLimitSearchResult res =
    search_PW_limit_from_center(voiceIdx, centerPW, dir, periodUs, targetDuty);

  if (!res.ok) {
    if (autotuneDebug >= 1) {
      const char *abortTag =
        (dir == PW_LIMIT_LOW) ? "[PW_LOW_ABORT_NO_SIGNAL_V2]" : "[PW_HIGH_ABORT_NO_SIGNAL_V2]";
      uint16_t keepPW =
        (dir == PW_LIMIT_LOW) ? PW_LOW_LIMIT[voiceIdx] : PW_HIGH_LIMIT[voiceIdx];
      Serial.println((String)abortTag +
                     (String)" note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" keeping_PW=" + keepPW);
    }
    return;
  }

  // Log result and commit it in the same style as the original function.
  double targetDutyPercent =
    (dir == PW_LIMIT_LOW)
      ? (kPWLowDutyFraction * 100.0)
      : ((1.0 - kPWHighDutyFraction) * 100.0);
  double targetHighDutyPercent = kPWHighDutyFraction * 100.0;

  if (autotuneDebug >= 1) {
    const char *resultTag =
      (dir == PW_LIMIT_LOW) ? "[PW_LOW_RESULT_V2]" : "[PW_HIGH_RESULT_V2]";
    Serial.println((String)resultTag +
                   (String)" note=" + DCO_calibration_current_note +
                   (String)" DCO=" + currentDCO +
                   (String)" PW_LIMIT=" + res.limitPW +
                   (String)" duty≈" + res.finalDutyPercent + "%" +
                   (String)" targetDuty=" + targetDutyPercent + "%" +
                   (dir == PW_LIMIT_LOW
                      ? (String)""
                      : (String)" targetHighDuty=" + targetHighDutyPercent + "%"));
  }

  if (dir == PW_LIMIT_LOW) {
    Serial.println("--------------------------------");
    Serial.println("PW low limit (v2) found !!!");
    Serial.println(
      (String)" PW_LIMIT=" + res.limitPW +
      (String)" duty≈" + res.finalDutyPercent + "%" +
      (String)" targetDuty=" + (kPWLowDutyFraction * 100.0) + "%");
    Serial.println("--------------------------------");
    update_FS_PW_Low_Limit(voiceIdx, res.limitPW);
    PW_LOW_LIMIT[voiceIdx] = res.limitPW;
  } else {
    Serial.println("--------------------------------");
    Serial.println("PW high limit (v2) found !!!");
    Serial.println(
      (String)" PW_LIMIT=" + res.limitPW +
      (String)" duty≈" + res.finalDutyPercent + "%" +
      (String)" targetDuty=" + ((1.0 - kPWHighDutyFraction) * 100.0) + "%" +
      (String)" targetHighDuty=" + (kPWHighDutyFraction * 100.0) + "%");
    Serial.println("--------------------------------");
    update_FS_PW_High_Limit(voiceIdx, res.limitPW);
    PW_HIGH_LIMIT[voiceIdx] = res.limitPW;
  }
}

//////////////////////////////////////////////////////////////////////////////
// Measure duty-cycle error on DCO_calibration_pin by timing rising/falling
// edges. Returns 0 when duty is ≈50%, or kGapTimeoutSentinel on timeout.
float find_gap(byte specialMode) {
  if (specialMode == 2) {  // find lowest freq mode
    samplesNumber = 12;
  } else {
    samplesNumber = 6;
  }

  // Estimate ideal period for the current note so we can reject obviously
  // invalid edge intervals (e.g. very short glitches) that do not match the
  // DCO's actual frequency.
  double freqHz = (double)sNotePitches[DCO_calibration_current_note - 12];
  double idealPeriodUs = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;
  double dtMinUs = 0.0;
  double dtMaxUs = 0.0;
  if (idealPeriodUs > 0.0) {
    // Accept any segment between ~1% and ~99% of the ideal period. This covers
    // extreme duty cycles (2%/98%) while rejecting very short/high-frequency
    // glitches that are clearly not the fundamental.
    dtMinUs = idealPeriodUs * 0.01;
    dtMaxUs = idealPeriodUs * 0.99;
    if (dtMinUs < (double)kEdgeDebounceMinUs) {
      dtMinUs = (double)kEdgeDebounceMinUs;
    }
    if (dtMaxUs > (double)kGapTimeoutUs) {
      dtMaxUs = (double)kGapTimeoutUs;
    }
  }

  // Reset edge-timing accumulators and counters at the start of each
  // measurement to avoid leaking partial sums from previous calls.
  pulseCounter         = 0;
  samplesCounter       = 0;
  risingEdgeTimeSum    = 0;
  fallingEdgeTimeSum   = 0;
  edgeDetectionLastVal = 0;

  // Local counters for how many rising/falling segments we actually measured.
  uint16_t risingCount  = 0;
  uint16_t fallingCount = 0;

  edgeDetectionLastTime = micros();

  while (samplesCounter < samplesNumber) {

    bool rawVal = digitalRead(DCO_calibration_pin);
    // Compensate for hardware polarity if needed so that 'val == 1' always
    // represents the same logical DCO level for duty measurements.
    bool val = kGapPolarityInverted ? !rawVal : rawVal;
    microsNow = micros();
    if ((microsNow - edgeDetectionLastTime) > kGapTimeoutUs) {

      pulseCounter = 0;
      samplesCounter = 0;
      DCO_calibration_difference = kGapTimeoutSentinel;
      val = 0;
      edgeDetectionLastVal = 0;

      if (autotuneDebug >= 3) {
        uint16_t pwRaw = g_lastPWMeasurementRaw;
        Serial.println((String)"[GAP_TIMEOUT] note=" + DCO_calibration_current_note +
                       (String)" DCO=" + currentDCO +
                       (String)" PW_raw=" + pwRaw +
                       (String)" ampComp=" + ampCompCalibrationVal);
      }

      microsNow = micros();
      edgeDetectionLastTime = microsNow;

      return kGapTimeoutSentinel;
    }
    if (val != edgeDetectionLastVal) {
      if ((microsNow - edgeDetectionLastTime) >= kEdgeDebounceMinUs) {

        edgeDetectionLastVal = val;

        if (pulseCounter == 1 && val == 0) {
          pulseCounter == 0;
        }
        if (pulseCounter > 2) {
          uint32_t dt = microsNow - edgeDetectionLastTime;
          bool intervalOk = true;
          if (idealPeriodUs > 0.0) {
            // Reject intervals that are incompatible with the ideal period.
            // This prevents very short spurious edges from corrupting the
            // duty measurement at low frequencies.
            if ((double)dt < dtMinUs || (double)dt > dtMaxUs) {
              intervalOk = false;
            }
          }

          if (intervalOk) {
          if (val == 0) {
              fallingEdgeTimeSum += dt;
              fallingCount++;
          } else {
              risingEdgeTimeSum += dt;
              risingCount++;
          }
          samplesCounter++;
          }
        }
        edgeDetectionLastTime = microsNow;
        pulseCounter++;
      }
    }
  }

  if (samplesCounter == samplesNumber) {

    // Compute average low and high segment durations directly from the number
    // of segments we actually accumulated.
    float avgLowUs  = (fallingCount  > 0) ? (float)fallingEdgeTimeSum  / (float)fallingCount  : 0.0f;
    float avgHighUs = (risingCount   > 0) ? (float)risingEdgeTimeSum   / (float)risingCount   : 0.0f;

    // Derived period and direct HIGH-duty estimate based purely on measured
    // low/high portions. Duty cycle is defined in la literatura como el
    // porcentaje de tiempo en nivel ALTO (HIGH) durante un período.
    float measuredPeriodUs = avgLowUs + avgHighUs;
    float dutyMeasuredFrac = (measuredPeriodUs > 0.0f) ? (avgHighUs / measuredPeriodUs) : 0.0f;

    // Positive DCO_calibration_difference now means HIGH segment longer than
    // LOW (duty > 50%); negative means LOW segment longer (duty < 50%).
    // This keeps the relation:
    //   duty_high - 0.5 = DCO_calibration_difference / (2 * periodUs)
    DCO_calibration_difference = avgHighUs - avgLowUs;

    if (autotuneDebug >= 2) {
      // Log raw gap measurement with context: which mode, note/DCO, the
      // current amplitude compensation value, the last PW we explicitly set,
      // and the inferred duty/target duty if a period is available.
      uint16_t pwRaw = g_lastPWMeasurementRaw;

      // Duty estimate using the same "diff vs ideal period" method used by
      // the PW search code.
      double dutyPercentIdeal = 0.0;
      double targetDutyPercent = g_gapLogTargetDutyFraction * 100.0;
      if (g_gapLogCurrentPeriodUs > 0.0) {
        double dutyErrorFrac = (double)DCO_calibration_difference / (2.0 * g_gapLogCurrentPeriodUs);
        dutyPercentIdeal = (0.5 + dutyErrorFrac) * 100.0;
      }

      // Direct duty estimate based only on measured low/high times.
      double dutyPercentMeasured = dutyMeasuredFrac * 100.0;

      Serial.println((String)"[GAP_MEASURE] mode=" + specialMode +
                     (String)" note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" AMP=" + ampCompCalibrationVal +
                     (String)" PW_raw=" + pwRaw +
                     (String)" diff=" + DCO_calibration_difference +
                     (String)" avgLowUs=" + avgLowUs +
                     (String)" avgHighUs=" + avgHighUs +
                     (String)" T_meas=" + measuredPeriodUs +
                     (String)" duty_meas≈" + dutyPercentMeasured + "%" +
                     (String)" duty_ideal≈" + dutyPercentIdeal + "%" +
                     (String)" targetDuty=" + targetDutyPercent + "%");
    }

    
    pulseCounter = 0;
    samplesCounter = 0;
    risingEdgeTimeSum = 0;
    fallingEdgeTimeSum = 0;
    edgeDetectionLastVal = 0;

  } else {
    return kGapTimeoutSentinel;
  }
  return (float)DCO_calibration_difference;
}

/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/

// Debug helper used during manual calibration: measure and report the
// duty-cycle difference from the target duty (normally 50%) for the
// current note/DCO. The result is sent to the Input board as a 32-bit
// PARAM_GAP_FROM_DCO value, which it relays to the screen as "GAP".
void DCO_calibration_debug() {
  // Reuse the main gap-measurement path (which already handles polarity,
  // debouncing, and timeouts) so manual calibration sees the same notion
  // of "gap" as the automatic routines.
  GapMeasurement gm = measure_gap(0);  // target is 50% duty

  // Compute duty error relative to the center target (0.5) using the
  // *ideal* period for the current note. For manual trimming this is
  // sufficient and keeps the math simple.
  double dutyErrorPercentTimes100 = 0.0;  // duty error [%] * 100

  if (!gm.timedOut) {
    double freqHz = (double)sNotePitches[DCO_calibration_current_note - 12];
    if (freqHz > 0.0) {
      double periodUs = 1000000.0 / freqHz;
      // gm.value is the low-vs-high time difference (avgLowUs - avgHighUs).
      // For a perfect 50% duty, low and high are equal, so gm.value == 0.
      // Duty error fraction from 50% is thus:
      //   duty_low - 0.5 = (avgLowUs - avgHighUs) / (2 * periodUs)
      double dutyErrorFrac = (double)gm.value / (2.0 * periodUs);
      double dutyErrorPercent = dutyErrorFrac * 100.0;
      // Scale by 100 for two decimal digits of resolution on the screen.
      dutyErrorPercentTimes100 = dutyErrorPercent * 100.0;
    }
  } else {
    // On timeout, propagate a large sentinel so the UI can tell that the
    // signal is invalid/out of range instead of near 0%.
    dutyErrorPercentTimes100 = 0;  // or some large sentinel if preferred
  }

  if (autotuneDebug >= 1) {
    Serial.println((String)"[MANUAL_GAP] note=" + DCO_calibration_current_note +
                   (String)" DCO=" + currentDCO +
                   (String)" AMP=" + ampCompCalibrationVal +
                   (String)" gapUs=" + gm.value +
                   (String)" dutyErr(%)≈" + (dutyErrorPercentTimes100 / 100.0));
  }

  // Send as a 32-bit PARAM_GAP_FROM_DCO value through the standard
  // param protocol: Serial2 → Input, which relays it to the Screen.
  serialSendParam32(PARAM_GAP_FROM_DCO,
                    (int32_t)dutyErrorPercentTimes100);
}
