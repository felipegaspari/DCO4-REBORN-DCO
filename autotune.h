#ifndef __AUTOTUNE_H__
#define __AUTOTUNE_H__

#include "include_all.h"
#include "autotune_constants.h"
#include "autotune_measurement.h"
#include "autotune_context.h"

// Global flags controlling calibration routines.
//  - calibrationFlag: a calibration process is currently running.
//  - manualCalibrationFlag: manual calibration mode is active.
//  - firstTuneFlag: true on the very first calibration run after boot/flash.
bool calibrationFlag = false;
bool manualCalibrationFlag = false;
bool firstTuneFlag = false;

// Manual DCO calibration workflow state and per-oscillator manual offsets
// that are added on top of automatic amp compensation.
uint8_t manualCalibrationStage;
int8_t manualCalibrationOffset[NUM_OSCILLATORS] = { 0, 0, 0 };
/************************************************/
/****************** DCO calibration ******************/

// Temporary buffer used during calibration to build [frequency, range-PWM]
// pairs for a single DCO. Persisted via update_FS_voice() when an osc is done.
uint32_t calibrationData[chanLevelVoiceDataSize];

// Index of the DCO currently being calibrated.
uint8_t currentDCO;

// Timing helpers for edge measurement and per-note calibration.
unsigned long edgeDetectionLastTime;
unsigned long microsNow;
unsigned long currentNoteCalibrationStart;
unsigned long DCOCalibrationStart;

// Last digital level read on the calibration pin (for edge detection).
bool edgeDetectionLastVal = 0;

// Current range-PWM value used during calibration for the active DCO.
volatile uint16_t ampCompCalibrationVal;



// Baseline manual amp-comp starting value for all oscillators.
int8_t initManualAmpCompCalibrationValPreset = 35;
// weact rp2040 dco //volatile int8_t initManualAmpCompCalibrationVal[NUM_OSCILLATORS] = {24,26,25,25,25,18,20,25};
// Per-oscillator baseline manual amp-comp starting values.
int8_t initManualAmpCompCalibrationVal[NUM_OSCILLATORS] = {
  initManualAmpCompCalibrationValPreset,
  initManualAmpCompCalibrationValPreset,
  initManualAmpCompCalibrationValPreset
};
// Range-PWM value used when probing for the lowest frequency during calibration.
volatile uint16_t ampCompLowestFreqVal = 10;

// Edge counting and sample accumulation for duty/frequency measurement.
int pulseCounter = 0;
int samplesCounter = 0;

// Accumulated time spent in high/low states during measurement windows.
double risingEdgeTimeSum, fallingEdgeTimeSum;

// Main error metric used by most calibration routines:
//  - For duty calibration: (lowTime - highTime) / scaling factor.
//  - For frequency measurement: (targetFreq - measuredFreq).
float DCO_calibration_difference;

// Number of edge samples to accumulate in the current measurement.
uint16_t samplesNumber;

// Note from which DCO calibration starts (MIDI note index).
static constexpr uint8_t DCO_calibration_start_note = 29; // 29 == C0
// Interval in semitones between successive calibration notes.
static constexpr uint8_t calibration_note_interval = 5;
// Starting note used for manual/PW-centered calibration passes.
static constexpr uint8_t manual_DCO_calibration_start_note = DCO_calibration_start_note - 5;

// Current note/oscillator indexes used during calibration.
uint8_t DCO_calibration_current_note;
uint8_t DCO_calibration_current_OSC;

// Highest reachable note per oscillator (found by highest-frequency search).
uint8_t highestNoteOSC[NUM_OSCILLATORS];

// Current PW value used during PW center/limit calibration.
uint16_t PWCalibrationVal;

// Global debug verbosity level for autotune routines.
byte autotuneDebug = 4;

// Direction selector for unified PW limit search.
enum PWLimitDir {
    PW_LIMIT_LOW,
    PW_LIMIT_HIGH
  };

// Result structure used by the new reusable PW-limit search helpers.
struct PWLimitSearchResult {
  bool     ok;                  // true if at least one valid sample was found
  uint16_t limitPW;             // PW value chosen as limit
  double   finalDutyPercent;    // measured duty at limitPW in percent, or < 0 if unknown
};

// New, more reusable PW-limit calibration helpers. These do *not* replace the
// existing find_PW_limit() yet; they are intended as a clearer alternative
// that can be adopted and iterated on.

// Low-level search routine that assumes the DCO is already configured for
// PW calibration on the desired note/voice. It scans from centerPW toward
// the requested direction and returns the PW that best matches targetDuty.
PWLimitSearchResult search_PW_limit_from_center(
  uint8_t     voiceIdx,
  uint16_t    centerPW,
  PWLimitDir  dir,
  double      periodUs,
  double      targetDuty
);

// High-level wrapper that mirrors the intent of find_PW_limit(), but is built
// on top of the search_PW_limit_from_center() core so that the scanning logic
// is reusable for both LOW and HIGH limits.
void find_PW_limit_v2(PWLimitDir dir);


#endif
