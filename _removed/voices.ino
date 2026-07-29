// Removed unused code from voices.ino

// --- from original:1544-1700 ---
inline void voice_task_simple() {
  for (int i = 0; i < NUM_VOICES; i++) {

    if (note_on_flag[i] == 1) {
      note_on_flag_flag[i] = true;
      note_on_flag[i] = 0;
    }

    if (VOICE_NOTES[i] >= 0) {
      uint8_t note1 = VOICE_NOTES[i] - 36 + OSC1_interval;
      if (note1 > highestNote) {
        note1 -= ((uint8_t(note1 - highestNote) / 12) * 12);
      }
      uint8_t note2 = note1 - 36 + OSC2_interval;
      if (note2 > highestNote) {
        note2 -= ((uint8_t(note2 - highestNote) / 12) * 12);
      }
      uint8_t note3 = note1 - 36 + OSC3_interval;
      if (note3 > highestNote) {
        note3 -= ((uint8_t(note3 - highestNote) / 12) * 12);
      }

      float freq;
      float freq2;
      float freq3;

      // Fixed osc indices for current mono hardware (3 oscs on voice 0).
      // Future paraphonic mode can remap osc ownership per voice without gutting allocation.
      const uint8_t DCO_A = 0;
      const uint8_t DCO_B = 1;
      const uint8_t DCO_C = 2;

      freq = sNotePitches[note1];
      freq2 = sNotePitches[note2];
      freq3 = sNotePitches[note3];

      // Serial.println("VOICE TASK 2");

      if ((uint16_t)freq > maxFrequency) {
        freq = maxFrequency;
      } else if ((uint16_t)freq < 6) {
        freq = 6;
      }
      if ((uint16_t)freq2 >= maxFrequency) {
        freq2 = maxFrequency;
      } else if ((uint16_t)freq2 < 6) {
        freq2 = 6;
      }
      if ((uint16_t)freq3 >= maxFrequency) {
        freq3 = maxFrequency;
      } else if ((uint16_t)freq3 < 6) {
        freq3 = 6;
      }

      // voice_task_2_time = micros() - voice_task_start_time;

      uint8_t pioNumberA = VOICE_TO_PIO[DCO_A];
      uint8_t pioNumberB = VOICE_TO_PIO[DCO_B];
      PIO pioN_A = pio[VOICE_TO_PIO[DCO_A]];
      PIO pioN_B = pio[VOICE_TO_PIO[DCO_B]];
      PIO pioN_C = pio[VOICE_TO_PIO[DCO_C]];
      uint8_t smAN = VOICE_TO_SM[DCO_A];
      uint8_t smBN = VOICE_TO_SM[DCO_B];
      uint8_t smCN = VOICE_TO_SM[DCO_C];

      // voice_task_3_time = micros() - voice_task_start_time;

      uint32_t clk_div1 = (uint32_t)((sysClock_Hz / freq) - pioPulseLength) / NUM_OSR_CHUNKS;
      if (freq == 0)
        clk_div1 = 0;

      uint32_t clk_div2;
      uint32_t phaseDelay;

      if (oscSync > 1) {

        clk_div2 = (uint32_t)(sysClock_Hz / freq2);
        phaseDelay = (clk_div2 - pioPulseLength) / 180 * phaseAlignOSC2;
        clk_div2 = (uint32_t)((clk_div2 - phaseDelay) / NUM_OSR_CHUNKS);
      } else {
        clk_div2 = (uint32_t)((sysClock_Hz / freq2) - pioPulseLength) / NUM_OSR_CHUNKS;
      }
      if (freq2 == 0)
        clk_div2 = 0;

      uint32_t clk_div3 = (uint32_t)((sysClock_Hz / freq3) - pioPulseLength) / NUM_OSR_CHUNKS;
      if (freq3 == 0)
        clk_div3 = 0;

      // voice_task_4_time = micros() - voice_task_start_time;

      uint16_t chanLevel, chanLevel2, chanLevel3;

      switch (syncMode) {
        case 0:
          chanLevel = get_chan_level_for_engine(freq, DCO_A);
          chanLevel2 = get_chan_level_for_engine(freq2, DCO_B);
          break;
        case 1:
          chanLevel = get_chan_level_for_engine(max(freq, freq2), DCO_A);
          chanLevel2 = get_chan_level_for_engine(freq2, DCO_B);
          break;
        case 2:
          chanLevel = get_chan_level_for_engine(freq, DCO_A);
          chanLevel2 = get_chan_level_for_engine(max(freq, freq2), DCO_B);
          break;
        default:
          chanLevel = get_chan_level_for_engine(freq, DCO_A);
          chanLevel2 = get_chan_level_for_engine(freq2, DCO_B);
          break;
      }
      chanLevel3 = get_chan_level_for_engine(freq3, DCO_C);

      // VCO LEVEL //uint16_t vcoLevel = get_vco_level(freq);

      pio_sm_put(pioN_A, smAN, clk_div1);
      pio_sm_put(pioN_B, smBN, clk_div2);
      pio_sm_put(pioN_C, smCN, clk_div3);
      pio_sm_exec(pioN_A, smAN, pio_encode_pull(false, false));
      pio_sm_exec(pioN_B, smBN, pio_encode_pull(false, false));
      pio_sm_exec(pioN_C, smCN, pio_encode_pull(false, false));

      // Serial.println("VOICE TASK 5a");

      if (note_on_flag_flag[i]) {
        if (oscSync > 0) {
          pio_sm_exec(pioN_A, smAN, pio_encode_jmp(10 + offset[pioNumberA]));  // OSC Sync MODE
          pio_sm_exec(pioN_B, smBN, pio_encode_jmp(10 + offset[pioNumberB]));

          if (oscSync > 1) {
            pio_sm_put(pioN_B, smBN, pioPulseLength + phaseDelay);
            pio_sm_exec(pioN_B, smBN, pio_encode_pull(false, false));
            pio_sm_exec(pioN_B, smBN, pio_encode_out(pio_y, 31));
            pio_sm_exec(pioN_B, smBN, pio_encode_out(pio_x, 31));
          }
        }

        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], pwm_gpio_to_channel(RANGE_PINS[DCO_C]), chanLevel3);
      }

      if (timer99microsFlag) {
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], pwm_gpio_to_channel(RANGE_PINS[DCO_C]), chanLevel3);
      }

      if (sqr1Status) {
        pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), PW_CENTER[i]);
      } else {
        pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), 0);
      }
    }
    note_on_flag_flag[i] = false;
  }
}

