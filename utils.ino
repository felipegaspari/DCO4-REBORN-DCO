
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
