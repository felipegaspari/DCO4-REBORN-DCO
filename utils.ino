// long map(long x, long in_min, long in_max, long out_min, long out_max) {
//   return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
// }

// Convert uint64 to decimal C string. Currently unused (only commented use in irq_tuner).
char * uintToStr( const uint64_t num, char *str )
{
  uint8_t i = 0;
  uint64_t n = num;
  
  do
    i++;
  while ( n /= 10 );
  
  str[i] = '\0';
  n = num;
 
  do
    str[--i] = ( n % 10 ) + '0';
  while ( n /= 10 );

  return str;
}

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

// Linear → exponential mapping (0..maxValue). Currently unused.
uint16_t linearToExponential(uint16_t linearValue, float base, uint16_t maxValue) {

  if (linearValue < 0) linearValue = 0;
  if (linearValue > maxValue) linearValue = maxValue;

  float normalizedValue = (float)linearValue / (float)maxValue;
  float expValue = pow(base, normalizedValue) - 1;
  float maxExpValue = pow(base, 1.0) - 1;
  uint16_t scaledExpValue = (uint16_t)(expValue * ((float)maxValue / maxExpValue));

  return scaledExpValue;
}

// Fader-style cubic-ish exp curve. Currently unused.
uint16_t faderExpConverter(uint16_t readingValue) {
  uint16_t pow3Calc = readingValue / 4;
  uint16_t expValOut = pow3Calc * pow3Calc * pow3Calc / 20000;
  return expValOut;
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

// Inverse of expConverter (sqrt). Currently unused.
uint16_t expConverterReverse(uint16_t readingValue, uint16_t curve) {
  uint16_t expValOut = sqrt((float)readingValue / curve);
  return expValOut;
}

// Inverse of expConverterFloat. Currently unused.
uint16_t expConverterFloatReverse(float readingValue, uint16_t curve) {
  uint16_t expValOut = sqrt(readingValue / curve);
  return expValOut;
}

// Cubic exp curve variant (x^3 / curve). Currently unused.
uint16_t expConverter2(uint16_t readingValue, uint16_t curve) {
  uint16_t pow3Calc = readingValue;
  uint16_t expValOut = pow3Calc * pow3Calc * pow3Calc / curve;
  return expValOut;
}

//1 VCFKeytrack
//2 ADSR2toVCF
//3 LFO1toVCF
//4 LFO2toVCF
//5 ADSR3toPWM
//6 LFO1toPWM
//7 LFO1toVCA
//8 ADSR1toVCA
//9 LFO1toDCO
//10 ADSR3toDETUNE1

// Legacy numbered formula update (body commented out). Currently unused.
void formula_update(byte formulaN) {
  // switch (formulaN) {
  //   case 1:
  //     if (VCFKeytrack > 0) {
  //       VCFKeytrackModifier = (float)VCFKeytrack / 8000;
  //     } else {
  //       VCFKeytrackModifier = 1;
  //     }
  //     break;
  //   case 2:
  //     ADSR2toVCF_formula = (float)1 / 512 * ADSR2toVCF;
  //     break;
  //   case 3:
  //     LFO1toVCF_formula = (float)1 / 512 * LFO1toVCF;
  //     break;
  //   case 4:
  //     LFO2toVCF_formula = (float)1 / 512 * LFO2toVCF;
  //     break;
  //   case 5:
  //     ADSR3toPWM_formula = (float)1 / 512 * ADSR3toPWM;
  //     break;
  //   case 6:
  //     LFO1toPWM_formula = (float)1 / 512 * LFO1toPWM;
  //     break;
  //   case 7:
  //     LFO1toVCA_formula = (float)1 / 512 * LFO1toVCA;
  //     break;
  //   case 8:
  //     ADSR1toVCA_formula = (float)1 / 512 * ADSR1toVCA;
  //     break;
  //   case 9:
  //     LFO1toDCO_formula = (float)1 / 1080000 * LFO1toDCO;
  //     break;
  //   case 10:
  //     ADSR3toDETUNE1_formula = (float)1 / 1080000 * ADSR3toDETUNE1;
  //     break;
  //   case 11:
  //   LFO2toPWM_formula = (float)1 / 512 * LFO2toPWM;
  //   break;
  // }
}

// Map raw control values into float params (LFO speeds / LFO1→DCO). Currently unused.
void controls_formula_update(byte formulaN) {
  switch (formulaN) {
    case 1:
      LFO1Speed = expConverterFloat(LFO1SpeedVal, 5000);
      //LFO1_class.setMode0Freq(LFO1Speed, micros());
      break;
          case 2:
      LFO2Speed = expConverterFloat(LFO2SpeedVal, 5000);
      //LFO2_class.setMode0Freq(LFO2Speed, micros());
      break;
      case 3:
      LFO1toDCO = expConverterFloat(LFO1toDCOVal, 500);
      break;
  }
}


// Clear onboard LED after blink window. Currently unused (no callers).
void led_blinking_task() {
  if (millis() - LED_BLINK_START < 50)
    return;
  digitalWrite(LED_BUILTIN, LOW);
}