MidiUartRingBuffer midi_din_rx_buf;
MidiUartTransport  midi_din_transport;
Adafruit_USBD_MIDI usb_midi;

// Hardware UART0 Interrupt Handler (Runs in SRAM)
void __not_in_flash_func(on_midi_uart_rx)() {
  while (uart_is_readable(uart0)) {
    midi_din_rx_buf.push((uint8_t)uart_get_hw(uart0)->dr);
  }
}


// Mono held-key stack lives in the shared library (monoStack in
// voice_alloc_state.h); which entry sounds depends on the allocation mode,
// which doubles as the mono note priority.
// Voice_task porta still restarts only on note_on_flag →
// note_on_flag_flag from VOICE_NOTES (no pitch queue on Core1).
static constexpr uint8_t MONO_NOTE_NONE = VOICE_ALLOC_NONE;
// Pitch currently gated on voice 0, so a key that loses priority can be stacked
// silently instead of retriggering the envelope.
static uint8_t mono_sounding_note = MONO_NOTE_NONE;

// Empty held stack when entering mono so notes do not leak across voice modes.
void mono_note_stack_clear() {
  monoStack.clear();
  mono_sounding_note = MONO_NOTE_NONE;
}


uint8_t velocity[NUM_VOICES_TOTAL];

// MIDI library callback → note_on(). Invoked from loop via MIDI_*.read().
void SRAM_HOT(handleNoteOn)(byte channel, byte pitch, byte velocity) {
  if (velocity == 0) {
    note_off(pitch);
    return;
  }
  //Serial.printf(">> MIDI IN: Ch=%d | Pitch=%d | Vel=%d | voiceMode=%d\n", 
  //  channel, pitch, velocity, voiceMode);
  note_on(pitch, velocity);
}
// MIDI library callback → note_off().
void SRAM_HOT(handleNoteOff)(byte channel, byte pitch, byte velocity) {
  note_off(pitch);
}

// -----------------------------------------------------------------------------
// Main CC Dispatcher
// -----------------------------------------------------------------------------
void SRAM_HOT(handleControlChange)(byte channel, byte number, byte value) {
  // Optional: MIDI Channel filter (if channel 0 = Omni / Listen to all)
  if (midi_channel != 0 && channel != midi_channel) {
    return;
  }

  switch (number) {
    // -------------------------------------------------------------------------
    // 1. Bank Select (CC 0 MSB, CC 32 LSB)
    // Latch upper (128..255) / lower (0..127) bank for next Program Change
    // -------------------------------------------------------------------------
    case 0:
    case 32:
      midiPresetBank = (value > 0) ? 1 : 0;
      return;

    // -------------------------------------------------------------------------
    // 2. Realtime Continuous Modulators (Sources for Mod Matrix)
    // -------------------------------------------------------------------------
    case 1:  // Mod Wheel (CC 1)
      midi_mod_wheel = value;
      mod_matrix_set_mod_wheel(value);
      return;

    case 2:  // Breath Controller (CC 2)
      midi_breath = value;
      mod_matrix_set_breath(value);
      return;

    case 11: // Expression Pedal (CC 11)
      midi_expression = value;
      mod_matrix_set_expression(value);
      return;

    // -------------------------------------------------------------------------
    // 3. Performance Switches
    // -------------------------------------------------------------------------
    case 64: // Sustain / Damper Pedal (>= 64 is ON, < 64 is OFF)
      midi_sustain = (value >= 64);
      // voice_allocator_set_sustain(midi_sustain);
      return;

    // -------------------------------------------------------------------------
    // 4. Custom DCO Pitch-Bend Range (CC 42)
    // -------------------------------------------------------------------------
    case MIDI_CC_PITCH_BEND_RANGE: {
      pitchBendRange = (value > 48) ? 48 : value;
      #if VOICE_ENGINE_FLOAT 
      pitchBendMultiplier = (float)pitchBendMultiplier_q24 / (float)(1 << 24);
      #else
      pitchBendMultiplier_q24 = (int32_t)(((int64_t)pitchBendRange * RECIP_TWELVE_Q24));
      #endif
      return;
    }

    // -------------------------------------------------------------------------
    // 5. Channel Mode Messages (CC 120 - 127)
    // -------------------------------------------------------------------------
    case 120: // All Sound Off (Kill voices immediately)
     // voice_allocator_all_sound_off();
      return;

    case 121: // Reset All Controllers
      // reset_all_controllers();
      return;

    case 123: // All Notes Off (Release active voice gates)
      //voice_allocator_all_notes_off();
      return;

    case 124: // Omni Mode Off
    case 125: // Omni Mode On
    case 126: // Mono Mode On
    case 127: // Poly Mode On
      // Polyphonic synth mode management:
      // voice_allocator_all_notes_off();
      // if (number == 126) {
      //   voice_allocator_set_mode(VOICE_MODE_MONO);
      // } else if (number == 127) {
      //   voice_allocator_set_mode(VOICE_MODE_POLY);
      // }
      return;

    // -------------------------------------------------------------------------
    // 6. Static Synth Parameters via generated jump-table
    // -------------------------------------------------------------------------
    default:
      midi_cc_handle(number, value);
      break;
  }
}

