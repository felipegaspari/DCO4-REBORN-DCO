#ifndef __ADSR_H__
#define __ADSR_H__


// =================================================================
// ADSR BEZIER CONFIGURATION MACROS
// =================================================================
#define ADSR_1_DACSIZE 4096
#define ARRAY_SIZE 512

#ifndef ADSR_BEZIER_PHASE_SHIFT
#define ADSR_BEZIER_PHASE_SHIFT 22
#endif

#ifndef ADSR_BEZIER_USE_FLOAT
#if defined(PICO_RP2350)
#define ADSR_BEZIER_USE_FLOAT 1
#else
#define ADSR_BEZIER_USE_FLOAT 0
#endif
#endif

#ifndef ADSR_BEZIER_USE_MICROS
#define ADSR_BEZIER_USE_MICROS 1
#endif

#ifndef ADSR_BEZIER_NATIVE_Q15
#define ADSR_BEZIER_NATIVE_Q15 1
#endif

#ifndef ADSR_BEZIER_Q15_DYADIC
#define ADSR_BEZIER_Q15_DYADIC 1
#endif

#ifndef ADSR_BEZIER_UPDATE_Q15_CACHE
#define ADSR_BEZIER_UPDATE_Q15_CACHE 1
#endif

#ifndef ADSR_BEZIER_SRAM_HOT
#define ADSR_BEZIER_SRAM_HOT 1
#endif

//------------------------------------------------------------------//
// ADSR Envelope Subsystem & Voice Routing
// Manages per-voice DCO/VCA envelopes and shared VCF paraphonic envelopes.
//------------------------------------------------------------------//

#include "_build_libs/ADSR_Bezier/ADSR_Bezier.h"
// =================================================================
// EXTERNAL VARIABLES (from cv_state.h)
// =================================================================
extern uint16_t ADSR_VCA_attack;
extern uint16_t ADSR_VCA_decay;
extern uint16_t ADSR_VCA_sustain;
extern uint16_t ADSR_VCA_release;

extern uint16_t ADSR_VCF_attack;
extern uint16_t ADSR_VCF_decay;
extern uint16_t ADSR_VCF_sustain;
extern uint16_t ADSR_VCF_release;

extern bool ADSR1Restart;
extern bool ADSR2Restart;
extern bool ADSR3Restart;

// =================================================================
// TRIGGER & LEVEL ARRAYS
// =================================================================
extern volatile byte noteStart[NUM_VOICES_TOTAL];
extern volatile byte noteEnd[NUM_VOICES_TOTAL];

/** @brief Paraphonic gate tracker for shared filter envelopes */
extern volatile uint8_t vcf_active_gates;

// Legacy u12/DAC mirrors
extern uint16_t ADSR3Level[NUM_VOICES_TOTAL];
extern uint16_t ADSR_VCA_Level[NUM_VOICES_TOTAL];
extern uint16_t ADSR_VCF_Level[NUM_VOICES_TOTAL];
extern uint16_t ADSR_VCF2_Level[NUM_VOICES_TOTAL];

// Primary mod taps (0..ADSR_Q15_ONE)
extern int16_t ADSR3Level_q15[NUM_VOICES_TOTAL];
extern int16_t ADSR_VCA_Level_q15[NUM_VOICES_TOTAL];
extern int16_t ADSR_VCF_Level_q15[NUM_VOICES_TOTAL];
extern int16_t ADSR_VCF2_Level_q15[NUM_VOICES_TOTAL];

extern volatile int16_t ADSR3Level_q15_volatile[NUM_VOICES_TOTAL];
extern volatile int16_t ADSR_VCA_Level_q15_volatile[NUM_VOICES_TOTAL];
extern volatile int16_t ADSR_VCF_Level_q15_volatile[NUM_VOICES_TOTAL];;
extern volatile int16_t ADSR_VCF2_Level_q15_volatile[NUM_VOICES_TOTAL];;

// =================================================================
// SCALES & CV LIMITS
// =================================================================
static constexpr uint16_t ADSR_1_CC = 4095;
static constexpr uint16_t ADSR_CV_CC = 4095;
static constexpr uint16_t ADSR_CV_SCALE = 4096;

