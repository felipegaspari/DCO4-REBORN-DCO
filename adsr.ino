/**
 * @file DCO-adsr.ino
 * @brief ADSR Envelope Management, Voice Modulation Routing, and Trigger
 * Engine.
 *
 * Handles per-voice amplitude (VCA) and pitch/PWM (DCO/ADSR3) envelopes, as
 * well as shared paraphonic filter (VCF1/VCF2) envelopes. Implements high-speed
 * interleaved execution (~10 kHz on Core 1) and deferred parameter updating
 * (~200 Hz).
 */

#include "include_all.h"

// =============================================================================
// GLOBAL LEVEL MIRRORS & MODULATION OUTPUTS
// =============================================================================

/** @brief Legacy 12-bit DAC output levels for DCO pitch/PWM envelope (per
 * voice). */
uint16_t ADSR3Level[NUM_VOICES_TOTAL];

/** @brief Legacy 12-bit DAC output levels for VCA amplitude envelope (per
 * voice). */
uint16_t ADSR_VCA_Level[NUM_VOICES_TOTAL];

/** @brief Legacy 12-bit DAC output level for primary filter (VCF1) envelope. */
uint16_t ADSR_VCF_Level[NUM_VOICES_TOTAL];

/** @brief Legacy 12-bit DAC output level for secondary filter (VCF2) envelope.
 */
uint16_t ADSR_VCF2_Level[NUM_VOICES_TOTAL];

/** @brief Primary Q15 modulation output tap (0 .. 32767) for DCO envelope (per
 * voice). */
int16_t ADSR3Level_q15[NUM_VOICES_TOTAL];

/** @brief Primary Q15 modulation output tap (0 .. 32767) for VCA envelope (per
 * voice). */
int16_t ADSR_VCA_Level_q15[NUM_VOICES_TOTAL];

/** @brief Primary Q15 modulation output tap (0 .. 32767) for VCF1 envelope. */
int16_t ADSR_VCF_Level_q15[NUM_VOICES_TOTAL];

/** @brief Primary Q15 modulation output tap (0 .. 32767) for VCF2 envelope. */
int16_t ADSR_VCF2_Level_q15[NUM_VOICES_TOTAL];

/** @brief Volatile Q15 output level read by audio synthesis tasks for DCO
 * envelope. */
volatile int16_t ADSR3Level_q15_volatile[NUM_VOICES_TOTAL];

/** @brief Volatile Q15 output level read by audio synthesis tasks for VCA
 * envelope. */
volatile int16_t ADSR_VCA_Level_q15_volatile[NUM_VOICES_TOTAL];

/** @brief Volatile Q15 output level read by audio synthesis tasks for VCF1
 * envelope. */
volatile int16_t ADSR_VCF_Level_q15_volatile[NUM_VOICES_TOTAL];

/** @brief Volatile Q15 output level read by audio synthesis tasks for VCF2
 * envelope. */
volatile int16_t ADSR_VCF2_Level_q15_volatile[NUM_VOICES_TOTAL];

/** @brief Maximum envelope level reference for calibration/export. */
float ADSRMaxLevel = ADSR_1_CC;

/** @brief Minimum envelope level reference. */
uint16_t ADSRMinLevel = 0;

// =============================================================================
// VOICE TRIGGER FLAGS & PARAPHONIC STATE
// =============================================================================

/** @brief Note-on edge triggers set by MIDI parser (1 = Trigger noteOn, cleared
 * by engine). */
volatile byte noteStart[NUM_VOICES_TOTAL];

/** @brief Note-off edge triggers set by MIDI parser (1 = Trigger noteOff,
 * cleared by engine). */
volatile byte noteEnd[NUM_VOICES_TOTAL];

/** @brief Tracks count of actively held keys for paraphonic filter envelope
 * management. */
volatile uint8_t vcf_active_gates = 0;

/**
 * @brief Current triggering mode for shared filter envelopes.
 * @details Default is VCF_TRIGGER_PARAPHONIC_MULTI (punchy per-note attack,
 * release on last key).
 */
uint8_t vcf_trigger_mode = VCF_TRIGGER_PARAPHONIC_MULTI;

// =============================================================================
// DCO ENVELOPE (ADSR3) PARAMETERS & ROUTING
// =============================================================================

