#ifndef __PWM_H__
#define __PWM_H__

void init_pwm();
#ifdef ENABLE_CV_OUTS
void init_cv_pwm();
void write_cv_pwm();
void write_cv_pwm_raw(uint16_t cutoff, uint16_t resonance, uint16_t vca,
                      uint16_t dist_drive, uint16_t dist_mix);
#endif

#endif
