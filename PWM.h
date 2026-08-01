#ifndef __PWM_H__
#define __PWM_H__

#ifndef NUM_FILTERS
#define NUM_FILTERS 2
#endif

void init_pwm();
#ifdef ENABLE_CV_OUTS
void init_cv_pwm();
void write_cv_pwm();
void write_cv_pwm_raw(uint16_t cutoff, const uint16_t resonance[NUM_FILTERS], uint16_t vca,
                      uint16_t dist_drive, uint16_t dist_mix);
void init_level_pwm();
void write_level_pwm();
void write_level_pwm_raw(uint16_t osc1, uint16_t osc2, uint16_t osc3, uint16_t sub);
#endif

#endif
