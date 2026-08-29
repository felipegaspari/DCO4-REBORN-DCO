#ifndef __LFO_H__
#define __LFO_H__

// Bipolar Q15 bus (±32767 ≈ ±1.0). mo-lfo always exposes getWaveQ15/setAmplQ15;
// MO_LFO_USE_Q15 is only a preferred-path hint (set here for the sketch TU).
// Live waves are always full-scale Q15 — ctor dacSize does not affect getWaveQ15.
// Depth scales below set musical travel; see docs/LFO.md.
#ifndef MO_LFO_USE_Q15
#define MO_LFO_USE_Q15 1
#endif
#ifndef MO_LFO_SRAM_HOT
#define MO_LFO_SRAM_HOT 1
#endif
#if defined(PICO_RP2350) || defined(ARDUINO_ARCH_RP2350) || defined(__ARM_FP)
  #ifndef FLOAT_ENGINE
  #define FLOAT_ENGINE 1
  #endif
#else
  #ifndef FLOAT_ENGINE
  #define FLOAT_ENGINE 0
  #endif
#endif

#define LFO_SINE_TABLE_BITS 9

// Same isolated-project pattern as ADSR_Bezier (relative _build_libs path).
// mo-lfo has a .cpp — it is compiled via #include in LFO.ino (not sketchbook).
#include "_build_libs/mo-lfo/mo-lfo.h"

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

// =============================================================================
// Depth scales (tune musical travel — live wave is always full-scale Q15)
// =============================================================================
static constexpr int32_t LFO1_PITCH_DEPTH_SCALE = 67200;
static constexpr int32_t LFO2_PITCH_DEPTH_SCALE = 11200;

static constexpr float ADSR_PITCH_MAX_OCTAVES = 2.0f;
static constexpr uint16_t ADSR_PITCH_DEPTH_PANEL_FULL = 511;
static constexpr int32_t DRIFT_PITCH_DEPTH_SCALE = 1000;
static constexpr int32_t DRIFT_PITCH_UNIT_Q24 =
   (int32_t)(0.0000005f * (float)(1 << 24) + 0.5f);

// Q24 to float octave conversion factor (1.0f = 1 Octave)
static constexpr float Q24_TO_OCTAVE_F = 1.0f / 16777216.0f;

static int32_t SRAM_HOT(lfo_pitch_depth_q24)(float amt, int32_t depth_scale) {
  return (int32_t)(amt * (float)depth_scale * (float)(1 << 24) + 0.5f);
}

#if defined(USE_FLOAT_VOICE_TASK)
static float lfo_pitch_depth_f(float amt, int32_t depth_scale) {
  // Converts amt (0..1.0) and depth_scale to Octaves per Q15 unit
  static constexpr float Q24_TO_OCTAVE_PER_Q15 = Q24_TO_OCTAVE_F / 32768.0f;
  return amt * (float)depth_scale * (float)(1 << 24) * Q24_TO_OCTAVE_PER_Q15;
}
#endif

static int32_t SRAM_HOT(applyDepthQ24)(int16_t wave_q15, int32_t depth_q24) {
  const int32_t w = (int32_t)wave_q15;
  const int32_t hi = depth_q24 >> 15;
  const int32_t lo = depth_q24 - (hi << 15);
  return w * hi + ((w * lo) >> 15);
}

// mo-lfo ctor dacSize is ignored on the Q15 path (getWaveQ15 / setAmplQ15).
static constexpr int LFO_DAC_SIZE_UNUSED = 1;



//////////////// LFO instances ////////////////////////////////////////

lfo LFO1_class(LFO_DAC_SIZE_UNUSED);
lfo LFO2_class(LFO_DAC_SIZE_UNUSED);
lfo LFO3_class(LFO_DAC_SIZE_UNUSED);

lfo LFO_DRIFT_CLASS[NUM_OSCILLATORS] = {
  lfo(LFO_DAC_SIZE_UNUSED),
  lfo(LFO_DAC_SIZE_UNUSED),
  lfo(LFO_DAC_SIZE_UNUSED),
  lfo(LFO_DAC_SIZE_UNUSED),
  lfo(LFO_DAC_SIZE_UNUSED),
  lfo(LFO_DAC_SIZE_UNUSED),
  lfo(LFO_DAC_SIZE_UNUSED),
  lfo(LFO_DAC_SIZE_UNUSED)
};

/////////////////////////////////////////////////////////////////
byte LFO_DRIFT_WAVEFORM = 10;
float LFO_DRIFT_SPEED_OFFSET[NUM_OSCILLATORS];
volatile int16_t LFO_DRIFT_LEVEL[NUM_OSCILLATORS];

byte LFO1Waveform = 3;
float LFO1Speed = 50;


#if defined(USE_VOICE_ENGINE_FLOAT) || defined(USE_FLOAT_VOICE_TASK)
volatile float lfo1_pitch_mod_f[LFO1_PITCH_SLOT_COUNT];
volatile float lfo2_pitch_mod_f[LFO2_PITCH_SLOT_COUNT];

float drift_pitch_scale_f = 0.0f;

float LFO1toDCO_f = 0.0f;
float LFO1toOSC1_f = 0.0f;
float LFO1toOSC2_f = 0.0f;
float LFO1toOSC3_f = 0.0f;

float LFO2toOSC2_f = 0.0f;
float LFO2toOSC3_f = 0.0f;
float LFO2toOSC2_coarse_f = 0.0f;
float LFO2toOSC3_coarse_f = 0.0f;
#else
volatile int32_t lfo1_pitch_mod_q24[LFO1_PITCH_SLOT_COUNT];
volatile int32_t lfo2_pitch_mod_q24[LFO2_PITCH_SLOT_COUNT];
int32_t drift_pitch_scale_q24 = 0;

int32_t LFO1toDCO_q24 = 0;
int32_t LFO1toOSC1_q24 = 0;
int32_t LFO1toOSC2_q24 = 0;
int32_t LFO1toOSC3_q24 = 0;

int32_t LFO2toOSC2_q24 = 0;
int32_t LFO2toOSC3_q24 = 0;
int32_t LFO2toOSC2_coarse_q24 = 0;
int32_t LFO2toOSC3_coarse_q24 = 0;
#endif

volatile int16_t LFO1Level;
volatile int16_t LFO2Level;
volatile int16_t LFO3Level;

byte LFO2Waveform = 2;
byte LFO3Waveform = 2;

float LFO2Speed;
float LFO3Speed;

volatile uint16_t LFO2toPW;

uint16_t LFO1SpeedVal;
uint16_t LFO2SpeedVal;
uint16_t LFO3SpeedVal;
uint16_t LFO1toDCOVal;

#if defined(USE_FLOAT_VOICE_TASK)

#else

#endif

void LFO1();
void LFO2();
void LFO3();
void DRIFT_LFOs();

#endif // __LFO_H__