
// Linear → logarithmic mapping (0..maxValue). Used by init_ADSR for linToLogLookup.
uint16_t linearToLogarithmic(uint16_t linearValue, float base, uint16_t maxValue) {
  if (linearValue < 0) linearValue = 0;
  if (linearValue > maxValue) linearValue = maxValue;

  float normalizedValue = (float)linearValue / (float)maxValue;
  float logValue = log(normalizedValue * (base - 1) + 1) / log(base);
  float maxLogValue = log(1 + (base - 1)) / log(base);
  uint16_t scaledLogValue = (uint16_t)(logValue * ((float)maxValue / maxLogValue));

  return scaledLogValue;
}

// Linear 0..4095 → exponential 0..maxValue. Same curve the Input board applies to the
// envelope A/D/R faders before sending them (auxiliary.h / linToExpLookup), so a MIDI CC
// lands in the exp domain the 'a'-'c' block frames carry.
uint16_t linearToExponential(uint16_t linearValue, float base, uint16_t maxValue) {
  if (linearValue > 4095) linearValue = 4095;

  float normalizedValue = (float)linearValue / 4095.0f;
  float expValue = pow(base, normalizedValue) - 1.0f;
  float maxExpValue = base - 1.0f;

  return (uint16_t)(expValue * ((float)maxValue / maxExpValue));
}

// Exp curve → float (x^2 / curve). Used by params/LFO drift and related control mapping.
float expConverterFloat(uint16_t readingValue, uint16_t curve) {
  uint16_t pow3Calc = readingValue;
  float expValOut = (float)pow3Calc * pow3Calc / curve;
  if (expValOut < 0.005) {
    expValOut = 0;
  }
  return expValOut;
}

// Exp curve → uint16 (x^2 / curve). Used e.g. by apply_param_portamento_time.
uint16_t expConverter(uint16_t readingValue, uint16_t curve) {
  uint16_t pow3Calc = readingValue;
  uint16_t expValOut = (float)pow3Calc * pow3Calc / curve;
  if (expValOut < 0.1) {
    expValOut = 0;
  }
  return expValOut;
}
