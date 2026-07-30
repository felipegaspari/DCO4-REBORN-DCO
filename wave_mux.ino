// Dual 74HC595 wave enables (Mainboard RoxMux port — bit-bang, no RoxMux dep).
// Active-low enables; pin indices match Mainboard "crossed cables" map (4 voice slots).

#ifdef ENABLE_WAVE_MUX

static uint16_t waveMuxBits = 0xFFFF;  // all off (1 = disabled)

// Crossed-cable map from Mainboard/waveSelector.h (voice slots 0..3).
static const uint8_t triPins[4]  = { 14, 10, 6, 2 };
static const uint8_t sinePins[4] = { 13, 9, 5, 1 };
static const uint8_t saw2Pins[4] = { 12, 8, 4, 0 };
static const uint8_t sawPins[4]  = { 15, 11, 7, 3 };

static inline void waveMuxWritePin(uint8_t pin, bool high) {
  if (pin > 15) return;
  if (high) {
    waveMuxBits |= (uint16_t)(1u << pin);
  } else {
    waveMuxBits &= (uint16_t)~(1u << pin);
  }
}

static void waveMuxShiftOut() {
  // Shift MSB first into daisy-chain (chip2 then chip1 convention used by Rox74HC595).
  for (int i = 15; i >= 0; --i) {
    gpio_put(HC595_DATA_PIN, (waveMuxBits >> i) & 1u);
    gpio_put(HC595_CLK_PIN, 1);
    busy_wait_us_32(1);
    gpio_put(HC595_CLK_PIN, 0);
  }
  gpio_put(HC595_LATCH_PIN, 1);
  busy_wait_us_32(1);
  gpio_put(HC595_LATCH_PIN, 0);
}

void init_waveSelector() {
  pinMode(HC595_LATCH_PIN, OUTPUT);
  pinMode(HC595_DATA_PIN, OUTPUT);
  pinMode(HC595_CLK_PIN, OUTPUT);
  digitalWrite(HC595_LATCH_PIN, LOW);
  digitalWrite(HC595_CLK_PIN, LOW);
  digitalWrite(HC595_DATA_PIN, LOW);
  waveMuxBits = 0xFFFF;
  waveMuxShiftOut();
}

// Update one wave family (0–3) or all (4) from status flags. Monosynth: only slot 0 live.
void update_waveSelector(byte wave) {
  switch (wave) {
    case 0:
      for (int i = 0; i < 4; i++) {
        waveMuxWritePin(sawPins[i], (i < NUM_VOICES) ? !sawStatus : 1);
      }
      break;
    case 1:
      for (int i = 0; i < 4; i++) {
        waveMuxWritePin(saw2Pins[i], (i < NUM_VOICES) ? !saw2Status : 1);
      }
      break;
    case 2:
      for (int i = 0; i < 4; i++) {
        waveMuxWritePin(triPins[i], (i < NUM_VOICES) ? !triStatus : 1);
      }
      break;
    case 3:
      for (int i = 0; i < 4; i++) {
        waveMuxWritePin(sinePins[i], (i < NUM_VOICES) ? !sqr2Status : 1);
      }
      break;
    case 4:
      for (int i = 0; i < 4; i++) {
        waveMuxWritePin(sawPins[i], (i < NUM_VOICES) ? !sawStatus : 1);
        waveMuxWritePin(saw2Pins[i], (i < NUM_VOICES) ? !saw2Status : 1);
        waveMuxWritePin(triPins[i], (i < NUM_VOICES) ? !triStatus : 1);
        waveMuxWritePin(sinePins[i], (i < NUM_VOICES) ? !sqr2Status : 1);
      }
      break;
    default:
      break;
  }
  waveMuxShiftOut();
}

// Manual calibration: isolate one oscillator's wave path (all others off).
// Stage is the oscillator index 0..2; OSC3 has no mux hardware, so it stays silent.
void waveSelector_manual_calibration(byte stage) {
  for (int i = 0; i < 4; i++) {
    waveMuxWritePin(sawPins[i], 1);
    waveMuxWritePin(saw2Pins[i], 1);
    waveMuxWritePin(triPins[i], 1);
    waveMuxWritePin(sinePins[i], 1);
  }

  if (stage == 0) {
    waveMuxWritePin(sawPins[0], 0);
  } else if (stage == 1) {
    waveMuxWritePin(saw2Pins[0], 0);
  }

  waveMuxShiftOut();
}

#else  // !ENABLE_WAVE_MUX

void init_waveSelector() {}
void update_waveSelector(byte) {}
void waveSelector_manual_calibration(byte) {}

#endif