extern float ADSRMaxLevel;
extern uint16_t ADSRMinLevel;

// =================================================================
// ENV 3 (DCO) ROUTING VARIABLES
// =================================================================
extern int8_t ADSR3ToOscSelect; // 0=OSC1, 1=OSC2, 2=OSC1+2, 3=OSC3, 4=all

extern uint16_t ADSR3_attack;
extern uint16_t ADSR3_decay;
extern uint16_t ADSR3_sustain;
extern uint16_t ADSR3_release;
extern bool     ADSR3Restart;

extern int16_t ADSR3toDETUNE1;
extern int32_t ADSR3toDETUNE1_scale_q24;
extern int16_t ADSR3toPWM;
extern int32_t ADSR3toPWM_scale;

uint8_t ADSR1Mode = 0;
uint8_t ADSR2Mode = 0;
uint8_t ADSR3Mode = 0;

// =================================================================
// DIRTY FLAGS (Core 0 write -> Core 1 apply)
// =================================================================
#define ADSR_DIRTY_DCO_A   (1u << 0)
#define ADSR_DIRTY_DCO_D   (1u << 1)
#define ADSR_DIRTY_DCO_S   (1u << 2)
#define ADSR_DIRTY_DCO_R   (1u << 3)
#define ADSR_DIRTY_VCA_A   (1u << 4)
#define ADSR_DIRTY_VCA_D   (1u << 5)
#define ADSR_DIRTY_VCA_S   (1u << 6)
#define ADSR_DIRTY_VCA_R   (1u << 7)
#define ADSR_DIRTY_VCF_A   (1u << 8)
#define ADSR_DIRTY_VCF_D   (1u << 9)
#define ADSR_DIRTY_VCF_S   (1u << 10)
#define ADSR_DIRTY_VCF_R   (1u << 11)

#define ADSR_DIRTY_DCO_ALL (ADSR_DIRTY_DCO_A | ADSR_DIRTY_DCO_D | ADSR_DIRTY_DCO_S | ADSR_DIRTY_DCO_R)
#define ADSR_DIRTY_VCA_ALL (ADSR_DIRTY_VCA_A | ADSR_DIRTY_VCA_D | ADSR_DIRTY_VCA_S | ADSR_DIRTY_VCA_R)
#define ADSR_DIRTY_VCF_ALL (ADSR_DIRTY_VCF_A | ADSR_DIRTY_VCF_D | ADSR_DIRTY_VCF_S | ADSR_DIRTY_VCF_R)

extern volatile uint16_t adsr_params_dirty;

/** @brief Flag parameters for Core 1 deferred update */
static inline void SRAM_HOT(mark_adsr_params_dirty)(uint16_t mask) {
  adsr_params_dirty |= mask;
}

/**
 * @enum VcfTriggerMode
 * @brief Selects the triggering and release behavior for shared filter envelopes.
 */
 enum VcfTriggerMode : uint8_t {
  /** @brief Single-trigger: Attack on 1st note only; Release when all notes released. */
  VCF_TRIGGER_PARAPHONIC_LEGATO = 0,

  /** @brief Multi-trigger: Attack on EVERY new note; Release when all notes released. */
  VCF_TRIGGER_PARAPHONIC_MULTI  = 1,

  /** @brief Direct: Raw trigger on any noteStart/noteEnd (Legacy behavior). */
  VCF_TRIGGER_DIRECT_PER_VOICE  = 2
};

/** @brief Active trigger mode for shared filter envelopes (Default: Paraphonic Multi-Trigger) */
extern uint8_t vcf_trigger_mode;

/**
* @brief Set the triggering mode for the shared filter envelopes.
* 
* ### Mode Options:
* - `0` / **`VCF_TRIGGER_PARAPHONIC_LEGATO`** : Single-trigger (Legato)
* - `1` / **`VCF_TRIGGER_PARAPHONIC_MULTI`**  : Multi-trigger (Punchy per-note attack)
* - `2` / **`VCF_TRIGGER_DIRECT_PER_VOICE`**  : Direct per-voice (Legacy)
* 
* @param mode Trigger mode index (0, 1, or 2) or VcfTriggerMode.
*/
void set_vcf_trigger_mode(uint8_t mode);
// =================================================================
// ADSR INSTANCES
// =================================================================
SRAM_DATA extern adsr adsr3_voice_0;
SRAM_DATA extern adsr adsr3_voice_1;
SRAM_DATA extern adsr adsr3_voice_2;
SRAM_DATA extern adsr adsr3_voice_3;