// --- from original:2089-2180 ---
void voice_task_debug() {

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    if (note_on_flag[i] == 1) {
      note_on_flag_flag[i] = true;
      note_on_flag[i] = 0;
    }
  }

  last_midi_pitch_bend = midi_pitch_bend;
  LAST_DETUNE = DETUNE;
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    if (VOICE_NOTES[i] >= 0) {
      uint8_t note1 = VOICE_NOTES[i] - 36 + OSC1_interval;
      if (note1 > highestNote) {
        note1 -= ((uint8_t(note1 - highestNote) / 12) * 12);
      }
      uint8_t note2 = note1 - 36 + OSC2_interval;
      if (note2 > highestNote) {
        note2 -= ((uint8_t(note2 - highestNote) / 12) * 12);
      }

      uint8_t DCO_A = i * 2;
      uint8_t DCO_B = (i * 2) + 1;

      register float freq;
      register float freq2;

      freq = (float)sNotePitches[note1];
      freq2 = (float)sNotePitches[note2];

      uint8_t pioNumberA = VOICE_TO_PIO[DCO_A];
      uint8_t pioNumberB = VOICE_TO_PIO[DCO_B];
      PIO pioN_A = pio[VOICE_TO_PIO[DCO_A]];
      PIO pioN_B = pio[VOICE_TO_PIO[DCO_B]];
      uint8_t sm1N = VOICE_TO_SM[DCO_A];
      uint8_t sm2N = VOICE_TO_SM[DCO_B];

      register uint32_t clk_div1 = (uint32_t)(((float)sysClock_Hz / freq) - pioPulseLength - 1) / NUM_OSR_CHUNKS;

      if (freq == 0)
        clk_div1 = 0;

      register uint32_t clk_div2 = (uint32_t)(((float)sysClock_Hz / freq2) - pioPulseLength - 1) / NUM_OSR_CHUNKS;

      uint16_t chanLevel = get_chan_level_for_engine(freq, (i * 2));
      uint16_t chanLevel2 = get_chan_level_for_engine(freq2, (i * 2) + 1);

      if (oscSync == 0) {

        pio_sm_put(pioN_A, sm1N, clk_div1);
        pio_sm_put(pioN_B, sm2N, clk_div2);
        pio_sm_exec(pioN_A, sm1N, pio_encode_pull(false, false));
        pio_sm_exec(pioN_B, sm2N, pio_encode_pull(false, false));
      } else {
        pio_sm_put(pioN_A, sm1N, clk_div1);
        pio_sm_put(pioN_B, sm2N, clk_div2);
        pio_sm_exec(pioN_A, sm1N, pio_encode_pull(false, false));
        pio_sm_exec(pioN_B, sm2N, pio_encode_pull(false, false));
        if (note_on_flag_flag[i]) {
          switch (oscSync) {
            case 1:
              pio_sm_exec(pioN_A, sm1N, pio_encode_jmp(10 + offset[pioNumberA]));  // OSC Sync MODE
              pio_sm_exec(pioN_B, sm2N, pio_encode_jmp(10 + offset[pioNumberB]));
              break;
            case 2:
              pio_sm_exec(pioN_A, sm1N, pio_encode_jmp(4 + offset[pioNumberA]));  // OSC Half Sync MODE
              pio_sm_exec(pioN_B, sm2N, pio_encode_jmp(12 + offset[pioNumberB]));
              break;
            case 3:
              pio_sm_exec(pioN_A, sm1N, pio_encode_jmp(4 + offset[pioNumberA]));  // OSC 3rd-quarter Sync MODE
              pio_sm_exec(pioN_B, sm2N, pio_encode_jmp(10 + offset[pioNumberB]));
              break;
            default:
              break;
          }
          uint16_t chanLevel = get_chan_level_for_engine(freq, (i * 2));
          uint16_t chanLevel2 = get_chan_level_for_engine(freq2, (i * 2) + 1);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
        }
      }
      if (timer99microsFlag) {
        uint16_t chanLevel = get_chan_level_for_engine(freq , (i * 2));
        uint16_t chanLevel2 = get_chan_level_for_engine(freq2, (i * 2) + 1);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
      }
    }
    note_on_flag_flag[i] = false;
  }
}

