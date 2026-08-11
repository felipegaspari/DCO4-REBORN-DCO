// Serial1 = MIDI DIN @ 31250; Serial2 = Mainboard @ 2.5M.
// USB CDC still accepts Input-style 'a'..'d'/'p'/'q' for bench without the panel.
// Analog VCA/VCF CVs live on Mainboard: USB 'a'/'b'/'d' are mirrored on Serial2.

// USB uses the Input-style LUT. Serial2 uses the Mainboard LUT.
// Tag the drain so USB 'p'/'a'/'b'/'d' can mirror to Mainboard without echoing MB→DCO.
enum ParamIngress : uint8_t {
  PARAM_SRC_INPUT = 0,
  PARAM_SRC_USB   = 1,
};
static ParamIngress g_param_ingress = PARAM_SRC_INPUT;

static void serial_forward_input_block_to_mb(char cmd, const uint8_t* payload, uint8_t len) {
  if (g_param_ingress != PARAM_SRC_USB) return;
  if (Serial2.availableForWrite() < 1) return;
  serial_frame_write(Serial2, (uint8_t)cmd, payload, len);
}

static void serial_send_adsr_block_to_mb(uint8_t cmd, uint16_t a, uint16_t d, uint16_t s, uint16_t r) {
  if (Serial2.availableForWrite() < 1) return;
  uint8_t payload[INPUT_SERIAL_LEN_ADSR_BLOCK];
  encode_u16_le(payload + 0, a);
  encode_u16_le(payload + 2, d);
  encode_u16_le(payload + 4, s);
  encode_u16_le(payload + 6, r);
  serial_frame_write(Serial2, cmd, payload, INPUT_SERIAL_LEN_ADSR_BLOCK);
}

void serial_send_adsr_vca_block_to_mb() {
  serial_send_adsr_block_to_mb(
    INPUT_CMD_ADSR1_BLOCK,
    ADSR_VCA_attack, ADSR_VCA_decay, ADSR_VCA_sustain, ADSR_VCA_release);
}

void serial_send_adsr_vcf_block_to_mb() {
  serial_send_adsr_block_to_mb(
    INPUT_CMD_ADSR2_BLOCK,
    ADSR_VCF_attack, ADSR_VCF_decay, ADSR_VCF_sustain, ADSR_VCF_release);
}

void serial_send_filter_block_to_mb() {
  if (Serial2.availableForWrite() < 1) return;
  uint8_t payload[INPUT_SERIAL_LEN_FILTER_BLOCK];
  encode_u16_le(payload + 0, CUTOFF);
  encode_u16_le(payload + 2, RESONANCE);
  encode_u16_le(payload + 4, (uint16_t)ADSR2toVCF);
  encode_u16_le(payload + 6, LFO2toVCF);
  serial_frame_write(Serial2, INPUT_CMD_FILTER_BLOCK, payload, INPUT_SERIAL_LEN_FILTER_BLOCK);
}

void serial_send_preset_loaded_to_mb(uint8_t slot) {
  if (Serial2.availableForWrite() < 1) return;
  uint8_t payload[INPUT_SERIAL_LEN_PRESET_LOADED] = { slot };
  serial_frame_write(Serial2, INPUT_CMD_PRESET_LOADED, payload, INPUT_SERIAL_LEN_PRESET_LOADED);
}

static void input_handle_adsr1(char cmd, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_ADSR_BLOCK) return;

  uint16_t dirty = 0;
  uint16_t v;

  v = decode_u16_le(payload + 0);
  if (v != ADSR_VCA_attack)  { ADSR_VCA_attack  = v; dirty |= ADSR_DIRTY_VCA_A; }

  v = decode_u16_le(payload + 2);
  if (v != ADSR_VCA_decay)   { ADSR_VCA_decay   = v; dirty |= ADSR_DIRTY_VCA_D; }

  v = decode_u16_le(payload + 4);
  if (v != ADSR_VCA_sustain) { ADSR_VCA_sustain = v; dirty |= ADSR_DIRTY_VCA_S; }

  v = decode_u16_le(payload + 6);
  if (v != ADSR_VCA_release) { ADSR_VCA_release = v; dirty |= ADSR_DIRTY_VCA_R; }

  if (dirty) mark_adsr_params_dirty(dirty);
  serial_forward_input_block_to_mb(cmd, payload, len);
}