/** @brief Target oscillator routing for DCO envelope: 0=OSC1, 1=OSC2, 2=OSC1+2,
 * 3=OSC3, 4=All. */
int8_t ADSR3ToOscSelect = 2;

/** @brief Attack time in ms (or ticks) for DCO envelope. */
uint16_t ADSR3_attack = 0;

/** @brief Decay time in ms (or ticks) for DCO envelope. */
uint16_t ADSR3_decay = 0;

/** @brief Sustain level (0 .. 4095) for DCO envelope. */
uint16_t ADSR3_sustain = 4095;

/** @brief Release time in ms (or ticks) for DCO envelope. */
uint16_t ADSR3_release = 0;

/** @brief Retrigger mode for DCO envelope (true = start from 0, false = analog
 * ramp from current level). */
bool ADSRRestart = true;

/** @brief Raw pitch modulation depth parameter for DCO envelope. */
int16_t ADSR3toDETUNE1;

/** @brief Precalculated Q24 pitch depth multiplier for DCO envelope modulation.
 */
int32_t ADSR3toDETUNE1_scale_q24;

/** @brief Raw pulse-width modulation depth parameter for DCO envelope (-512 ..
 * +511). */
int16_t ADSR3toPWM;

/** @brief Full-scale pulse-width count delta for DCO envelope modulation. */
int32_t ADSR3toPWM_scale = 0;

/** @brief Bitmask of modified ADSR parameters waiting to be pushed to Core 1
 * voices. */
volatile uint16_t adsr_params_dirty = 0;

// =============================================================================
// ENVELOPE INSTANCE DECLARATIONS
// =============================================================================

/** @brief DCO pitch/PWM envelope generator for Voice 0. */
adsr adsr3_voice_0(ADSR_1_CC, ADSR_CURVE_LINEAR, ADSR_CURVE_LINEAR,
                   ADSR_CURVE_LINEAR);
/** @brief DCO pitch/PWM envelope generator for Voice 1. */
adsr adsr3_voice_1(ADSR_1_CC, ADSR_CURVE_LINEAR, ADSR_CURVE_LINEAR,
                   ADSR_CURVE_LINEAR);
/** @brief DCO pitch/PWM envelope generator for Voice 2. */
adsr adsr3_voice_2(ADSR_1_CC, ADSR_CURVE_LINEAR, ADSR_CURVE_LINEAR,
                   ADSR_CURVE_LINEAR);
/** @brief DCO pitch/PWM envelope generator for Voice 3. */
adsr adsr3_voice_3(ADSR_1_CC, ADSR_CURVE_LINEAR, ADSR_CURVE_LINEAR,
                   ADSR_CURVE_LINEAR);

/** @brief VCA volume envelope generator for Voice 0. */
adsr adsr_vca_voice_0(ADSR_CV_CC, ADSR_CURVE_EXP_SMOOTH, ADSR_CURVE_PERCUSSIVE,
                      ADSR_CURVE_EXP_SMOOTH);
/** @brief VCA volume envelope generator for Voice 1. */
adsr adsr_vca_voice_1(ADSR_CV_CC, ADSR_CURVE_EXP_SMOOTH, ADSR_CURVE_PERCUSSIVE,
                      ADSR_CURVE_EXP_SMOOTH);
/** @brief VCA volume envelope generator for Voice 2. */
adsr adsr_vca_voice_2(ADSR_CV_CC, ADSR_CURVE_EXP_SMOOTH, ADSR_CURVE_PERCUSSIVE,
                      ADSR_CURVE_EXP_SMOOTH);
/** @brief VCA volume envelope generator for Voice 3. */
adsr adsr_vca_voice_3(ADSR_CV_CC, ADSR_CURVE_EXP_SMOOTH, ADSR_CURVE_PERCUSSIVE,
                      ADSR_CURVE_EXP_SMOOTH);

/** @brief Shared primary filter (VCF1) envelope generator. */
adsr adsr_vcf_voice(ADSR_CV_CC, ADSR_CURVE_S_CURVE_SOFT, ADSR_CURVE_ROUNDED,
                    ADSR_CURVE_EXP_SMOOTH);

