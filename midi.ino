
// Init USB + DIN MIDI ports and register note/CC/program/pitch-bend handlers. Called from setup().
void init_midi() {
  MIDI_USB.begin(MIDI_CHANNEL_OMNI);
  MIDI_USB.setHandleNoteOn(handleNoteOn);
  MIDI_USB.setHandleNoteOff(handleNoteOff);
  MIDI_USB.setHandleControlChange(handleControlChange);
  MIDI_USB.setHandleProgramChange(handleProgramChange);
  MIDI_USB.setHandlePitchBend(handlePitchBend);


  MIDI_SERIAL.begin(MIDI_CHANNEL_OMNI);
  MIDI_SERIAL.setHandleNoteOn(handleNoteOn);
  MIDI_SERIAL.setHandleNoteOff(handleNoteOff);
  MIDI_SERIAL.setHandleControlChange(handleControlChange);
  MIDI_SERIAL.setHandleProgramChange(handleProgramChange);
  MIDI_SERIAL.setHandlePitchBend(handlePitchBend);
}


// MIDI library callback → note_on(). Invoked from loop via MIDI_*.read().
void handleNoteOn(byte channel, byte pitch, byte velocity) {
  note_on(pitch, velocity);
}
// MIDI library callback → note_off().
void handleNoteOff(byte channel, byte pitch, byte velocity) {
  note_off(pitch);
}

// MIDI CC handler (CC 42 sets pitch-bend range in semitones and updates Q24 multiplier).
void handleControlChange(byte channel, byte number, byte value) {
  // CC #42 is used to set the pitch bend range in semitones.
  if (number == 42) {
    pitchBendRange = value;
    // Optimized: Use fast fixed-point multiplication instead of float division.
    pitchBendMultiplier_q24 = (int32_t)(((int64_t)pitchBendRange * RECIP_TWELVE_Q24));
    pitchBendMultiplier = (float)pitchBendMultiplier_q24 / (float)(1 << 24);
  }
}

// MIDI program-change callback (currently unused / empty).
void handleProgramChange(byte channel, byte program) {
}

// MIDI pitch-bend callback → midi_pitch_bend (offset to 0..16383 style).
void handlePitchBend(byte channel, int pitchBend) {
  midi_pitch_bend = pitchBend + 8192;
}

// Allocate voice(s) from MIDI note-on per voiceMode/polyMode; set ADSR flags; notify mainboard.
void note_on(uint8_t note, uint8_t velocity) {

  switch (voiceMode) {
    case 0:
      VOICE_NOTES[0] = note;
      midi_velocity[0] = velocity;
      VOICES[0] = millis();
      note_on_flag[0] = 1;
      noteStart[0] = 1;
      serial_send_note_on(0, velocity, note - 36 + OSC1_interval);
      return;

      break;

    case 1:

      if (polyMode == 0) {
        if (STACK_VOICES < 2) {
          for (int i = 0; i < NUM_VOICES; i++)  // REVISAR!!
          {
            if (VOICE_NOTES[i] == note) {
              VOICES[i] = millis();
              midi_velocity[i] = velocity;
              note_on_flag[i] = 1;
              noteStart[i] = 1;
              serial_send_note_on(i, velocity, note - 36 + OSC1_interval);
              return;  // note already playing
            }
          }
        }

        for (int i = 0; i < STACK_VOICES; i++) {  // REVISAR!! Quizas debiera ser NUM_VOICES y no STACK
          uint8_t voice_num = get_free_voice();
          VOICES[voice_num] = millis();
          VOICE_NOTES[voice_num] = note;
          midi_velocity[voice_num] = velocity;
          note_on_flag[voice_num] = 1;
          noteStart[voice_num] = 1;
          serial_send_note_on(voice_num, velocity, note - 36 + OSC1_interval);
        }
      }

      if (polyMode == 1) {
        if (STACK_VOICES < 2) {
          for (int i = 0; i < NUM_VOICES; i++)  // REVISAR!!
          {
            if (VOICE_NOTES[i] == note) {
              VOICES[i] = 1;
              midi_velocity[i] = velocity;
              note_on_flag[i] = 1;
              noteStart[i] = 1;
              noteEnd[i] = 0;
              serial_send_note_on(i, velocity, note - 36 + OSC1_interval);
              return;  // note already playing
            }
          }
        }

        uint8_t voice_num = get_free_voice_sequential();
        VOICES[voice_num] = 1;
        VOICE_NOTES[voice_num] = note;
        midi_velocity[voice_num] = velocity;
        note_on_flag[voice_num] = 1;
        noteStart[voice_num] = 1;
        noteEnd[voice_num] = 0;
        serial_send_note_on(voice_num, velocity, note - 36 + OSC1_interval);
      }
      break;

    case 2:
      for (int i = 0; i < NUM_VOICES_TOTAL; i++)  // REVISAR!! // Previously NUM_VOICES
      {
        VOICES[i] = 1;
        VOICE_NOTES[i] = note;
        midi_velocity[i] = velocity;
        note_on_flag[i] = 1;
        noteStart[i] = 1;
        serial_send_note_on(i, velocity, note - 36 + OSC1_interval);
      }
      break;
    default:
      return;
      break;
  }
  last_midi_pitch_bend = 0;
}

// Release matching voice(s) on MIDI note-off; set noteEnd flags; notify mainboard.
void note_off(uint8_t note) {
  // gate off
  for (int i = 0; i < NUM_VOICES_TOTAL; i++)  // REVISAR!! // Previously NUM_VOICES
  {
    if (VOICE_NOTES[i] == note) {
      // gpio_put(GATE_PINS[i], 0);
      // VOICE_NOTES[i] = 0;
      VOICES[i] = 0;
      noteEnd[i] = 1;
      noteStart[i] = 0;
      serial_send_note_off(i);
    }
  }
}