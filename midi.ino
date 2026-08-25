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

uint8_t velocity[NUM_VOICES_TOTAL];

// MIDI library callback → note_on(). Invoked from loop via MIDI_*.read().
void handleNoteOn(byte channel, byte pitch, byte velocity) {
  note_on(pitch, velocity);
}
// MIDI library callback → note_off().
void handleNoteOff(byte channel, byte pitch, byte velocity) {
  note_off(pitch);
}

// MIDI CC handler: CC 0/32 latch preset bank, CC 42 sets pitch-bend range,
// everything else goes through the generated map in midi_cc_map.h.
void handleControlChange(byte channel, byte number, byte value) {
  // CC 1 (mod wheel) → mod matrix source 11.
  if (number == 1) {
    midi_mod_wheel = value;
    mod_matrix_set_mod_wheel(value);
    serial_send_expression();
    return;
  }
  // CC 0 (Bank Select MSB) / CC 32 (LSB): latch the upper/lower 128-slot bank
  // for the next Program Change. Nonzero = bank 1 (slots 128..255).
  if (number == 0 || number == 32) {
    midiPresetBank = (value != 0) ? 1 : 0;
    return;
  }
  // CC #42 is used to set the pitch bend range in semitones.
  if (number == MIDI_CC_PITCH_BEND_RANGE) {
    pitchBendRange = value;
    // Optimized: Use fast fixed-point multiplication instead of float division.
    pitchBendMultiplier_q24 = (int32_t)(((int64_t)pitchBendRange * RECIP_TWELVE_Q24));
    pitchBendMultiplier = (float)pitchBendMultiplier_q24 / (float)(1 << 24);
    return;
  }
  midi_cc_handle(number, value);
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
void midi_cc_apply(uint8_t target, int16_t value) {
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
      serial_echo_persistable_param16(target, value);
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

/**
 * @brief Allocates voice(s) upon receiving a MIDI Note-On event and dispatches to Mainboard.
 * 
 * Performance Modes (voiceMode):
 *  - Mode 0 (MONO): 1 physical voice slot (Voice 0, DCO 0 & 1).
 *  - Mode 1 (POLY): Dynamic allocation across all NUM_VOICES_TOTAL slots.
 *  - Mode 2 (UNISON / STACK): All NUM_VOICES_TOTAL slots track the same note with detune.
 * 
 * Allocation Policies (alloc_mode = voiceAlloc.mode()):
 *  - Mode 2 (LEGATO): If a key is already sounding, subsequent overlapping keys glide
 *    to the new pitch WITHOUT retriggering ADSR envelopes (NOTE_FLAG_PORTA_ONLY).
 *  - Other Modes: Multi-trigger re-attack on every key strike (NOTE_FLAG_RETRIGGER).
 * 
 * @param note        MIDI note number (0..127).
 * @param velocity_in MIDI velocity (1..127).
 */
 void note_on(uint8_t note, uint8_t velocity_in) {
  const uint8_t alloc_mode = voiceAlloc.mode();

  switch (voiceMode) {
    // =========================================================================
    // MONOPHONE & UNISON MODES (Shared Held-Key Priority Stack)
    // =========================================================================
    case 0:  // MONO (1 voice)
    case 2:  // STACK / UNISON (all 4 voices playing the same note)
    {
      // 1. Push note to the held-key stack.
      // Returns false if VOICE_ALLOC_NO_STEAL denies adding a new key while held.
      if (!monoStack.push(note, alloc_mode)) return;

      // 2. Determine which held key wins priority (Last, First, Low, High, etc.)
      const uint8_t winner = monoStack.pick(alloc_mode);

      // If priority did not change (e.g. struck a lower key in High-Note priority), ignore
      if (winner == MONO_NOTE_NONE || winner == mono_sounding_note) return;

      // 3. Legato Check:
      // Active ONLY in Alloc Mode 2 when another key is already held/sounding.
      const bool is_legato = (alloc_mode == 2) && (mono_sounding_note != MONO_NOTE_NONE);
      mono_sounding_note = winner;

      // Number of voice slots to update (Mono = 1, Unison = all 4 voices)
      const uint8_t count = (voiceMode == 0) ? 1 : NUM_VOICES_TOTAL;
      const uint8_t note_flag = is_legato ? NOTE_FLAG_PORTA_ONLY : NOTE_FLAG_RETRIGGER;

      for (uint8_t v = 0; v < count; v++) {
        if (is_legato) {
          // Pitch shift / glide only; keep ADSR envelopes in sustain/decay
          voice_mark_regate(v, winner);
        } else {
          // Hard strike; restart ADSR envelopes and mod matrix triggers
          voice_mark_on(v, winner, velocity_in);
        }

        // Notify Mainboard UART hub to gate/retrigger its analog CVs
        serial_send_note_on(v, velocity_in, winner, note_flag);
      }
      break;
    }

    // =========================================================================
    // POLYPHONIC MODE (Dynamic Voice Stealing & Allocation)
    // =========================================================================
    case 1:  // POLY
    {
      // 1. Same-Note Retrigger:
      // If this note is already ringing in a voice slot, reuse it instead of stealing.
      const uint8_t held = voiceAlloc.findNote(note);
      if (held != VOICE_ALLOC_NONE) {
        voice_mark_on(held, note, velocity_in);
        serial_send_note_on(held, velocity_in, note, NOTE_FLAG_RETRIGGER);
        return;
      }

      // 2. Allocate a free or stolen voice per the active steal policy
      const uint8_t voice_num = voice_alloc();
      if (voice_num == VOICE_ALLOC_NONE) return; // Dropped under NO_STEAL policy

      // 3. Mark voice active and notify Mainboard
      voice_mark_on(voice_num, note, velocity_in);
      serial_send_note_on(voice_num, velocity_in, note, NOTE_FLAG_RETRIGGER);
      break;
    }
  }

  // Reset global MIDI pitch bend tracking center on fresh key strikes
  last_midi_pitch_bend = 0;
}

/**
 * @brief Handles MIDI Note-Off release events.
 * 
 *  - In Mono/Unison: Checks if other keys are still held. If so, glides/retriggers
 *    back to the previous note. If no keys remain, gates the voices off.
 *  - In Poly: Finds the active voice matching this note and gates it off into release.
 * 
 * @param note MIDI note number released (0..127).
 */
void note_off(uint8_t note) {
  // ===========================================================================
  // MONOPHONE & UNISON MODES
  // ===========================================================================
  if (voiceMode == 0 || voiceMode == 2) {
    // Remove key from stack. Returns false if the released note wasn't in the stack.
    if (!monoStack.remove(note)) return;

    const uint8_t count = (voiceMode == 0) ? 1 : NUM_VOICES_TOTAL;

    // CASE A: All keys released -> Gate off into the envelope release stage
    if (monoStack.empty()) {
      mono_sounding_note = MONO_NOTE_NONE;
      for (uint8_t v = 0; v < count; v++) {
        voice_mark_off(v);
        serial_send_note_off(v);
      }
      return;
    }

    // CASE B: Other keys still held -> Fallback to next priority winner
    const uint8_t alloc_mode = voiceAlloc.mode();
    const uint8_t winner = monoStack.pick(alloc_mode);

    // If releasing a key that wasn't currently sounding, do nothing to the active pitch
    if (winner == MONO_NOTE_NONE || winner == mono_sounding_note) return;

    mono_sounding_note = winner;

    // In Legato mode (2), fall back smoothly without re-attacking envelopes
    const bool is_legato = (alloc_mode == 2);
    const uint8_t note_flag = is_legato ? NOTE_FLAG_PORTA_ONLY : NOTE_FLAG_RETRIGGER;

    for (uint8_t v = 0; v < count; v++) {
      if (is_legato) {
        voice_mark_regate(v, winner);
      } else {
        voice_mark_on(v, winner, velocity[0]);
      }
      serial_send_note_on(v, velocity[0], winner, note_flag);
    }
    return;
  }

  // ===========================================================================
  // POLYPHONIC MODE
  // ===========================================================================
  // Scan all voice slots so no release is missed if NUM_VOICES was changed mid-note
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    if (VOICE_NOTES[i] == note && VOICES[i] != 0) {
      voice_mark_off((uint8_t)i);
      serial_send_note_off(i);
    }
  }
}