/** @brief Shared secondary filter (VCF2) envelope generator. */
adsr adsr_vcf2_voice(ADSR_CV_CC, ADSR_CURVE_S_CURVE_SOFT, ADSR_CURVE_ROUNDED,
                     ADSR_CURVE_EXP_SMOOTH);

/** @brief Voice struct pairing per-voice DCO and VCA envelope instances. */
ADSRStruct ADSRVoices[] = {
    {adsr3_voice_0,
     adsr_vca_voice_0},
    {adsr3_voice_1,
     adsr_vca_voice_1},
    {adsr3_voice_2,
     adsr_vca_voice_2},
    {adsr3_voice_3,
     adsr_vca_voice_3},
};

// =============================================================================
// HELPER FUNCTIONS & MODE SETTERS
// =============================================================================

/**
 * @brief Converts panel MIDI/CC sustain level (0 .. panel_full) into native
 * envelope scale.
 *
 * @param panel      Raw incoming panel/CC sustain value (e.g. 0 .. 4095).
 * @param panel_full Full-scale panel reference (typically ADSR_CV_SCALE =
 * 4096).
 * @return int       Sustain level scaled to the active envelope peak domain.
 */
static inline int adsr_sustain_for_set(uint16_t panel, uint16_t panel_full) {
#if ADSR_BEZIER_NATIVE_Q15
  if (panel_full == 0)
    return 0;
  if (panel_full == ADSR_CV_SCALE)
    return (int)(((uint32_t)panel * (uint32_t)ADSR_Q15_PEAK) >> 12);
  return (int)(((uint32_t)panel * (uint32_t)ADSR_Q15_PEAK) /
               (uint32_t)panel_full);
#else
  (void)panel_full;
  return (int)panel;
#endif
}

/**
 * @brief Set the triggering and release behavior mode for shared filter
 * envelopes.
 *
 * ### Mode Options:
 * - `0` / **`VCF_TRIGGER_PARAPHONIC_LEGATO`** : Single-trigger (Legato)
 * - `1` / **`VCF_TRIGGER_PARAPHONIC_MULTI`**  : Multi-trigger (Punchy per-note
 * attack)
 * - `2` / **`VCF_TRIGGER_DIRECT_PER_VOICE`**  : Direct per-voice (Legacy)
 *
 * @param mode Desired trigger mode (0, 1, or 2).
 */
void set_vcf_trigger_mode(uint8_t mode) {
  if (mode > VCF_TRIGGER_DIRECT_PER_VOICE) {
    mode = VCF_TRIGGER_PARAPHONIC_MULTI;
  }
  vcf_trigger_mode = mode;
  vcf_active_gates = 0; // Reset active gate counter
}

// =============================================================================
// SYSTEM INITIALIZATION & MAIN LOOPS
// =============================================================================

/**
 * @brief Boot Initialization. Generates global Bézier tables and assigns
 * initial envelope settings.
 */
void init_ADSR() {
#if ADSR_BEZIER_NATIVE_Q15
  adsrBezierInitTables((float)ADSR_Q15_PEAK, ARRAY_SIZE);
#else
  adsrBezierInitTables((float)ADSR_1_CC, ARRAY_SIZE);
#endif

  vcf_active_gates = 0;

  // Initialize per-voice envelope parameters
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    ADSRVoices[i].adsr3_voice.setAttack(ADSR3_attack);
    ADSRVoices[i].adsr3_voice.setDecay(ADSR3_decay);
    ADSRVoices[i].adsr3_voice.setSustain(
        adsr_sustain_for_set(ADSR3_sustain, ADSR_CV_SCALE));
    ADSRVoices[i].adsr3_voice.setRelease(ADSR3_release);
    ADSRVoices[i].adsr3_voice.setResetAttack(ADSR3Restart);
    ADSRVoices[i].adsr3_voice.setMode(ADSR3Mode);
    
        ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack);
        ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay);
        ADSRVoices[i].adsr_vca_voice.setSustain(adsr_sustain_for_set(ADSR_VCA_sustain, ADSR_CV_SCALE));
        ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
        ADSRVoices[i].adsr_vca_voice.setResetAttack(ADSR1Restart);
  }

  /* DISABLED FOR DCO4
    // Initialize shared filter envelope 1 (VCF1)
    adsr_vcf_voice.setAttack(ADSR_VCF_attack);
    adsr_vcf_voice.setDecay(ADSR_VCF_decay);
    adsr_vcf_voice.setSustain(adsr_sustain_for_set(ADSR_VCF_sustain,
    ADSR_CV_SCALE)); adsr_vcf_voice.setRelease(ADSR_VCF_release);
    adsr_vcf_voice.setResetAttack(ADSR2Restart);

    // Initialize shared filter envelope 2 (VCF2)
    adsr_vcf2_voice.setAttack(ADSR_VCF2_attack);
    adsr_vcf2_voice.setDecay(ADSR_VCF2_decay);
    adsr_vcf2_voice.setSustain(adsr_sustain_for_set(ADSR_VCF2_sustain,
    ADSR_CV_SCALE)); adsr_vcf2_voice.setRelease(ADSR_VCF2_release);
    adsr_vcf2_voice.setResetAttack(ADSR2Restart);
  */
}