// --- LAST_DETUNE assign ---
  LAST_DETUNE = DETUNE;

// --- LAST_DETUNE assign ---
    LAST_DETUNE          = DETUNE;

// --- voiceFreq write ---
voiceFreq[currentDCO] = freq;

// --- re-excise voice_task_simple (live file still had it) ---
inline void voice_task_simple() {
  for (int i = 0; i < NUM_VOICES; i++) {

    if (note_on_flag[i] == 1) {
      note_on_flag_flag[i] = true;
      note_on_flag[i] = 0;
    }

    if (VOICE_NOTES[i] >= 0) {
      uint8_t note1 = VOICE_NOTES[i] - 36 + OSC1_interval;
      if (note1 > highestNote) {
        note1 -= ((uint8_t(note1 - highestNote) / 12) * 12);
      }
      uint8_t note2 = note1 - 36 + OSC2_interval;
      if (note2 > highestNote) {
        note2 -= ((uint8_t(note2 - highestNote) / 12) * 12);
      }
      uint8_t note3 = note1 - 36 + OSC3_interval;
      if (note3 > highestNote) {
        note3 -= ((uint8_t(note3 - highestNote) / 12) * 12);
      }

      if (OSC2DetuneVal == 256) {
        OSC2_detune = 1;
      } else {
        OSC2_detune = 1.00f + (0.0002f * ((int)256 - OSC2DetuneVal));
      }

      float freq;
      float freq2;
      float freq3;

      // Fixed osc indices for current mono hardware (3 oscs on voice 0).
      // Future paraphonic mode can remap osc ownership per voice without gutting allocation.
      const uint8_t DCO_A = 0;
      const uint8_t DCO_B = 1;
      const uint8_t DCO_C = 2;

      freq = sNotePitches[note1];
      freq2 = sNotePitches[note2];
      freq3 = sNotePitches[note3];

      // Serial.println("VOICE TASK 2");

      if ((uint16_t)freq > maxFrequency) {
        freq = maxFrequency;
      } else if ((uint16_t)freq < 6) {
        freq = 6;
      }
      if ((uint16_t)freq2 >= maxFrequency) {
        freq2 = maxFrequency;
      } else if ((uint16_t)freq2 < 6) {
        freq2 = 6;
      }
      if ((uint16_t)freq3 >= maxFrequency) {
        freq3 = maxFrequency;
      } else if ((uint16_t)freq3 < 6) {
        freq3 = 6;
      }

      // voice_task_2_time = micros() - voice_task_start_time;

      uint8_t pioNumberA = VOICE_TO_PIO[DCO_A];
      uint8_t pioNumberB = VOICE_TO_PIO[DCO_B];
      PIO pioN_A = pio[VOICE_TO_PIO[DCO_A]];
      PIO pioN_B = pio[VOICE_TO_PIO[DCO_B]];
      PIO pioN_C = pio[VOICE_TO_PIO[DCO_C]];
      uint8_t smAN = VOICE_TO_SM[DCO_A];
      uint8_t smBN = VOICE_TO_SM[DCO_B];
      uint8_t smCN = VOICE_TO_SM[DCO_C];

      // voice_task_3_time = micros() - voice_task_start_time;

      uint32_t clk_div1 = (uint32_t)((sysClock_Hz / freq) - pioPulseLength) / NUM_OSR_CHUNKS;
      if (freq == 0)
        clk_div1 = 0;

      uint32_t clk_div2;
      uint32_t phaseDelay;

      if (oscSync > 1) {

        clk_div2 = (uint32_t)(sysClock_Hz / freq2);
        phaseDelay = (clk_div2 - pioPulseLength) / 180 * phaseAlignOSC2;
        clk_div2 = (uint32_t)((clk_div2 - phaseDelay) / NUM_OSR_CHUNKS);
      } else {
        clk_div2 = (uint32_t)((sysClock_Hz / freq2) - pioPulseLength) / NUM_OSR_CHUNKS;
      }
      if (freq2 == 0)
        clk_div2 = 0;

      uint32_t clk_div3 = (uint32_t)((sysClock_Hz / freq3) - pioPulseLength) / NUM_OSR_CHUNKS;
      if (freq3 == 0)
        clk_div3 = 0;

      // voice_task_4_time = micros() - voice_task_start_time;

      uint16_t chanLevel, chanLevel2, chanLevel3;

      switch (syncMode) {
        case 0:
          chanLevel = get_chan_level_for_engine(freq, DCO_A);
          chanLevel2 = get_chan_level_for_engine(freq2, DCO_B);
          break;
        case 1:
          chanLevel = get_chan_level_for_engine(max(freq, freq2), DCO_A);
          chanLevel2 = get_chan_level_for_engine(freq2, DCO_B);
          break;
        case 2:
          chanLevel = get_chan_level_for_engine(freq, DCO_A);
          chanLevel2 = get_chan_level_for_engine(max(freq, freq2), DCO_B);
          break;
        default:
          chanLevel = get_chan_level_for_engine(freq, DCO_A);
          chanLevel2 = get_chan_level_for_engine(freq2, DCO_B);
          break;
      }
      chanLevel3 = get_chan_level_for_engine(freq3, DCO_C);

      // VCO LEVEL //uint16_t vcoLevel = get_vco_level(freq);

      pio_sm_put(pioN_A, smAN, clk_div1);
      pio_sm_put(pioN_B, smBN, clk_div2);
      pio_sm_put(pioN_C, smCN, clk_div3);
      pio_sm_exec(pioN_A, smAN, pio_encode_pull(false, false));
      pio_sm_exec(pioN_B, smBN, pio_encode_pull(false, false));
      pio_sm_exec(pioN_C, smCN, pio_encode_pull(false, false));

      // Serial.println("VOICE TASK 5a");

      if (note_on_flag_flag[i]) {
        if (oscSync > 0) {
          pio_sm_exec(pioN_A, smAN, pio_encode_jmp(10 + offset[pioNumberA]));  // OSC Sync MODE
          pio_sm_exec(pioN_B, smBN, pio_encode_jmp(10 + offset[pioNumberB]));

          if (oscSync > 1) {
            pio_sm_put(pioN_B, smBN, pioPulseLength + phaseDelay);
            pio_sm_exec(pioN_B, smBN, pio_encode_pull(false, false));
            pio_sm_exec(pioN_B, smBN, pio_encode_out(pio_y, 31));
            pio_sm_exec(pioN_B, smBN, pio_encode_out(pio_x, 31));
          }
        }

        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], pwm_gpio_to_channel(RANGE_PINS[DCO_C]), chanLevel3);
      }

      if (timer99microsFlag) {
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_C], pwm_gpio_to_channel(RANGE_PINS[DCO_C]), chanLevel3);
      }

      if (sqr1Status) {
        pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), PW_CENTER[i]);
      } else {
        pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), 0);
      }
    }
    note_on_flag_flag[i] = false;
  }
}