static void input_handle_adsr2(char cmd, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_ADSR_BLOCK) return;

  uint16_t dirty = 0;
  uint16_t v;

  v = decode_u16_le(payload + 0);
  if (v != ADSR_VCF_attack)  { ADSR_VCF_attack  = v; dirty |= ADSR_DIRTY_VCF_A; }

  v = decode_u16_le(payload + 2);
  if (v != ADSR_VCF_decay)   { ADSR_VCF_decay   = v; dirty |= ADSR_DIRTY_VCF_D; }

  v = decode_u16_le(payload + 4);
  if (v != ADSR_VCF_sustain) { ADSR_VCF_sustain = v; dirty |= ADSR_DIRTY_VCF_S; }

  v = decode_u16_le(payload + 6);
  if (v != ADSR_VCF_release) { ADSR_VCF_release = v; dirty |= ADSR_DIRTY_VCF_R; }

  if (dirty) mark_adsr_params_dirty(dirty);
  serial_forward_input_block_to_mb(cmd, payload, len);
}

// EnvDCO times ('c') → existing ADSR1_* engine (pitch/PW). Stays DCO-local.
static void input_handle_adsr3(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_ADSR_BLOCK) return;

  uint16_t dirty = 0;
  uint16_t v;

  v = decode_u16_le(payload + 0);
  if (v != ADSR1_attack)  { ADSR1_attack  = v; dirty |= ADSR_DIRTY_DCO_A; }

  v = decode_u16_le(payload + 2);
  if (v != ADSR1_decay)   { ADSR1_decay   = v; dirty |= ADSR_DIRTY_DCO_D; }

  v = decode_u16_le(payload + 4);
  if (v != ADSR1_sustain) { ADSR1_sustain = v; dirty |= ADSR_DIRTY_DCO_S; }

  v = decode_u16_le(payload + 6);
  if (v != ADSR1_release) { ADSR1_release = v; dirty |= ADSR_DIRTY_DCO_R; }

  if (dirty) mark_adsr_params_dirty(dirty);
}

static void input_handle_filter_block(char cmd, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_FILTER_BLOCK) return;
  CUTOFF     = decode_u16_le(payload + 0);
  RESONANCE  = decode_u16_le(payload + 2);
  ADSR2toVCF = decode_i16_le(payload + 4);
  LFO2toVCF  = decode_u16_le(payload + 6);
  cv_bake_adsr2_to_vcf_scale();
  cv_bake_lfo2_to_vcf_scale();
  serial_forward_input_block_to_mb(cmd, payload, len);
}

// The persistable patch-param set lives in preset_store.h now
// (preset_param_is_persistable), shared with the preset shadow capture.

void serialSendParam16(byte paramNumber, int16_t paramValue, bool force) {
  if (!force && Serial2.availableForWrite() < 1) {
    return;
  }
  uint8_t payload[INPUT_SERIAL_LEN_PARAM_16];
  encode_param_p(payload, (uint8_t)paramNumber, paramValue);
  serial_frame_write(Serial2, INPUT_CMD_PARAM_16, payload, INPUT_SERIAL_LEN_PARAM_16);
}

void serial_echo_persistable_param16(uint8_t id, int16_t value) {
  if (!preset_param_is_persistable(id)) {
    return;
  }
  serialSendParam16(id, value);
}

static void input_handle_param16(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PARAM_16) return;
  ParamFrame frame;
  decode_param_p(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
  if (g_param_ingress != PARAM_SRC_INPUT) {
    serial_echo_persistable_param16(frame.id, (int16_t)frame.value);
  }
}