/**
 * @brief High-speed envelope generation loop (~10 kHz, Core 1).
 *
 * Processes note-on / note-off edge triggers, computes paraphonic filter gates,
 * and updates envelope output levels. Uses an interleaved 2-phase scheduler
 * (processing half the polyphonic voices per tick) to reduce CPU load.
 */



void SRAM_HOT(ADSR_update)() {
  // Phase toggle: alternate between even and odd voices on successive ticks
  static uint8_t phase = 0;
  phase ^= 1;

  for (int i = phase; i < NUM_VOICES; i += 2) {
#if ADSR_BEZIER_NATIVE_Q15
    ADSR3Level_q15_volatile[i] = (int16_t)ADSRVoices[i].adsr3_voice.getWave();
    ADSR_VCA_Level_q15_volatile[i] =(int16_t)ADSRVoices[i].adsr_vca_voice.getWave();
     /* DISABLED FOR DCO4
    ADSR_VCF2_Level_q15_volatile = (int16_t)adsr_vcf2_voice.getWave();
    */
#else
    ADSR3Level[i] = ADSRVoices[i].adsr3_voice.getWave();
    ADSR3Level_q15_volatile[i] = ADSRVoices[i].adsr3_voice.levelQ15();
    ADSR_VCA_Level[i] = ADSRVoices[i].adsr_vca_voice.getWave();
    ADSR_VCA_Level_q15_volatile[i] = ADSRVoices[i].adsr_vca_voice.levelQ15();
    /* DISABLED FOR DCO4
        ADSR_VCF2_Level = adsr_vcf2_voice.getWave();
    ADSR_VCF2_Level_q15_volatile = adsr_vcf2_voice.levelQ15();
    */
#endif
  }
}

inline void SRAM_HOT(adsr_note_on)(uint8_t voice) {
  ADSRVoices[voice].adsr3_voice.noteOn();
    ADSRVoices[voice].adsr_vca_voice.noteOn();
  //  ADSRVoices[voice].adsr_vcf_voice.noteOn();
}

inline void SRAM_HOT(adsr_note_off)(uint8_t voice) {
  ADSRVoices[voice].adsr3_voice.noteOff();
    ADSRVoices[voice].adsr_vca_voice.noteOff();
  //  ADSRVoices[voice].adsr_vcf_voice.noteOff();
}

/**
 * @brief Periodic parameter update loop (~200 Hz).
 *
 * Flushes dirty envelope timing parameters ($A, D, S, R$) from ingress staging
 * variables to the active envelope instances across all voices.
 */
