#ifndef __CV_STATE_H__
#define __CV_STATE_H__

/**
 * @file cv_state.h
 * @brief CV Panel, Modulation Routing, Envelope Staging, and Mixer State.
 * 
 * Holds the live runtime and shadow state for analog CV outputs, soft PWM CVs,
 * envelope timing/curve parameters, oscillator wave enables, and mixer levels.
 */

#include <stdint.h>
#include <stdbool.h>

#ifndef NUM_FILTERS
#define NUM_FILTERS 2
#endif

// =============================================================================
// 1. OSCILLATOR WAVE ENABLES (74HC595 -> DG411 MUX)
// =============================================================================

/**
 * @brief Per-oscillator analog waveform enable flags.
 * @details Dimension: [Oscillator 0..2][Waveform 0..2].
 *          - Wave 0: Sawtooth
 *          - Wave 1: Pulse / Square
 *          - Wave 2: Triangle
 */
bool waveEnable[3][3] = {
  { false, false, false },
  { false, false, false },
  { false, false, false }
};

// =============================================================================
// 2. ENVELOPE TIMINGS & RESTART FLAGS (VCA & VCF)
// =============================================================================

/** @brief VCA envelope Attack duration in milliseconds/ticks. */
uint16_t ADSR_VCA_attack = 0;
/** @brief VCA envelope Decay duration in milliseconds/ticks. */
uint16_t ADSR_VCA_decay = 0;
/** @brief VCA envelope Sustain level (0 .. 4095). */
uint16_t ADSR_VCA_sustain = 0;
/** @brief VCA envelope Release duration in milliseconds/ticks. */
uint16_t ADSR_VCA_release = 0;

/** @brief VCF envelope Attack duration in milliseconds/ticks. */
uint16_t ADSR_VCF_attack = 0;
/** @brief VCF envelope Decay duration in milliseconds/ticks. */
uint16_t ADSR_VCF_decay = 0;
/** @brief VCF envelope Sustain level (0 .. 4095). */
uint16_t ADSR_VCF_sustain = 0;
/** @brief VCF envelope Release duration in milliseconds/ticks. */
uint16_t ADSR_VCF_release = 0;

/** @brief Retrigger mode for VCA envelope (true = start from 0, false = analog ramp from current). */
bool VCAADSRRestart = true;

/** @brief Retrigger mode for VCF envelope (true = start from 0, false = analog ramp from current). */
bool VCFADSRRestart = true;

// =============================================================================
// 3. ENVELOPE CURVE SHAPE PRESETS (0 to 7)
// =============================================================================

/** @brief ADSR1 (VCA) Attack curve preset index (Default: 1 - Smooth Exp). */
uint8_t ADSR1AttackCurveVal  = 1;
/** @brief ADSR1 (VCA) Decay curve preset index (Default: 2 - Percussive). */
uint8_t ADSR1DecayCurveVal   = 2;
/** @brief ADSR1 (VCA) Release curve preset index (Default: 1 - Smooth Exp). */
uint8_t ADSR1ReleaseCurveVal = 1;

/** @brief ADSR2 (VCF) Attack curve preset index (Default: 4 - Soft S-Curve). */
uint8_t ADSR2AttackCurveVal  = 4;
/** @brief ADSR2 (VCF) Decay curve preset index (Default: 6 - Rounded). */
uint8_t ADSR2DecayCurveVal   = 6;
/** @brief ADSR2 (VCF) Release curve preset index (Default: 1 - Smooth Exp). */
uint8_t ADSR2ReleaseCurveVal = 1;

/** @brief ADSR3 (DCO) Attack curve preset index (Default: 7 - Linear). */
uint8_t ADSR3AttackCurveVal  = 7;
/** @brief ADSR3 (DCO) Decay curve preset index (Default: 7 - Linear). */
uint8_t ADSR3DecayCurveVal   = 7;
/** @brief ADSR3 (DCO) Release curve preset index (Default: 7 - Linear). */
uint8_t ADSR3ReleaseCurveVal = 7;