inline uint8_t get_free_voice_sequential() {
  uint8_t nextVoice;
  uint8_t freeVoices = 0;

  if (VOICES[VOICES_LAST_SEQUENCE[NUM_VOICES_TOTAL - 1]] == 1 || VOICES[VOICES_LAST_SEQUENCE[NUM_VOICES_TOTAL - 1]] == 0) {
    for (int voiceIndex = NUM_VOICES_TOTAL - 1; voiceIndex > 0; voiceIndex--) {
      if (VOICES[VOICES_LAST_SEQUENCE[voiceIndex]] == 0) {
        nextVoice = VOICES_LAST_SEQUENCE[voiceIndex];
        freeVoices = 1;
        for (int freeIndex = voiceIndex; freeIndex > 0; freeIndex--) {
          VOICES_LAST_SEQUENCE[freeIndex] = VOICES_LAST_SEQUENCE[freeIndex - 1];
        }
        VOICES_LAST_SEQUENCE[0] = nextVoice;
        return nextVoice;
      }
    }
  } else {
    if (VOICES[VOICES_LAST_SEQUENCE[NUM_VOICES_TOTAL - 1]] == 0) {
      nextVoice = VOICES_LAST_SEQUENCE[NUM_VOICES_TOTAL - 1];

      for (int voiceIndex = NUM_VOICES_TOTAL - 1; voiceIndex > 0; voiceIndex--) {
        VOICES_LAST_SEQUENCE[voiceIndex] = VOICES_LAST_SEQUENCE[voiceIndex - 1];
      }

      VOICES_LAST_SEQUENCE[0] = nextVoice;

      return nextVoice;
    }
  }
  if (freeVoices == 0) {
    nextVoice = VOICES_LAST_SEQUENCE[NUM_VOICES_TOTAL - 1];

    for (int voiceIndex = NUM_VOICES_TOTAL - 1; voiceIndex > 0; voiceIndex--) {
      VOICES_LAST_SEQUENCE[voiceIndex] = VOICES_LAST_SEQUENCE[voiceIndex - 1];
    }

    VOICES_LAST_SEQUENCE[0] = nextVoice;
  }
  return nextVoice;
}

