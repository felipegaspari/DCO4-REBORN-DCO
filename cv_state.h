#ifndef __CV_STATE_H__
#define __CV_STATE_H__

// Mainboard-local CV / panel state absorbed onto DCO.
// Phase 2: EnvVCA/EnvVCF engines + soft CV levels (VCA_PWM/VCF_PWM).
// Phase 3: PWM / MCP4728 / 74HC595 writers.

#ifndef NUM_FILTERS
#define NUM_FILTERS 2
#endif

// --- Wave mux statuses (74HC595 in Phase 3) ---------------------------------
bool sawStatus = 0;
bool saw2Status = 0;
bool triStatus = 0;
bool sineStatus = 0;
bool sqr2Status = 0;
// sqr1Status lives in voices.h (already used by voice_task)

// --- EnvVCA / EnvVCF times --------------------------------------------------
uint16_t ADSR_VCA_attack = 0;
uint16_t ADSR_VCA_decay = 0;
uint16_t ADSR_VCA_sustain = 0;
uint16_t ADSR_VCA_release = 0;

uint16_t ADSR_VCF_attack = 0;
uint16_t ADSR_VCF_decay = 0;
uint16_t ADSR_VCF_sustain = 0;
uint16_t ADSR_VCF_release = 0;

bool VCAADSRRestart = true;
bool VCFADSRRestart = true;

uint8_t ADSR1AttackCurveVal = 0;
uint8_t ADSR1DecayCurveVal = 0;
uint8_t ADSR2AttackCurveVal = 0;
uint8_t ADSR2DecayCurveVal = 0;

// --- Filter / VCA CV panel values ------------------------------------------
uint16_t CUTOFF = 0;
uint16_t RESONANCE = 0;
int16_t ADSR2toVCF = 0;
uint16_t LFO2toVCF = 0;
int16_t ADSR1toVCA = 0;
uint16_t VCALevel = 0;
uint16_t LFO1toVCA = 0;

float ADSR2toVCF_formula = 0.0f;
float LFO2toVCF_formula = 0.0f;
float LFO1toVCA_formula = 0.0f;

bool RESONANCEAmpCompensation = true;
int16_t VCAResonanceCompensation = 100;
int16_t VCFKeytrack = 0;
float VCFKeytrackModifier = 1.0f;
float VCFKeytrackPerVoice[NUM_VOICES_TOTAL];
float velocityToVCF = 0;
float velocityToVCA = 0;
int8_t velocityToVCFVal = 0;
int8_t velocityToVCAVal = 0;

uint8_t midi_velocity[NUM_VOICES_TOTAL];

// Soft CV outs (Phase 3 maps these to PWM / DAC)
uint16_t VCA_PWM[NUM_VOICES_TOTAL];
uint16_t VCF_PWM[NUM_VOICES_TOTAL];
uint16_t RESONANCE_PWM = 0;
volatile float VCF_DRIFT[NUM_VOICES_TOTAL];

uint16_t AS2164_VCA_linearize_table[4096];

// --- SQR / Sub DAC levels (MCP4728 in Phase 3) -----------------------------
int16_t SQR1LevelVal = 0;
int16_t SQR2LevelVal = 0;
int16_t SubLevelVal = 0;
uint16_t SQR1Level = 0;
uint16_t SQR2Level = 0;
uint16_t SubLevel = 0;

bool ADSR3Enabled = false;

uint8_t presetName[12] = { 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32 };

#endif