static void input_handle_preset_name(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PRESET_NAME) return;
  for (int i = 0; i < 16; ++i) {
    presetName[i] = payload[i];
  }
}

// Bulk restore staging ('B') and commit ('C') → preset_store.ino.
static void input_handle_bulk_chunk(char, const uint8_t* payload, uint8_t len) {
  preset_bulk_chunk(payload, len);
}

static void input_handle_bulk_commit(char, const uint8_t* payload, uint8_t len) {
  preset_bulk_commit(payload, len);
}

// 'N': Input asking for the whole preset directory → preset_store.ino. Arrives
// relayed by the Mainboard; the 256 'O' answers go back the same way.
static void input_handle_preset_dir_request(char, const uint8_t*, uint8_t) {
  preset_store_send_directory_to_mb();
}

static const SerialCommandDef inputSerialCommands[] = {
  { INPUT_CMD_ADSR1_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr1        },
  { INPUT_CMD_ADSR2_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr2        },
  { INPUT_CMD_ADSR3_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr3        },
  { INPUT_CMD_FILTER_BLOCK,  INPUT_SERIAL_LEN_FILTER_BLOCK, input_handle_filter_block },
  { INPUT_CMD_PARAM_16,      INPUT_SERIAL_LEN_PARAM_16,     input_handle_param16      },
  { INPUT_CMD_PRESET_NAME,   INPUT_SERIAL_LEN_PRESET_NAME,  input_handle_preset_name  },
  { INPUT_CMD_BULK_CHUNK,    INPUT_SERIAL_LEN_BULK_CHUNK,   input_handle_bulk_chunk   },
  { INPUT_CMD_BULK_COMMIT,   INPUT_SERIAL_LEN_BULK_COMMIT,  input_handle_bulk_commit  },
};

static void mb_handle_param16(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PARAM_16) return;
  ParamFrame frame;
  decode_param_p(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

#define MB_BENCH_RING_CAP 2048
static uint8_t mb_bench_ring[MB_BENCH_RING_CAP];
static uint16_t mb_bench_ring_head = 0;
static uint16_t mb_bench_ring_tail = 0;
static uint16_t mb_bench_ring_count = 0;

static void mb_handle_bench_text(char, const uint8_t* payload, uint8_t len) {
  if (len < 1) return;
  uint8_t n = payload[0];
  if (n > SERIAL_BENCH_TEXT_DATA_MAX) n = SERIAL_BENCH_TEXT_DATA_MAX;
  if ((uint8_t)(n + 1u) > len) n = (uint8_t)(len - 1u);
  if ((uint16_t)(mb_bench_ring_count + n) > MB_BENCH_RING_CAP) return;
  for (uint8_t i = 0; i < n; i++) {
    mb_bench_ring[mb_bench_ring_head] = payload[1 + i];
    mb_bench_ring_head = (uint16_t)((mb_bench_ring_head + 1u) % MB_BENCH_RING_CAP);
    mb_bench_ring_count++;
  }
}

void mb_bench_text_drain() {
  if (mb_bench_ring_count == 0u) return;
  int avail = Serial.availableForWrite();
  if (avail <= 0) return;

  uint16_t n = mb_bench_ring_count;
  if (n > 256u) n = 256u;
  if ((uint16_t)avail < n) n = (uint16_t)avail;

  uint16_t first = (uint16_t)(MB_BENCH_RING_CAP - mb_bench_ring_tail);
  if (first > n) first = n;
  Serial.write(mb_bench_ring + mb_bench_ring_tail, first);
  mb_bench_ring_tail = (uint16_t)((mb_bench_ring_tail + first) % MB_BENCH_RING_CAP);
  mb_bench_ring_count = (uint16_t)(mb_bench_ring_count - first);
  n = (uint16_t)(n - first);
  if (n > 0u) {
    Serial.write(mb_bench_ring + mb_bench_ring_tail, n);
    mb_bench_ring_tail = (uint16_t)((mb_bench_ring_tail + n) % MB_BENCH_RING_CAP);
    mb_bench_ring_count = (uint16_t)(mb_bench_ring_count - n);
  }
}

static void mb_handle_mod_stream(char, const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_PAYLOAD_LEN_MOD_STREAM) return;
  LFO1Level = (int16_t)decode_u16_le(payload + 0);
  LFO2Level = (int16_t)decode_u16_le(payload + 2);
  for (uint8_t i = 0; i < 4 && i < NUM_VOICES_TOTAL; i++) {
    ADSR1Level_q15[i] = (int16_t)decode_u16_le(payload + 4 + i * 2);
  }
  matrix_pitch_mod_q24 = (int32_t)decode_u32_le(payload + 12);
#ifdef ENABLE_MB_MOD_STREAM
  if (LFO1toOSC1_q24 == 0 && LFO1toOSC2_q24 == 0 && LFO1toOSC3_q24 == 0) {
    const int32_t m = applyDepthQ24(LFO1Level, LFO1toDCO_q24);
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC1] = m;
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC2] = m;
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC3] = m;
  } else {
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC1] =
      applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC1_q24);
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC2] =
      applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC2_q24);
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC3] =
      applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC3_q24);
  }
  lfo2_pitch_mod_q24[LFO2_PITCH_OSC2] =
    applyDepthQ24(LFO2Level, LFO2toOSC2_q24 + LFO2toOSC2_coarse_q24);
  lfo2_pitch_mod_q24[LFO2_PITCH_OSC3] =
    applyDepthQ24(LFO2Level, LFO2toOSC3_q24 + LFO2toOSC3_coarse_q24);
  __dmb();