inline void SRAM_HOT(ADSR_set_parameters)() {
  static uint8_t tick = 0;
  if (++tick < 50)
    return; // Prescale to ~200 Hz
  tick = 0;

  uint16_t ch = adsr_params_dirty;
  if (!ch)
    return;
  adsr_params_dirty = 0;

  // Update DCO Envelope parameters
  if (ch & ADSR_DIRTY_DCO_ALL) {
    const int s = adsr_sustain_for_set(ADSR3_sustain, ADSR_CV_SCALE);
    for (int i = 0; i < NUM_VOICES; i++) {
      if (ch & ADSR_DIRTY_DCO_A)
        ADSRVoices[i].adsr3_voice.setAttack(ADSR3_attack);
      if (ch & ADSR_DIRTY_DCO_D)
        ADSRVoices[i].adsr3_voice.setDecay(ADSR3_decay);
      if (ch & ADSR_DIRTY_DCO_S)
        ADSRVoices[i].adsr3_voice.setSustain(s);
      if (ch & ADSR_DIRTY_DCO_R)
        ADSRVoices[i].adsr3_voice.setRelease(ADSR3_release);
    }
  }
  
    // Update VCA Envelope parameters
    if (ch & ADSR_DIRTY_VCA_ALL) {
      const int s = adsr_sustain_for_set(ADSR_VCA_sustain, ADSR_CV_SCALE);
      for (int i = 0; i < NUM_VOICES; i++) {
        if (ch & ADSR_DIRTY_VCA_A)
    ADSRVoices[i].adsr_vca_voice.setAttack(ADSR_VCA_attack); if (ch &
    ADSR_DIRTY_VCA_D) ADSRVoices[i].adsr_vca_voice.setDecay(ADSR_VCA_decay); if
    (ch & ADSR_DIRTY_VCA_S) ADSRVoices[i].adsr_vca_voice.setSustain(s); if (ch &
    ADSR_DIRTY_VCA_R) ADSRVoices[i].adsr_vca_voice.setRelease(ADSR_VCA_release);
      }
    }
/** DISABLED FOR DCO4
    // Update Shared VCF Envelopes (VCF1 & VCF2)
    if (ch & ADSR_DIRTY_VCF_ALL) {
      const int s = adsr_sustain_for_set(ADSR_VCF_sustain, ADSR_CV_SCALE);
      if (ch & ADSR_DIRTY_VCF_A) {
        adsr_vcf_voice.setAttack(ADSR_VCF_attack);
        adsr_vcf2_voice.setAttack(ADSR_VCF_attack);
      }
      if (ch & ADSR_DIRTY_VCF_D) {
        adsr_vcf_voice.setDecay(ADSR_VCF_decay);
        adsr_vcf2_voice.setDecay(ADSR_VCF_decay);
      }
      if (ch & ADSR_DIRTY_VCF_S) {
        adsr_vcf_voice.setSustain(s);
        adsr_vcf2_voice.setSustain(s);
      }
      if (ch & ADSR_DIRTY_VCF_R) {
        adsr_vcf_voice.setRelease(ADSR_VCF_release);
        adsr_vcf2_voice.setRelease(ADSR_VCF_release);
      }
    }
  */
}

// =============================================================================
// RETRIGGER BEHAVIOR CONFIGURATORS
// =============================================================================

/** @brief Apply DCO envelope retrigger behavior to all active voices. */
void ADSR3_set_restart() {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr3_voice.setResetAttack(ADSR3Restart);
  }
}

/** @brief Apply VCA envelope retrigger behavior to all active voices. */
void ADSR_VCA_set_restart() {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr_vca_voice.setResetAttack(ADSR1Restart);
  }
}

/** @brief Apply VCF1 envelope retrigger behavior. */
void ADSR_VCF_set_restart() {
  //  adsr_vcf_voice.setResetAttack(ADSR2Restart);
}

/** @brief Apply VCF2 envelope retrigger behavior. */
void ADSR_VCF2_set_restart() {
  //  adsr_vcf2_voice.setResetAttack(ADSR2Restart);
}

// =============================================================================
// CURVE SWITCHERS (Optimized via direct pointer remapping)
// =============================================================================

// -----------------------------------------------------------------------------
// VCA Envelope (ADSR1) Curve Switchers
// -----------------------------------------------------------------------------

