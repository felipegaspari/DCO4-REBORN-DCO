#ifndef __WAVE_MUX_H__
#define __WAVE_MUX_H__

void init_waveSelector();
// Rebuild all 9 OSC×wave bits from waveEnable[][] and shift out.
void update_waveSelector();
// Manual cal: all off, then enable OSC{stage} Saw only (stage 0..2).
void waveSelector_manual_calibration(byte stage);

#endif