// Oldest-voice / steal allocator. Called from note_on() when polyMode == 0.
inline uint8_t get_free_voice() {
  uint32_t oldest_time = millis();
  uint8_t oldest_voice = 0;

  for (int i = 0; i < NUM_VOICES_TOTAL; i++)  // REVISAR!!
  {
    uint8_t n = (NEXT_VOICE + i) % NUM_VOICES_TOTAL;

    if (VOICES[n] == 0) {
      NEXT_VOICE = (n + 1) % NUM_VOICES_TOTAL;
      return n;
    }

    if (VOICES[i] < oldest_time) {
      oldest_time = VOICES[i];
      oldest_voice = i;
    }
  }

  NEXT_VOICE = (oldest_voice + 1) % NUM_VOICES_TOTAL;
  return oldest_voice;
}

// Map voiceMode → NUM_VOICES / STACK_VOICES. Called from init_voices and apply_param_voice_mode.
inline void setVoiceMode() {
  switch (voiceMode) {
    case 0:
      NUM_VOICES = 1;
      STACK_VOICES = 1;
      break;
    case 1:
      NUM_VOICES = NUM_VOICES_TOTAL;
      STACK_VOICES = 1;
      break;
    case 2:
      NUM_VOICES = NUM_VOICES_TOTAL;
      STACK_VOICES = NUM_VOICES_TOTAL;
      break;
  }
}