#endif
}

static const SerialCommandDef mainboardSerialCommands[] = {
  { SERIAL_CMD_PARAM_16,    INPUT_SERIAL_LEN_PARAM_16,      mb_handle_param16     },
  { SERIAL_CMD_MOD_STREAM,  SERIAL_PAYLOAD_LEN_MOD_STREAM,  mb_handle_mod_stream  },
  { SERIAL_CMD_BENCH_TEXT,  SERIAL_PAYLOAD_LEN_BENCH_TEXT,  mb_handle_bench_text  },
  // Panel-origin frames the Mainboard passes through. The DCO owns the preset
  // store but has no direct link to Input here, so it has to shadow the panel's
  // envelope/filter blocks and preset name to build an accurate record.
  // These handlers only re-emit on USB ingress, so nothing bounces back.
  { INPUT_CMD_ADSR1_BLOCK,  INPUT_SERIAL_LEN_ADSR_BLOCK,    input_handle_adsr1        },
  { INPUT_CMD_ADSR2_BLOCK,  INPUT_SERIAL_LEN_ADSR_BLOCK,    input_handle_adsr2        },
  { INPUT_CMD_ADSR3_BLOCK,  INPUT_SERIAL_LEN_ADSR_BLOCK,    input_handle_adsr3        },
  { INPUT_CMD_FILTER_BLOCK, INPUT_SERIAL_LEN_FILTER_BLOCK,  input_handle_filter_block },
  { INPUT_CMD_PRESET_NAME,  INPUT_SERIAL_LEN_PRESET_NAME,   input_handle_preset_name  },
  { INPUT_CMD_PRESET_DIR_REQUEST, INPUT_SERIAL_LEN_PRESET_DIR_REQUEST, input_handle_preset_dir_request },
};

static SerialCommandTable inputSerialLut;
static SerialParserContext inputSerialParser = {};
static SerialCommandTable mainboardSerialLut;
static SerialParserContext mainboardSerialParser = {};