// Scale a controller into its parameter's native range and apply it. Unmapped CCs are
// ignored. Linear search over ~70 entries, which at MIDI's 3125 bytes/s is free.
void midi_cc_handle(uint8_t number, uint8_t value) {
  for (size_t i = 0; i < midiCcMapSize; ++i) {
    const MidiCcEntry& entry = midiCcMap[i];
    if (entry.cc != number) continue;

    int16_t scaled = entry.lo + (int16_t)(((int32_t)(entry.hi - entry.lo) * value + 63) / 127);
    if (entry.curve == MIDI_CC_EXP_TIME) {
      scaled = (int16_t)linearToExponential((uint16_t)scaled, MIDI_CC_EXP_BASE, MIDI_CC_EXP_MAX);
    }
    midi_cc_apply(entry.target, scaled);
    return;
  }
}

// Route a scaled value to its target. Table parameters take the normal router; ADSR/filter
// block values have no ParamId (1 ms packed 'a'–'d' frames), so they are written here
// exactly as input_handle_*() in Serial.ino writes them. PW and EnvVCA→VCA are ParamIds.
void SRAM_HOT(midi_cc_apply)(uint8_t target, int16_t value) {
  switch (target) {
    case CC_LOCAL_ADSR_VCA_ATTACK:      ADSR_VCA_attack  = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_VCA_A); serial_send_adsr_vca_block_to_mb(); break;
    case CC_LOCAL_ADSR_VCA_DECAY:       ADSR_VCA_decay   = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_VCA_D); serial_send_adsr_vca_block_to_mb(); break;
    case CC_LOCAL_ADSR_VCA_SUSTAIN:     ADSR_VCA_sustain = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_VCA_S); serial_send_adsr_vca_block_to_mb(); break;
    case CC_LOCAL_ADSR_VCA_RELEASE:     ADSR_VCA_release = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_VCA_R); serial_send_adsr_vca_block_to_mb(); break;

    case CC_LOCAL_ADSR_VCF_ATTACK:      ADSR_VCF_attack  = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_VCF_A); serial_send_adsr_vcf_block_to_mb(); break;
    case CC_LOCAL_ADSR_VCF_DECAY:       ADSR_VCF_decay   = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_VCF_D); serial_send_adsr_vcf_block_to_mb(); break;
    case CC_LOCAL_ADSR_VCF_SUSTAIN:     ADSR_VCF_sustain = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_VCF_S); serial_send_adsr_vcf_block_to_mb(); break;
    case CC_LOCAL_ADSR_VCF_RELEASE:     ADSR_VCF_release = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_VCF_R); serial_send_adsr_vcf_block_to_mb(); break;

    // EnvDCO runs here, but the block still goes out: the Mainboard relays it to
    // the panel so the faders follow a CC edit.
    case CC_LOCAL_ADSR_DCO_ATTACK:      ADSR3_attack  = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_DCO_A); serial_send_adsr_dco_block_to_mb(); break;
    case CC_LOCAL_ADSR_DCO_DECAY:       ADSR3_decay   = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_DCO_D); serial_send_adsr_dco_block_to_mb(); break;
    case CC_LOCAL_ADSR_DCO_SUSTAIN:     ADSR3_sustain = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_DCO_S); serial_send_adsr_dco_block_to_mb(); break;
    case CC_LOCAL_ADSR_DCO_RELEASE:     ADSR3_release = (uint16_t)value; mark_adsr_params_dirty(ADSR_DIRTY_DCO_R); serial_send_adsr_dco_block_to_mb(); break;

    // Analog VCF CVs live on Mainboard; emit slim 'd' after local update + scale bake.
    case CC_LOCAL_FILTER_CUTOFF:        CUTOFF     = (uint16_t)value; serial_send_filter_block_to_mb(); break;
    case CC_LOCAL_FILTER_RESONANCE:     RESONANCE  = (uint16_t)value; serial_send_filter_block_to_mb(); break;
    case CC_LOCAL_FILTER_ADSR2_TO_VCF:  ADSR2toVCF = value;           cv_bake_adsr2_to_vcf_scale(); serial_send_filter_block_to_mb(); break;
    case CC_LOCAL_FILTER_LFO2_TO_VCF:   LFO2toVCF  = (uint16_t)value; cv_bake_lfo2_to_vcf_scale(); serial_send_filter_block_to_mb(); break;

    default:
      update_parameters((uint16_t)target, value);
      serial_echo_usb_param16(target, value);
      break;
  }
}

