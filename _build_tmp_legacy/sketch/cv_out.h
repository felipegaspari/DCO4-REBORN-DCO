#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/cv_out.h"
#ifndef __CV_OUT_H__
#define __CV_OUT_H__

// Software CV math (Phase 2). Hardware PWM writers arrive in Phase 3.

#ifndef NUM_FILTERS
#define NUM_FILTERS 2
#endif

void init_cv_out();
void update_CV_outs();
void cv_update_mod_formulas();

#endif
