#ifndef __PWM_H__
#define __PWM_H__

void init_pwm();
#ifdef ENABLE_CV_OUTS
void init_cv_pwm();
void write_cv_pwm();
#endif

#endif