// MIDI program-change callback → recall preset slot
// (midiPresetBank * 128 + program), covering 0..255 (preset_store.ino).
void handleProgramChange(byte channel, byte program) {
  (void)channel;
  const uint16_t slot = (uint16_t)midiPresetBank * 128u + (uint16_t)program;
  if (slot < PRESET_NUM_SLOTS) {
    preset_store_load((uint8_t)slot);
  }
}

// MIDI pitch-bend callback → midi_pitch_bend (offset to 0..16383 style).
void handlePitchBend(byte channel, int pitchBend) {
  midi_pitch_bend = pitchBend + 8192;
  serial_send_expression();
}

// Channel aftertouch → mod matrix source.
void handleAfterTouchChannel(byte channel, byte pressure) {
  (void)channel;
  midi_aftertouch = pressure;
  mod_matrix_set_aftertouch(pressure);
  serial_send_expression();
}

// ===========================================================================
// INLINE MATH HELPERS (Optimized for Cortex-M0+ / RP2040)
// ===========================================================================

// Fast loop-based folding avoids the RP2040's slow software division (modulo)
static inline uint8_t SRAM_HOT(fold_table_idx)(int val) {
  constexpr int MAX_TABLE_IDX = (sizeof(sNotePitches_q24) / sizeof(sNotePitches_q24[0])) - 1;
  while (val > MAX_TABLE_IDX) val -= 12;
  while (val < 0)             val += 12;
  return (uint8_t)val;
}

// Single function to calculate the modified table index from an incoming key strike
static inline void SRAM_HOT(get_modified_indices)(uint8_t raw_note, uint8_t* idx1, uint8_t* idx2) {
  int shifted = (int)raw_note + (int)octave_shift;
  while (shifted > 127) shifted -= 12;
  while (shifted < 0)   shifted += 12;

  *idx1 = fold_table_idx(shifted - 36);
  *idx2 = fold_table_idx(shifted - 36 + ((int)OSC2_interval - 36));
}

// Easily derive OSC2 index from OSC1 (useful during priority stack fallbacks)
static inline uint8_t SRAM_HOT(get_osc2_from_osc1)(uint8_t idx1) {
  return fold_table_idx((int)idx1 + ((int)OSC2_interval - 36));
}

// ===========================================================================
// NOTE ON / OFF ROUTINES
// ===========================================================================

