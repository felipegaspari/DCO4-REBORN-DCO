#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/adsr.h"
#ifndef __ADSR_H__
#define __ADSR_H__

#define ADSR_1_DACSIZE 4000

#define ARRAY_SIZE 512

#define LIN_TO_EXP_TABLE_SIZE ADSR_1_DACSIZE + 1
uint16_t linToExpLookup[LIN_TO_EXP_TABLE_SIZE];
uint16_t linToLogLookup[LIN_TO_EXP_TABLE_SIZE];
uint16_t maxADSRControlValue = ADSR_1_DACSIZE;

// ADSR Bezier library (provides curve tables and ADSR class)
#include <ADSR_Bezier.h>

volatile byte noteStart[NUM_VOICES_TOTAL];
volatile byte noteEnd[NUM_VOICES_TOTAL];

uint16_t ADSR1Level[NUM_VOICES_TOTAL];
uint16_t ADSR_VCA_Level[NUM_VOICES_TOTAL];
uint16_t ADSR_VCF_Level[NUM_VOICES_TOTAL];

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

byte ADSR1_curve2Val = 0;

float ADSR1_curve1 = 0.999;
float ADSR1_curve2 = 0.997;
float ADSR_VCA_curve1 = 0.9995f;
float ADSR_VCA_curve2 = 0.9995f;
float ADSR_VCF_curve1 = 0.997f;
float ADSR_VCF_curve2 = 0.997f;

unsigned long tADSR;
unsigned long tADSR_params;

bool ADSRRestart = true;

int16_t ADSR1toDETUNE1;

float ADSR1toDETUNE1_formula;
int32_t ADSR1toDETUNE1_scale_q24;

int16_t ADSR1toPWM;
float ADSR1toPWM_formula;
int32_t ADSR1toPWM_formula_q24;

adsr adsr1_voice_0(ADSR_1_CC, ADSR1_curve1, ADSR1_curve2, false, 7, 7, 7);
adsr adsr_vca_voice_0(ADSR_CV_CC, ADSR_VCA_curve1, ADSR_VCA_curve2, false, 1, 2, 1);
adsr adsr_vcf_voice_0(ADSR_CV_CC, ADSR_VCF_curve1, ADSR_VCF_curve2, false, 4, 6, 1);

struct ADSRStruct {
  adsr adsr1_voice;   // EnvDCO → pitch/PW
  adsr adsr_vca_voice;
  adsr adsr_vcf_voice;
};

ADSRStruct ADSRVoices[] = {
  { adsr1_voice_0, adsr_vca_voice_0, adsr_vcf_voice_0 },
};

void init_ADSR();
void ADSR_update();
void ADSR_set_parameters();
void ADSR1_set_restart();
void ADSR_VCA_set_restart();
void ADSR_VCF_set_restart();

#endif