// Reconfigure PIO sideset pins for oscillator sync topology and retrigger voices.
// Called from apply_param_sync_mode (Serial2).
void setSyncMode() {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    uint8_t sidesetPin;
    switch (syncMode) {
      case 0:
        sidesetPin = RESET_PINS[i];
        break;
      case 1:
        // OSC2 syncs from OSC1; OSC3 free-running
        if (i == 1) {
          sidesetPin = RESET_PINS[0];
        } else {
          sidesetPin = RESET_PINS[i];
        }
        break;
      case 2:
        // OSC1 syncs from OSC2; OSC3 free-running
        if (i == 0) {
          sidesetPin = RESET_PINS[1];
        } else {
          sidesetPin = RESET_PINS[i];
        }
        break;
      default:
        sidesetPin = RESET_PINS[i];
        break;
    }

    pio_sm_set_sideset_pins(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i], sidesetPin);
    pio_gpio_init(pio[VOICE_TO_PIO[i]], sidesetPin);
    pio_sm_restart(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i]);  // IS THIS NEEDED ?
  }

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    note_on_flag[i] = 1;
  }
}

#ifndef USE_FLOAT_AMP_COMP
/**
 * @brief Fast amplitude compensation lookup tuned for the RP2040.
 *
 * Uses a deterministic window selection rule that matches the float path:
 * for each oscillator we choose the *first* window i such that
 *   freqRow[i] <= x < freqRow[i+2]
 * scanning from low to high. This guarantees that a given x always maps to the
 * same window regardless of whether we are gliding up or down in frequency
 * (no hysteresis), while keeping the table small enough that a linear scan
 * remains very cheap on the M0+.
 *
 * The core calculation uses the proven, numerically stable fixed-point
 * quadratic method with precomputed Q-format coefficients.
 */