void SRAM_HOT(note_on)(uint8_t note, uint8_t velocity_in) {
  const uint8_t alloc_mode = voiceAlloc.mode();
  
  uint8_t note1_idx, note2_idx;
  get_modified_indices(note, &note1_idx, &note2_idx);

  switch (voiceMode) {
    case 0:  // MONO
    case 2:  // UNISON
    {
      // The stack exclusively tracks the modified version
      if (!monoStack.push(note1_idx, alloc_mode)) return;
      const uint8_t winner = monoStack.pick(alloc_mode); 
      
      if (winner == MONO_NOTE_NONE || winner == mono_sounding_note) return;
      
      const bool is_legato = (alloc_mode == 2) && (mono_sounding_note != MONO_NOTE_NONE);
      mono_sounding_note = winner;

      // If priority stack chose a fallback held key, re-sync notes
      if (winner != note1_idx) {
        note1_idx = winner;
        note2_idx = get_osc2_from_osc1(winner);
      }

      const uint8_t count = (voiceMode == 0) ? 1 : NUM_VOICES_TOTAL;
      const uint8_t note_flag = is_legato ? NOTE_FLAG_PORTA_ONLY : NOTE_FLAG_RETRIGGER;

      for (uint8_t v = 0; v < count; v++) {
        if (is_legato) voice_mark_regate(v, note1_idx, note2_idx);
        else           voice_mark_on(v, note1_idx, note2_idx, velocity_in);

        // Send strictly the modified index over Serial
        serial_send_note_on(v, velocity_in, note1_idx, note_flag);
      }
      break;
    }

    case 1:  // POLY
    {
      const uint8_t held = voiceAlloc.findNote(note1_idx);
      if (held != VOICE_ALLOC_NONE) {
        voice_mark_on(held, note1_idx, note2_idx, velocity_in);
        serial_send_note_on(held, velocity_in, note1_idx, NOTE_FLAG_RETRIGGER);
        return;
      }

      const uint8_t voice_num = voice_alloc();
      if (voice_num == VOICE_ALLOC_NONE) return;

      voice_mark_on(voice_num, note1_idx, note2_idx, velocity_in);
      serial_send_note_on(voice_num, velocity_in, note1_idx, NOTE_FLAG_RETRIGGER);
      break;
    }
  }
}

void SRAM_HOT(note_off)(uint8_t note) {
  uint8_t note1_idx, note2_idx;
  get_modified_indices(note, &note1_idx, &note2_idx);

  if (voiceMode == 0 || voiceMode == 2) {
    if (!monoStack.remove(note1_idx)) return;
    
    const uint8_t count = (voiceMode == 0) ? 1 : NUM_VOICES_TOTAL;

    if (monoStack.empty()) {
      mono_sounding_note = MONO_NOTE_NONE;
      for (uint8_t v = 0; v < count; v++) {
        voice_mark_off(v);
        serial_send_note_off(v);
      }
      return;
    }

    const uint8_t alloc_mode = voiceAlloc.mode();
    const uint8_t winner = monoStack.pick(alloc_mode); // This returns note1_idx
    if (winner == MONO_NOTE_NONE || winner == mono_sounding_note) return;

    mono_sounding_note = winner;
    
    // Smoothly reconstruct fallback indices using only the winner's modified OSC1 index
    const uint8_t w_idx1 = winner;
    const uint8_t w_idx2 = get_osc2_from_osc1(winner);
    
    const bool is_legato = (alloc_mode == 2);
    const uint8_t note_flag = is_legato ? NOTE_FLAG_PORTA_ONLY : NOTE_FLAG_RETRIGGER;

    for (uint8_t v = 0; v < count; v++) {
      if (is_legato) voice_mark_regate(v, w_idx1, w_idx2);
      else           voice_mark_on(v, w_idx1, w_idx2, velocity[0]);

      serial_send_note_on(v, velocity[0], w_idx1, note_flag);
    }
    return;
  }

  // POLYPHONIC RELEASE
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    // Exact match entirely on the modified version
    if (VOICE_NOTE_OSC1[i] == note1_idx && VOICES[i] != 0) {
      voice_mark_off((uint8_t)i);
      serial_send_note_off((uint8_t)i);
    }
  }
}

// ===========================================================================
// VOICE HARDWARE UPDATERS
// ===========================================================================
/**
 * @brief Hard note attack: stores pre-baked oscillator pitch indices,
 *        gates the voice active, triggers ADSR envelopes, and alerts Core 1.
 * 
 * @param voice       Physical voice index (0..NUM_VOICES_TOTAL - 1).
 * @param note1_idx   Pre-baked DCO pitch table index for OSC1 (0..TABLE_LEN - 1).
 * @param note2_idx   Pre-baked DCO pitch table index for OSC2 (includes interval).
 * @param velocity_in Key strike velocity (1..127).
 */