// =============================================================================
// 4. FILTER / VCA PANEL VALUES & MODULATION ROUTING
// =============================================================================

/** @brief Base cutoff CV panel value (0 .. 4095). */
uint16_t CUTOFF = 0;

/** @brief Base resonance CV panel value (0 .. 4095). */
uint16_t RESONANCE = 0;

/** @brief Filter envelope depth to VCF cutoff (-4095 .. +4095 bipolar). */
int16_t ADSR2toVCF = 0;

/** @brief LFO2 modulation depth to VCF cutoff (0 .. 4095). */
uint16_t LFO2toVCF = 0;

/** @brief DCO envelope modulation depth to VCA amplitude. */
int16_t ADSR3toVCA = 0;

/** @brief Master VCA initial bias level (0 .. 4095). */
uint16_t VCALevel = 0;

/** @brief LFO1 modulation depth to VCA amplitude (0 .. 4095). */
uint16_t LFO1toVCA = 0;

/** @brief Distortion drive amount (0 .. 4095). */
uint16_t DIST_DRIVE = 0;

/** @brief Distortion dry/wet mix amount (0 = full dry .. 4095 = full wet). */
uint16_t DIST_MIX = 0;

/** @brief AS3320 filter hardware mode selector (e.g. 24dB LP, 12dB LP, BP, HP). */
uint8_t FILTER_MODE = 0;

// =============================================================================
// 5. PRECOMPUTED MODULATION SCALES & DRIFT STATE
// =============================================================================

#ifdef USE_FLOAT_CV_OUTS
/** @brief Floating-point scale for EnvVCF -> Cutoff. */
float ADSR2toVCF_scale = 0.0f;
/** @brief Floating-point scale for LFO2 -> Cutoff. */
float LFO2toVCF_scale = 0.0f;
/** @brief Floating-point scale for LFO1 -> VCA. */
float LFO1toVCA_scale = 0.0f;
/** @brief Floating-point keytracking multiplier. */
float VCFKeytrackModifier = 1.0f;
/** @brief Per-voice keytrack cutoff offset. */
float VCFKeytrackPerVoice[NUM_VOICES_TOTAL];
/** @brief Velocity modulation depth to filter. */
float velocityToVCF = 0;
/** @brief Velocity modulation depth to VCA. */
float velocityToVCA = 0;
/** @brief Per-voice analog drift offset for filter. */
volatile float VCF_DRIFT[NUM_VOICES_TOTAL];
#else
/** @brief Q15 precomputed scale (1.0 = 32768) for EnvVCF -> Cutoff. */
int32_t ADSR2toVCF_scale_q15 = 0;
/** @brief Q15 precomputed scale for LFO2 -> Cutoff. */
int32_t LFO2toVCF_scale_q15 = 0;
/** @brief Q15 precomputed scale for LFO1 -> VCA. */
int32_t LFO1toVCA_scale_q15 = 0;
/** @brief Q15 global keytracking multiplier. */
int32_t VCFKeytrackModifier_q15 = 32768;
/** @brief Q15 per-voice keytrack cutoff offset. */
int32_t VCFKeytrackPerVoice_q15[NUM_VOICES_TOTAL];
/** @brief Q15 velocity modulation depth to filter. */
int32_t velocityToVCF_q15 = 0;
/** @brief Q15 velocity modulation depth to VCA. */
int32_t velocityToVCA_q15 = 0;
/** @brief Analog drift scale factor in Q15. */
int32_t vcf_drift_scale_q15 = 0;
/** @brief Volatile per-voice drift offset buffer for filter cutoff. */
volatile int16_t VCF_DRIFT[NUM_VOICES_TOTAL];
#endif

/** @brief Automatic VCA gain boost enable during high resonance. */
bool RESONANCEAmpCompensation = true;

/** @brief Scaling factor for resonance amplitude compensation. */
int16_t VCAResonanceCompensation = 100;

/** @brief Keyboard pitch tracking amount to filter cutoff. */
int16_t VCFKeytrack = 0;

