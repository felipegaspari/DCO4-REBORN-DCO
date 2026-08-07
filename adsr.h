#ifndef __ADSR_H__
#define __ADSR_H__

#define ADSR_1_DACSIZE 4000

#define ARRAY_SIZE 512

#define LIN_TO_EXP_TABLE_SIZE ADSR_1_DACSIZE + 1
uint16_t linToExpLookup[LIN_TO_EXP_TABLE_SIZE];
uint16_t linToLogLookup[LIN_TO_EXP_TABLE_SIZE];
uint16_t maxADSRControlValue = ADSR_1_DACSIZE;

// ADSR Bezier library (provides curve tables and ADSR class).
// Hot path always uses Q24 phase / Q16 amp; Q15 mod tap via levelQ15()/getWaveQ15().
#ifndef ADSR_BEZIER_USE_FLOAT
#define ADSR_BEZIER_USE_FLOAT 0
#endif

#ifndef ADSR_BEZIER_USE_MICROS
#define ADSR_BEZIER_USE_MICROS 1
#endif

#include "_build_libs/ADSR_Bezier/ADSR_Bezier.h"


volatile byte noteStart[NUM_VOICES_TOTAL];
volatile byte noteEnd[NUM_VOICES_TOTAL];

uint16_t ADSR1Level[NUM_VOICES_TOTAL];
uint16_t ADSR_VCA_Level[NUM_VOICES_TOTAL];
uint16_t ADSR_VCF_Level;
uint16_t ADSR_VCF2_Level;
// Q15 mod taps (0..32768); DAC paths keep the u12/u12-ish levels above.
int16_t ADSR1Level_q15[NUM_VOICES_TOTAL];
int16_t ADSR_VCA_Level_q15[NUM_VOICES_TOTAL];
int16_t ADSR_VCF_Level_q15;
int16_t ADSR_VCF2_Level_q15;

static constexpr uint16_t ADSR_1_CC = 4000;
static constexpr uint16_t ADSR_CV_CC = 4095;  // EnvVCA/EnvVCF domain (Mainboard CV scale)

float ADSRMaxLevel = ADSR_1_CC;

uint16_t ADSRMinLevel = 0;

// 0=OSC1, 1=OSC2, 2=OSC1+OSC2, 3=OSC3, 4=all
int8_t ADSR3ToOscSelect = 2;

uint16_t ADSR1_attack = 0;
uint16_t ADSR1_decay;
uint16_t ADSR1_sustain;
uint16_t ADSR1_release;

// Core 0 marks dirty on write; Core 1 applies to voices every ~5 ms.
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

volatile uint16_t adsr_params_dirty = 0;

static inline void mark_adsr_params_dirty(uint16_t mask) {
  adsr_params_dirty |= mask;
}

byte ADSR1_curve2Val = 0;

float ADSR1_curve1 = 0.999;
float ADSR1_curve2 = 0.997;
float ADSR_VCA_curve1 = 0.9995f;
float ADSR_VCA_curve2 = 0.9995f;
float ADSR_VCF_curve1 = 0.997f;
float ADSR_VCF_curve2 = 0.997f;

bool ADSRRestart = true;

int16_t ADSR1toDETUNE1;

float ADSR1toDETUNE1_formula;
int32_t ADSR1toDETUNE1_scale_q24;

int16_t ADSR1toPWM;
// Full-scale PW delta (PWM counts) for (ADSR1Level_q15 * scale) >> 15.
int32_t ADSR1toPWM_scale = 0;
float ADSR1toPWM_formula;
int32_t ADSR1toPWM_formula_q24;

adsr adsr1_voice_0(ADSR_1_CC, ADSR1_curve1, ADSR1_curve2, false, 7, 7, 7);
adsr adsr1_voice_1(ADSR_1_CC, ADSR1_curve1, ADSR1_curve2, false, 7, 7, 7);
adsr adsr1_voice_2(ADSR_1_CC, ADSR1_curve1, ADSR1_curve2, false, 7, 7, 7);

adsr adsr_vca_voice_0(ADSR_CV_CC, ADSR_VCA_curve1, ADSR_VCA_curve2, false, 1, 2, 1);
adsr adsr_vca_voice_1(ADSR_CV_CC, ADSR_VCA_curve1, ADSR_VCA_curve2, false, 1, 2, 1);
adsr adsr_vca_voice_2(ADSR_CV_CC, ADSR_VCA_curve1, ADSR_VCA_curve2, false, 1, 2, 1);

// Shared filter envelopes (VCF1 + VCF2); not per voice.
adsr adsr_vcf_voice(ADSR_CV_CC, ADSR_VCF_curve1, ADSR_VCF_curve2, false, 4, 6, 1);
adsr adsr_vcf2_voice(ADSR_CV_CC, ADSR_VCF_curve1, ADSR_VCF_curve2, false, 4, 6, 1);

struct ADSRStruct {
  adsr adsr1_voice;   // EnvDCO → pitch/PW
  adsr adsr_vca_voice;
};

ADSRStruct ADSRVoices[] = {
  { adsr1_voice_0, adsr_vca_voice_0 },
  { adsr1_voice_1, adsr_vca_voice_1 },
  { adsr1_voice_2, adsr_vca_voice_2 },
};

void init_ADSR();
void ADSR_update();
void ADSR_set_parameters();
void ADSR1_set_restart();
void ADSR_VCA_set_restart();
void ADSR_VCF_set_restart();
void ADSR_VCA_change_attack_curve(uint8_t adsrCurveAttack);
void ADSR_VCA_change_decay_curve(uint8_t adsrCurveDecay);
void ADSR_VCF_change_attack_curve(uint8_t adsrCurveAttack);
void ADSR_VCF_change_decay_curve(uint8_t adsrCurveDecay);

#endif