void SRAM_HOT(voice_mark_on)(uint8_t voice, uint8_t note1_idx, uint8_t note2_idx, uint8_t velocity_in) {
  //Serial.printf("   --> Voice Mark On: Voice=%d | PitchIdx1=%d | PitchIdx2=%d | Velocity=%d\n", voice, note1_idx, note2_idx, velocity_in);
  VOICES[voice] = 1;
  VOICE_NOTE_OSC1[voice] = note1_idx;
  VOICE_NOTE_OSC2[voice] = note2_idx;
  velocity[voice] = velocity_in;
  __dmb();

  note_on_flag[voice] = 1;
  noteStart[voice] = 1;
  noteEnd[voice] = 0;

  voiceAlloc.markOn(voice, note1_idx); // Allocator purely tracks the modified version
  adsr_note_on(voice);
  mod_matrix_on_note_on(voice);
}

/**
 * @brief Legato pitch regate: updates dual-oscillator pitch & portamento 
 *        WITHOUT restarting ADSR envelopes or resetting Mod Matrix LFOs.
 * 
 * Used during Mono/Unison overlapping notes and release fallback when smooth 
 * legato transitions are active.
 * 
 * @param voice     Physical voice channel index (0..NUM_VOICES_TOTAL - 1).
 * @param note1_idx Pre-baked DCO pitch table index for OSC1 (0..TABLE_LEN - 1).
 * @param note2_idx Pre-baked DCO pitch table index for OSC2 (includes interval).
 */
 void SRAM_HOT(voice_mark_regate)(uint8_t voice, uint8_t note1_idx, uint8_t note2_idx) {
  VOICES[voice] = 1;
  VOICE_NOTE_OSC1[voice] = note1_idx;
  VOICE_NOTE_OSC2[voice] = note2_idx;
  
  noteEnd[voice] = 0;
  
  voiceAlloc.regate(voice, note1_idx);
}

/**
 * @brief Gates off a voice into its ADSR release tail.
 * 
 * Note indices (VOICE_NOTE_OSC1 / VOICE_NOTE_OSC2) are intentionally kept
 * intact during release so the oscillator continues ringing at the released pitch
 * until the envelope decays completely to silence.
 * 
 * @param voice Physical voice channel index (0..NUM_VOICES_TOTAL - 1).
 */
 void SRAM_HOT(voice_mark_off)(uint8_t voice) {
  VOICES[voice] = 0;           // Clear active gate flag
  noteEnd[voice] = 1;          // Signal ADSR generator to enter Release stage
  noteStart[voice] = 0;        // Clear Attack trigger flag
  voiceAlloc.markOff(voice);   // Notify voice allocator that voice is releasing
  adsr_note_off(voice);        // Trigger hardware/software ADSR release phase
}


// Register note/CC/program/pitch-bend handlers on USB + DIN MIDI. USB begin is in init_usb().
void init_midi() {
  MIDI_USB.setHandleNoteOn(handleNoteOn);
  MIDI_USB.setHandleNoteOff(handleNoteOff);
  MIDI_USB.setHandleControlChange(handleControlChange);
  MIDI_USB.setHandleProgramChange(handleProgramChange);
  MIDI_USB.setHandlePitchBend(handlePitchBend);
  MIDI_USB.setHandleAfterTouchChannel(handleAfterTouchChannel);

  MIDI_SERIAL.begin(MIDI_CHANNEL_OMNI);
  MIDI_SERIAL.setHandleNoteOn(handleNoteOn);
  MIDI_SERIAL.setHandleNoteOff(handleNoteOff);
  MIDI_SERIAL.setHandleControlChange(handleControlChange);
  MIDI_SERIAL.setHandleProgramChange(handleProgramChange);
  MIDI_SERIAL.setHandlePitchBend(handlePitchBend);
  MIDI_SERIAL.setHandleAfterTouchChannel(handleAfterTouchChannel);

  MIDI_USB.turnThruOff();
  MIDI_SERIAL.turnThruOff();
}