#ifndef __PID_H__
#define __PID_H__

#include <PID_v1.h>
#include "autotune_context.h"
#include "autotune_measurement.h"
#include "autotune_constants.h"

// PID controller variables used by legacy calibration routines:
//  - PIDSetpoint: desired error (usually 0).
//  - PIDInput: measured error (from duty/frequency measurements).
//  - PIDOutput: control output (e.g. target frequency or range-PWM).
double PIDSetpoint = 0, PIDInput, PIDOutput;

// Base scaling factor for the active PID gains.
double PIDKMultiplier = 2;
// Conservative PID gains used to construct myPID (SetTunings overrides at runtime).
double consKp = 0.0006 * PIDKMultiplier, consKi = 0.003 * PIDKMultiplier, consKd = 0.000004 * PIDKMultiplier;

// Threshold for how close to the target we need to be (in error units)
// before declaring a calibration step “good enough”.
double PIDMinGap;
// Number of consecutive iterations where the PID gap has been below PIDMinGap.
uint8_t PIDMinGapCounter = 0;

// Best (smallest) gap seen so far and the corresponding candidate PWM.
double bestGap;
uint16_t bestCandidate;


// Dynamic bounds and heuristic “expected” amplitude for the PID output.
double PIDOutputLowerLimit, PIDOutputHigherLimit, PIDLimitsFormula;
// Target interval between PID compute steps (microseconds-based logic in code).
float sampleTime;


// Index into calibrationData[] updated by PID-based calibration.
byte arrayPos;

// Specify the links and initial tuning parameters for the PID controller.
PID myPID(&PIDInput, &PIDOutput, &PIDSetpoint, consKp, consKi, consKd, P_ON_E, REVERSE);

// Main DCO calibration routine (search-based) implemented in PID.ino.
// dutyErrorFraction specifies the allowed duty-cycle error (e.g. 0.005 = 0.5%).
void calibrate_DCO(DCOCalibrationContext& ctx, double dutyErrorFraction);

#endif