/** @brief Raw panel parameter for velocity -> VCF. */
int8_t velocityToVCFVal = 0;

/** @brief Raw panel parameter for velocity -> VCA. */
int8_t velocityToVCAVal = 0;

/** @brief Stored note-on MIDI velocity per voice slot. */
uint8_t midi_velocity[NUM_VOICES_TOTAL];

// =============================================================================
// 6. SOFT CV OUTPUT BUFFERS (HARDWARE PWM / DAC TARGETS)
// =============================================================================

/** @brief Computed per-voice VCA CV output level (PWM / DAC counts). */
uint16_t VCA_PWM[NUM_VOICES_TOTAL];

/** @brief Computed per-voice VCF Cutoff CV output level (PWM / DAC counts). */
uint16_t VCF_PWM[NUM_VOICES_TOTAL];

/** @brief Computed per-filter Resonance CV output level after mod summation. */
uint16_t RESONANCE_PWM[NUM_FILTERS] = { 0, 0 };

/** @brief Hardware linearization lookup table for AS2164 VCA response. */
uint16_t AS2164_VCA_linearize_table[4096];

// =============================================================================
// 7. OSCILLATOR & SUB-MIX LEVELS (PWM -> LEVEL VCAs)
// =============================================================================

/**
 * @brief Logarithmic attenuation curve table (0..128 -> 12-bit CV).
 * @details 4095 = Muted / Maximum attenuation, 0 = Full volume / No attenuation.
 */
const uint16_t lin_to_log_128[129] = {
  4095, 2040, 1702, 1593, 1504, 1428, 1363, 1305, 1254, 1207, 
  1164, 1125, 1088, 1054, 1023,  993,  965,  938,  913,  889, 
   866,  844,  823,  803,  784,  765,  748,  730,  714,  698, 
   682,  667,  652,  638,  624,  610,  597,  585,  572,  560, 
   548,  537,  525,  514,  503,  493,  482,  472,  462,  453, 
   443,  434,  424,  415,  407,  398,  389,  381,  373,  365, 
   357,  349,  341,  333,  326,  318,  311,  304,  297,  290, 
   283,  276,  269,  263,  256,  250,  244,  237,  231,  225, 
   219,  213,  207,  201,  195,  190,  184,  179,  173,  168, 
   162,  157,  152,  146,  141,  136,  131,  126,  121,  116, 
   112,  107,  102,   97,   93,   88,   83,   79,   74,   70, 
    65,   61,   57,   52,   48,   44,   40,   36,   32,   27, 
    23,   19,   15,   11,    8,    6,    4,    2,    0
};

/** @brief Raw panel parameter for Oscillator 1 level (0 .. 128). */
int16_t OSC1LevelVal = 0;
/** @brief Raw panel parameter for Oscillator 2 level (0 .. 128). */
int16_t OSC2LevelVal = 0;
/** @brief Raw panel parameter for Oscillator 3 level (0 .. 128). */
int16_t OSC3LevelVal = 0;
/** @brief Raw panel parameter for Sub-oscillator level (0 .. 128). */
int16_t SubLevelVal  = 0;

/** @brief 12-bit CV output level for Oscillator 1 VCA. */
uint16_t OSC1Level = 0;
/** @brief 12-bit CV output level for Oscillator 2 VCA. */
uint16_t OSC2Level = 0;
/** @brief 12-bit CV output level for Oscillator 3 VCA. */
uint16_t OSC3Level = 0;
/** @brief 12-bit CV output level for Sub-oscillator VCA. */
uint16_t SubLevel  = 0;

// =============================================================================
// 8. PRESET & SYSTEM METADATA
// =============================================================================

/** @brief Master toggle enabling DCO pitch/PWM envelope routing. */
bool ADSR3Enabled = false;

/** @brief Space-padded 16-character ASCII string of the active preset name. */
uint8_t presetName[16] = {
  32, 32, 32, 32, 32, 32, 32, 32,
  32, 32, 32, 32, 32, 32, 32, 32
};

#endif // __CV_STATE_H__