/**
 * @brief Set the Attack curve shape preset for all VCA envelopes across active
 * voices.
 * @param adsrCurveAttack Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR_VCA_change_attack_curve(uint8_t adsrCurveAttack) {
  for (int i = 0; i < NUM_VOICES; i++) {
     ADSRVoices[i].adsr_vca_voice.adsrCurveAttack(adsrCurveAttack);
  }
}

/**
 * @brief Set the Decay curve shape preset for all VCA envelopes across active
 * voices.
 * @param adsrCurveDecay Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR_VCA_change_decay_curve(uint8_t adsrCurveDecay) {
  for (int i = 0; i < NUM_VOICES; i++) {
     ADSRVoices[i].adsr_vca_voice.adsrCurveDecay(adsrCurveDecay);
  }
}

/**
 * @brief Set the Release curve shape preset for all VCA envelopes across active
 * voices.
 * @param adsrCurveRelease Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR_VCA_change_release_curve(uint8_t adsrCurveRelease) {
  for (int i = 0; i < NUM_VOICES; i++) {
     ADSRVoices[i].adsr_vca_voice.adsrCurveRelease(adsrCurveRelease);
  }
}

// -----------------------------------------------------------------------------
// VCF1 Filter Envelope (ADSR2) Curve Switchers
// -----------------------------------------------------------------------------
/**
 * @brief Set the Attack curve shape preset for the primary filter (VCF1)
 * envelope.
 * @param adsrCurveAttack Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR_VCF_change_attack_curve(uint8_t adsrCurveAttack) {
  //    adsr_vcf_voice.adsrCurveAttack(adsrCurveAttack);
}

/**
 * @brief Set the Decay curve shape preset for the primary filter (VCF1)
 * envelope.
 * @param adsrCurveDecay Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR_VCF_change_decay_curve(uint8_t adsrCurveDecay) {
  //    adsr_vcf_voice.adsrCurveDecay(adsrCurveDecay);
}
/**
 * @brief Set the Release curve shape preset for the primary filter (VCF1)
 * envelope.
 * @param adsrCurveRelease Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR_VCF_change_release_curve(uint8_t adsrCurveRelease) {
  //    adsr_vcf_voice.adsrCurveRelease(adsrCurveRelease);
}

// -----------------------------------------------------------------------------
// VCF2 Secondary Filter Envelope Curve Switchers
// -----------------------------------------------------------------------------
/**
 * @brief Set the Attack curve shape preset for the secondary filter (VCF2)
 * envelope.
 * @param adsrCurveAttack Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR_VCF2_change_attack_curve(uint8_t adsrCurveAttack) {
  //    adsr_vcf2_voice.adsrCurveAttack(adsrCurveAttack);
}
/**
 * @brief Set the Decay curve shape preset for the secondary filter (VCF2)
 * envelope.
 * @param adsrCurveDecay Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR_VCF2_change_decay_curve(uint8_t adsrCurveDecay) {
  //    adsr_vcf2_voice.adsrCurveDecay(adsrCurveDecay);
}
/*
 * @brief Set the Release curve shape preset for the secondary filter (VCF2)
 * envelope.
 * @param adsrCurveRelease Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR_VCF2_change_release_curve(uint8_t adsrCurveRelease) {
  //    adsr_vcf2_voice.adsrCurveRelease(adsrCurveRelease);
}

// -----------------------------------------------------------------------------
// DCO Envelope (ADSR3) Curve Switchers
// -----------------------------------------------------------------------------

/**
 * @brief Set the Attack curve shape preset for all DCO envelopes across active
 * voices.
 * @param adsrCurveAttack Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR3_change_attack_curve(uint8_t adsrCurveAttack) {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr3_voice.adsrCurveAttack(adsrCurveAttack);
  }
}

/**
 * @brief Set the Decay curve shape preset for all DCO envelopes across active
 * voices.
 * @param adsrCurveDecay Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR3_change_decay_curve(uint8_t adsrCurveDecay) {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr3_voice.adsrCurveDecay(adsrCurveDecay);
  }
}

/**
 * @brief Set the Release curve shape preset for all DCO envelopes across active
 * voices.
 * @param adsrCurveRelease Curve preset index (0 to 7) or ADSRCurveType.
 */
void ADSR3_change_release_curve(uint8_t adsrCurveRelease) {
  for (int i = 0; i < NUM_VOICES; i++) {
    ADSRVoices[i].adsr3_voice.adsrCurveRelease(adsrCurveRelease);
  }
}

 // =============================================================================
// MODE SETTERS (Direct mode updates across all 4 voices)
// =============================================================================

void ADSR1_set_mode(uint8_t mode) {
 for (int i = 0; i < NUM_VOICES; i++) ADSRVoices[i].adsr_vca_voice.setMode(mode);
}

void ADSR2_set_mode(uint8_t mode) {
 // for (int i = 0; i < NUM_VOICES; i++) ADSRVoices[i].adsr2_voice.setMode(mode);
}

void ADSR3_set_mode(uint8_t mode) {
  for (int i = 0; i < NUM_VOICES; i++) ADSRVoices[i].adsr3_voice.setMode(mode);
}