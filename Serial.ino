// Serial1 = MIDI DIN @ 31250; Serial2 = Input hub @ 2.5M (inner panel protocol).
// Screen has no DCO port: gap 'x' rides the Input link and Input relays it.

static void input_handle_adsr1(char, const uint8_t* payload, uint8_t len) {
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
}

static void input_handle_adsr2(char, const uint8_t* payload, uint8_t len) {
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
}

// EnvDCO times ('c') → existing ADSR1_* engine (pitch/PW)
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

static void input_handle_filter_block(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_FILTER_BLOCK) return;
  CUTOFF     = decode_u16_le(payload + 0);
  RESONANCE  = decode_u16_le(payload + 2);
  ADSR2toVCF = decode_i16_le(payload + 4);
  LFO2toVCF  = decode_u16_le(payload + 6);
  cv_bake_adsr2_to_vcf_scale();
  cv_bake_lfo2_to_vcf_scale();
}

static void input_handle_param16(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PARAM_16) return;
  ParamFrame frame;
  decode_param_p(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static void input_handle_preset_name(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PRESET_NAME) return;
  for (int i = 0; i < 8; ++i) {
    presetName[i] = payload[i];
  }
}

static const SerialCommandDef inputSerialCommands[] = {
  { INPUT_CMD_ADSR1_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr1        },
  { INPUT_CMD_ADSR2_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr2        },
  { INPUT_CMD_ADSR3_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr3        },
  { INPUT_CMD_FILTER_BLOCK,  INPUT_SERIAL_LEN_FILTER_BLOCK, input_handle_filter_block },
  { INPUT_CMD_PARAM_16,      INPUT_SERIAL_LEN_PARAM_16,     input_handle_param16      },
  { INPUT_CMD_PRESET_NAME,   INPUT_SERIAL_LEN_PRESET_NAME,  input_handle_preset_name  },
};

static SerialCommandTable inputSerialLut;
static SerialParserContext inputSerialParser = {};

void init_serial() {
  Serial1.setFIFOSize(256);
  Serial1.setPollingMode(false);
  Serial1.setRX(1);
  Serial1.setTX(0);
  Serial1.begin(31250);

  Serial2.setFIFOSize(512);
  Serial2.setPollingMode(false);
  Serial2.setRX(21);
  Serial2.setTX(20);
  Serial2.begin(2500000);

  serial_command_table_init(
    inputSerialLut,
    inputSerialCommands,
    sizeof(inputSerialCommands) / sizeof(inputSerialCommands[0])
  );
}

// USB composite: CDC serial + MIDI. Descriptors first, then detach/attach so the
// host re-enumerates after flash/soft reset (avoids missing /dev/ttyACM* on Linux).
void init_usb() {
  USBDevice.setManufacturerDescriptor("FELA         ");
  USBDevice.setProductDescriptor("DCO3-MONO   ");

  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  Serial.begin(2000000);
  usb_midi.setStringDescriptor("DCO3-MONO MIDI");
  MIDI_USB.begin(MIDI_CHANNEL_OMNI);

  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
}

void __not_in_flash_func(serial_panel_task)() {
  serial_parser_drain(
    inputSerialParser,
    inputSerialLut,
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
  serial_parser_drain(
    usbSerialParser,
    inputSerialLut,
    Serial,
    SERIAL_DRAIN_BYTE_BUDGET
  );
}

#endif  // ENABLE_USB_CONTROL

// TX slim 'x' to Input: gap (154) and cal offsets (155). Input relays 154 on to Screen.
// Drop the frame if Serial2 TX is not ready (USB-only bench with no Input board).
void serialSendParam32(byte paramNumber, uint32_t paramValue) {
  if (Serial2.availableForWrite() < 1) {
    return;
  }
  uint8_t payload[INPUT_SERIAL_LEN_PARAM_32];
  encode_param32(payload, (uint8_t)paramNumber, paramValue);
  serial_frame_write(Serial2, INPUT_CMD_PARAM_32, payload, INPUT_SERIAL_LEN_PARAM_32);
}