void init_serial() {
  Serial1.setFIFOSize(256);
  Serial1.setPollingMode(false);
  Serial1.setRX(1);
  Serial1.setTX(0);
  Serial1.begin(31250);

  Serial2.setFIFOSize(2048);
  Serial2.setPollingMode(false);
  Serial2.setRX(21);
  Serial2.setTX(20);
  Serial2.begin(2500000);

  serial_command_table_init(
    inputSerialLut,
    inputSerialCommands,
    sizeof(inputSerialCommands) / sizeof(inputSerialCommands[0])
  );
  serial_command_table_init(
    mainboardSerialLut,
    mainboardSerialCommands,
    sizeof(mainboardSerialCommands) / sizeof(mainboardSerialCommands[0])
  );
}

// USB composite: CDC serial + MIDI. Descriptors first, then detach/attach so the
// host re-enumerates after flash/soft reset (avoids missing /dev/ttyACM* on Linux).
void init_usb() {
  USBDevice.setManufacturerDescriptor("FELA         ");
  USBDevice.setProductDescriptor("DCO4-REBORN ");

  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  Serial.begin(2000000);
  usb_midi.setStringDescriptor("DCO4-REBORN MIDI");
  MIDI_USB.begin(MIDI_CHANNEL_OMNI);

  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
}

void __not_in_flash_func(serial_panel_task)() {
  g_param_ingress = PARAM_SRC_INPUT;
  serial_parser_drain(
    mainboardSerialParser,
    mainboardSerialLut,
    Serial2,
    SERIAL_DRAIN_BYTE_BUDGET
  );
}

// USB CDC bench link: same inner frames as Serial2. Only host → DCO is framed;
// DCO → host stays plain debug text.
#ifdef ENABLE_USB_CONTROL

static SerialParserContext usbSerialParser = {};

void __not_in_flash_func(serial_usb_task)() {
  if (!Serial) return;
  g_param_ingress = PARAM_SRC_USB;
  serial_parser_drain(
    usbSerialParser,
    inputSerialLut,
    Serial,
    SERIAL_DRAIN_BYTE_BUDGET
  );
}

#endif  // ENABLE_USB_CONTROL

// TX slim 'x' to Mainboard: gap (154) and cal offsets (155). MB relays to Input → Screen.
// Drop the frame if Serial2 TX is not ready (USB-only bench with no Mainboard).
// Persistable USB/MIDI 'p' echo is serialSendParam16 / serial_echo_persistable_param16.
void serialSendParam32(byte paramNumber, uint32_t paramValue) {
  if (Serial2.availableForWrite() < 1) {
    return;
  }
  uint8_t payload[INPUT_SERIAL_LEN_PARAM_32];
  encode_param32(payload, (uint8_t)paramNumber, paramValue);
  serial_frame_write(Serial2, INPUT_CMD_PARAM_32, payload, INPUT_SERIAL_LEN_PARAM_32);
}

void serial_send_note_on(uint8_t voice, uint8_t velocity, uint8_t note, uint8_t flags) {
  if (Serial2.availableForWrite() < 1) {
    return;
  }
  uint8_t payload[SERIAL_PAYLOAD_LEN_NOTE_ON] = { voice, velocity, note, flags };
  serial_frame_write(Serial2, SERIAL_CMD_NOTE_ON, payload, SERIAL_PAYLOAD_LEN_NOTE_ON);
}

void serial_send_note_off(uint8_t voice) {
  if (Serial2.availableForWrite() < 1) {
    return;
  }
  serial_frame_write(Serial2, SERIAL_CMD_NOTE_OFF, &voice, SERIAL_PAYLOAD_LEN_NOTE_OFF);
}

void serial_send_expression() {
  if (Serial2.availableForWrite() < 1) {
    return;
  }
  uint8_t payload[SERIAL_PAYLOAD_LEN_EXPRESSION];
  payload[0] = midi_aftertouch;
  payload[1] = midi_mod_wheel;
  encode_u16_le(payload + 2, (uint16_t)midi_pitch_bend);
  serial_frame_write(Serial2, SERIAL_CMD_EXPRESSION, payload, SERIAL_PAYLOAD_LEN_EXPRESSION);
}