// --- re-excise voice_task_debug ---
void voice_task_debug() {

  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    if (note_on_flag[i] == 1) {
      note_on_flag_flag[i] = true;
      note_on_flag[i] = 0;
    }
  }

  last_midi_pitch_bend = midi_pitch_bend;
  LAST_DETUNE = DETUNE;
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    if (VOICE_NOTES[i] >= 0) {
      uint8_t note1 = VOICE_NOTES[i] - 36 + OSC1_interval;
      if (note1 > highestNote) {
        note1 -= ((uint8_t(note1 - highestNote) / 12) * 12);
      }
      uint8_t note2 = note1 - 36 + OSC2_interval;
      if (note2 > highestNote) {
        note2 -= ((uint8_t(note2 - highestNote) / 12) * 12);
      }

      uint8_t DCO_A = i * 2;
      uint8_t DCO_B = (i * 2) + 1;

      register float freq;
      register float freq2;

      freq = (float)sNotePitches[note1];
      freq2 = (float)sNotePitches[note2];

      uint8_t pioNumberA = VOICE_TO_PIO[DCO_A];
      uint8_t pioNumberB = VOICE_TO_PIO[DCO_B];
      PIO pioN_A = pio[VOICE_TO_PIO[DCO_A]];
      PIO pioN_B = pio[VOICE_TO_PIO[DCO_B]];
      uint8_t sm1N = VOICE_TO_SM[DCO_A];
      uint8_t sm2N = VOICE_TO_SM[DCO_B];

      register uint32_t clk_div1 = (uint32_t)(((float)sysClock_Hz / freq) - pioPulseLength - 1) / NUM_OSR_CHUNKS;

      if (freq == 0)
        clk_div1 = 0;

      register uint32_t clk_div2 = (uint32_t)(((float)sysClock_Hz / freq2) - pioPulseLength - 1) / NUM_OSR_CHUNKS;

      uint16_t chanLevel = get_chan_level_for_engine(freq, (i * 2));
      uint16_t chanLevel2 = get_chan_level_for_engine(freq2, (i * 2) + 1);

      if (oscSync == 0) {

        pio_sm_put(pioN_A, sm1N, clk_div1);
        pio_sm_put(pioN_B, sm2N, clk_div2);
        pio_sm_exec(pioN_A, sm1N, pio_encode_pull(false, false));
        pio_sm_exec(pioN_B, sm2N, pio_encode_pull(false, false));
      } else {
        pio_sm_put(pioN_A, sm1N, clk_div1);
        pio_sm_put(pioN_B, sm2N, clk_div2);
        pio_sm_exec(pioN_A, sm1N, pio_encode_pull(false, false));
        pio_sm_exec(pioN_B, sm2N, pio_encode_pull(false, false));
        if (note_on_flag_flag[i]) {
          switch (oscSync) {
            case 1:
              pio_sm_exec(pioN_A, sm1N, pio_encode_jmp(10 + offset[pioNumberA]));  // OSC Sync MODE
              pio_sm_exec(pioN_B, sm2N, pio_encode_jmp(10 + offset[pioNumberB]));
              break;
            case 2:
              pio_sm_exec(pioN_A, sm1N, pio_encode_jmp(4 + offset[pioNumberA]));  // OSC Half Sync MODE
              pio_sm_exec(pioN_B, sm2N, pio_encode_jmp(12 + offset[pioNumberB]));
              break;
            case 3:
              pio_sm_exec(pioN_A, sm1N, pio_encode_jmp(4 + offset[pioNumberA]));  // OSC 3rd-quarter Sync MODE
              pio_sm_exec(pioN_B, sm2N, pio_encode_jmp(10 + offset[pioNumberB]));
              break;
            default:
              break;
          }
          uint16_t chanLevel = get_chan_level_for_engine(freq, (i * 2));
          uint16_t chanLevel2 = get_chan_level_for_engine(freq2, (i * 2) + 1);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
          pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
        }
      }
      if (timer99microsFlag) {
        uint16_t chanLevel = get_chan_level_for_engine(freq , (i * 2));
        uint16_t chanLevel2 = get_chan_level_for_engine(freq2, (i * 2) + 1);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_A], pwm_gpio_to_channel(RANGE_PINS[DCO_A]), chanLevel);
        pwm_set_chan_level(RANGE_PWM_SLICES[DCO_B], pwm_gpio_to_channel(RANGE_PINS[DCO_B]), chanLevel2);
      }
    }
    note_on_flag_flag[i] = false;
  }
}

// Cached variant: pass DCO index to reuse last segment and avoid binary search
