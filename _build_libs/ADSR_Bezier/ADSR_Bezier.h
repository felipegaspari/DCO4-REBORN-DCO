#pragma once
// Minimal stub for compile verification when the real ADSR_Bezier library is not installed.
#include <Arduino.h>
#include <stdint.h>

static float _curve_tables[8][512];

inline void adsrBezierInitTables(uint16_t /*cc*/, int /*arraySize*/, float /*tables*/[][512]) {}

class adsr {
public:
  adsr(uint16_t /*cc*/, float /*c1*/, float /*c2*/, bool /*flag*/, int /*a*/, int /*b*/, int /*c*/) {}
  void setAttack(uint16_t) {}
  void setDecay(uint16_t) {}
  void setSustain(uint16_t) {}
  void setRelease(uint16_t) {}
  void setResetAttack(bool) {}
  void noteOn() {}
  void noteOff() {}
  uint16_t getWave() { return 0; }
};