/** @brief Per-voice VCA envelopes */
/* DISABLED FOR DCO4 */
/*
extern adsr adsr_vca_voice_0;
extern adsr adsr_vca_voice_1;
extern adsr adsr_vca_voice_2;
extern adsr adsr_vca_voice_3;
*/

/** @brief Per-voice VCF envelopes */
/* DISABLED FOR DCO4 */
/*
extern adsr adsr_vcf_voice;
extern adsr adsr_vcf2_voice;
*/
/** @brief Groups per-voice envelopes for array iteration */
struct ADSRStruct {
  adsr& adsr3_voice;   // EnvDCO → pitch/PW
//  adsr& adsr_vca_voice;// EnvVCA → volume
};

SRAM_DATA extern ADSRStruct ADSRVoices[];

// =================================================================
// FUNCTION PROTOTYPES
// =================================================================
void init_ADSR();
void SRAM_HOT(ADSR_update)();
void SRAM_HOT(ADSR_set_parameters)();

void ADSR3_set_restart();
void ADSR_VCA_set_restart();
void ADSR_VCF_set_restart();
void ADSR_VCF2_set_restart();

void ADSR_VCA_change_attack_curve(uint8_t adsrCurveAttack);
void ADSR_VCA_change_decay_curve(uint8_t adsrCurveDecay);
void ADSR_VCA_change_release_curve(uint8_t adsrCurveRelease);

void ADSR_VCF_change_attack_curve(uint8_t adsrCurveAttack);
void ADSR_VCF_change_decay_curve(uint8_t adsrCurveDecay);
void ADSR_VCF_change_release_curve(uint8_t adsrCurveRelease);

void ADSR_VCF2_change_attack_curve(uint8_t adsrCurveAttack);
void ADSR_VCF2_change_decay_curve(uint8_t adsrCurveDecay);
void ADSR_VCF2_change_release_curve(uint8_t adsrCurveRelease);

/**
 * @brief Set the Attack curve shape for all DCO pitch/PWM envelopes (ADSR3).
 * 
 * ### Curve Options:
 * - `0` / **`ADSR_CURVE_EXP_NATURAL`**   : "Natural Exp" (Classic analog ramp)
 * - `1` / **`ADSR_CURVE_EXP_SMOOTH`**    : "Smooth Exp" (Soft exponential knee)
 * - `2` / **`ADSR_CURVE_PERCUSSIVE`**    : "Percussive" (Sharp punchy drop)
 * - `3` / **`ADSR_CURVE_LOG_CONVEX`**    : "Log / Convex" (High sustain before drop)
 * - `4` / **`ADSR_CURVE_S_CURVE_SOFT`**  : "Soft S-Curve" (Smooth sigmoidal)
 * - `5` / **`ADSR_CURVE_S_CURVE_STEEP`** : "Steep S-Curve" (Aggressive inflection)
 * - `6` / **`ADSR_CURVE_ROUNDED`**       : "Rounded" (Gentle convex slope)
 * - `7` / **`ADSR_CURVE_LINEAR`**        : "Linear" (Constant straight ramp)
 * 
 * @param adsrCurveAttack Curve preset index (0 to 7) or ADSRCurveType.
 */
 void ADSR3_change_attack_curve(uint8_t adsrCurveAttack);

 /**
  * @brief Set the Decay curve shape for all DCO pitch/PWM envelopes (ADSR3).
  * @param adsrCurveDecay Curve preset index (0 to 7) or ADSRCurveType.
  */
 void ADSR3_change_decay_curve(uint8_t adsrCurveDecay);
 
 /**
  * @brief Set the Release curve shape for all DCO pitch/PWM envelopes (ADSR3).
  * @param adsrCurveRelease Curve preset index (0 to 7) or ADSRCurveType.
  */
 void ADSR3_change_release_curve(uint8_t adsrCurveRelease);
 
#endif