#ifndef __LFO_H__
#define __LFO_H__

#include <mo-lfo.h>  // required for function generation

// After Mainboard absorption, DCO is the sole LFO clock:
//   LFO1 → pitch (volatile Q24 arrays) + EnvVCA depth (LFO1toVCA)
//   LFO2 → OSC2/3 pitch (fine+coarse folded) + PW + EnvVCF depth (LFO2toVCF)
//   drift LFOs → pitch drift + soft VCF_DRIFT
// Do not run a second LFO engine on Mainboard in hub mode (Phase 5 archives it).

enum Lfo1PitchSlot : uint8_t {
  LFO1_PITCH_OSC1 = 0,
  LFO1_PITCH_OSC2 = 1,
  LFO1_PITCH_OSC3 = 2,
  LFO1_PITCH_SLOT_COUNT = 3,
};

enum Lfo2PitchSlot : uint8_t {
  LFO2_PITCH_OSC2 = 0,
  LFO2_PITCH_OSC3 = 1,
  LFO2_PITCH_SLOT_COUNT = 2,
};

//static constexpr uint16_t PWM_CC = 4096;
static constexpr uint16_t LFO1_CC = 3400;
static constexpr uint16_t LFO1_CC_HALF = LFO1_CC / 2;
static constexpr uint16_t LF01_CC_THIRD = LFO1_CC / 3;
static constexpr uint16_t LFO2_CC = 1024;
static constexpr uint16_t LFO2_CC_HALF = LFO2_CC / 2;

static constexpr uint16_t LFO_DRIFT_CC = 2000;
static constexpr uint16_t LFO_DRIFT_CC_HALF = LFO_DRIFT_CC / 2;

//////////////// LFO ian ////////////////////////////////////////

lfo LFO1_class(LFO1_CC + 1);
lfo LFO2_class(LFO2_CC);

lfo LFO_DRIFT_CLASS[NUM_OSCILLATORS] = {
  lfo(LFO_DRIFT_CC),
  lfo(LFO_DRIFT_CC),
  lfo(LFO_DRIFT_CC)
};


/////////////////////////////////////////////////////////////////
byte LFO_DRIFT_WAVEFORM = 2;
float LFO_DRIFT_SPEED_OFFSET[NUM_OSCILLATORS];
float LFO_DRIFT_SPEED = 0.6;
// Drift LFO levels as bipolar Q15 (Core0 write / Core1 read). Polarity matches legacy (half - wave).
volatile int16_t LFO_DRIFT_LEVEL[NUM_OSCILLATORS];


// LFO1/LFO2 levels as bipolar Q15 (±32768 ≈ ±1.0). Core0 write / Core1 CV+matrix+PW read.
volatile int16_t LFO1Level;
byte LFO1Waveform = 3;
float LFO1Speed = 50;
float LFO1toDCO = 0;
// Full-scale octave travel in Q24: (LFO*_Level_q15 * depth_q24) >> 15.
// Param apply multiplies legacy amt by the old CC peak so musical depth is unchanged.
int32_t LFO1toDCO_q24 = 0;
// Additive per-osc LFO1 pitch depths (each stacks on LFO1toDCO_q24 in LFO1()).
int32_t LFO1toOSC1_q24 = 0;
int32_t LFO1toOSC2_q24 = 0;
int32_t LFO1toOSC3_q24 = 0;
// Core 0 writes, Core 1 reads. Q24 log-frequency additive pitch modifiers.
volatile int32_t lfo1_pitch_mod_q24[LFO1_PITCH_SLOT_COUNT];
volatile int32_t lfo2_pitch_mod_q24[LFO2_PITCH_SLOT_COUNT];
int16_t LFO1toDETUNE1;
uint16_t LFO1toDETUNE2;

volatile int16_t LFO2Level;
byte LFO2Waveform;
float LFO2Speed;
// Per-osc LFO2 fine pitch depths (OSC2/3; param 0..255).
int32_t LFO2toOSC2_q24 = 0;
int32_t LFO2toOSC3_q24 = 0;
// Coarse LFO2 pitch depths (0..511 param; depth pre-scaled to LFO1-equivalent travel).
int32_t LFO2toOSC2_coarse_q24 = 0;
int32_t LFO2toOSC3_coarse_q24 = 0;
volatile uint16_t LFO2toPW;

uint16_t LFO1SpeedVal;
uint16_t LFO2SpeedVal;
uint16_t LFO1toDCOVal;
uint16_t LFO2toVCFVal;

volatile float LFO2toPWM_formula;
volatile int32_t LFO2toPWM_formula_q24;

void LFO1();
void LFO2();


#endif
