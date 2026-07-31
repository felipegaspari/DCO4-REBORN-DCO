// Configure Serial1 (MIDI DIN @ 31250) and Serial2 (Input hub @ 2.5M).
// Screen has no DCO port: gap 'x' rides the Input link and Input relays it.

void init_serial() {
  Serial1.setFIFOSize(256);
  Serial1.setPollingMode(true);
  Serial1.setRX(1);
  Serial1.setTX(0);
  Serial1.begin(31250);

  Serial2.setFIFOSize(512);
  Serial2.setPollingMode(false);
  Serial2.setRX(21);
  Serial2.setTX(20);
  Serial2.begin(2500000);

  Serial.begin(2000000);
}

/// -------------------------------
// Serial2: Input panel protocol
// -------------------------------

// EnvVCA times ('a')
static void input_handle_adsr1(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_ADSR_BLOCK) return;
  ADSR_VCA_attack  = word(payload[0], payload[1]);
  ADSR_VCA_decay   = word(payload[2], payload[3]);
  ADSR_VCA_sustain = word(payload[4], payload[5]);
  ADSR_VCA_release = word(payload[6], payload[7]);
}

// EnvVCF times ('b')
static void input_handle_adsr2(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_ADSR_BLOCK) return;
  ADSR_VCF_attack  = word(payload[0], payload[1]);
  ADSR_VCF_decay   = word(payload[2], payload[3]);
  ADSR_VCF_sustain = word(payload[4], payload[5]);
  ADSR_VCF_release = word(payload[6], payload[7]);
}

// EnvDCO times ('c') → existing ADSR1_* engine (pitch/PW)
static void input_handle_adsr3(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_ADSR_BLOCK) return;
  ADSR1_attack  = word(payload[0], payload[1]);
  ADSR1_decay   = word(payload[2], payload[3]);
  ADSR1_sustain = word(payload[4], payload[5]);
  ADSR1_release = word(payload[6], payload[7]);
}

static void input_handle_filter_block(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_FILTER_BLOCK) return;
  CUTOFF     = word(payload[0], payload[1]);
  RESONANCE  = word(payload[2], payload[3]);
  ADSR2toVCF = (int16_t)word(payload[4], payload[5]);
  LFO2toVCF  = word(payload[6], payload[7]);
  cv_update_mod_formulas();
}

static void input_handle_adsr1_to_vca(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_ADSR1_TO_VCA) return;
  ADSR1toVCA = (int16_t)word(payload[0], payload[1]);
}

// Input 'f' is big-endian PW (0..4095-ish); voice engine uses PW[0] at /4 scale
static void input_handle_pw(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PW_VALUE) return;
  uint16_t pwRaw = word(payload[0], payload[1]);
  PW[0] = pwRaw / 4;
}

static void input_handle_param16(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PARAM_16) return;
  ParamFrame frame;
  decode_param_p(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static void input_handle_param8(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PARAM_8) return;
  ParamFrame frame;
  decode_param_w(payload, frame);
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
  { INPUT_CMD_ADSR1_TO_VCA,  INPUT_SERIAL_LEN_ADSR1_TO_VCA, input_handle_adsr1_to_vca },
  { INPUT_CMD_PW_VALUE,      INPUT_SERIAL_LEN_PW_VALUE,     input_handle_pw           },
  { INPUT_CMD_PARAM_16,      INPUT_SERIAL_LEN_PARAM_16,     input_handle_param16      },
  { INPUT_CMD_PARAM_8,       INPUT_SERIAL_LEN_PARAM_8,      input_handle_param8       },
  { INPUT_CMD_PRESET_NAME,   INPUT_SERIAL_LEN_PRESET_NAME,  input_handle_preset_name  },
};

static SerialParserContext inputSerialParser = {
  SERIAL_WAIT_FOR_CMD, 0, nullptr, {0}, 0, 0, 0
};

void serial_panel_task() {
  if (inputSerialParser.state == SERIAL_READ_PAYLOAD) {
    uint32_t now = micros();
    serial_parser_check_timeout(inputSerialParser, now);
  }
  if (Serial2.available() > 0) {
    uint32_t now = micros();
    while (Serial2.available() > 0) {
      uint8_t b = Serial2.read();
      serial_parser_process_byte(
        inputSerialParser,
        inputSerialCommands,
        sizeof(inputSerialCommands) / sizeof(inputSerialCommands[0]),
        b,
        now
      );
    }
  }
}

// -------------------------------
// USB CDC: bench control link
// -------------------------------
// Speaks the same Input panel frames as Serial2, so a host app can drive the board
// with no Input or Screen attached. The parser takes its context and command table as
// arguments, so this is the panel link's table with a second, independent context.
//
// Only host -> DCO is framed. DCO -> host stays plain debug text, so the two never
// interleave badly: text going out cannot be mistaken for a frame coming in.
#ifdef ENABLE_USB_CONTROL

static SerialParserContext usbSerialParser = {
  SERIAL_WAIT_FOR_CMD, 0, nullptr, {0}, 0, 0, 0
};

// Drain the USB CDC RX buffer into the panel-protocol parser. Called from loop().
void serial_usb_task() {
  if (usbSerialParser.state == SERIAL_READ_PAYLOAD) {
    serial_parser_check_timeout(usbSerialParser, micros());
  }
  while (Serial.available() > 0) {
    uint8_t b = Serial.read();
    serial_parser_process_byte(
      usbSerialParser,
      inputSerialCommands,
      sizeof(inputSerialCommands) / sizeof(inputSerialCommands[0]),
      b,
      micros()
    );
  }
}

#endif  // ENABLE_USB_CONTROL

// TX 'x' to Input: gap (154) and cal offsets (155). Input relays 154 on to the Screen.
// availableForWrite() on a hardware UART reports 0/1, not free bytes — waiting for more hangs.
void serialSendParam32(byte paramNumber, uint32_t paramValue) {
  uint8_t *b = (uint8_t *)&paramValue;
  byte bytesArray[7] = { (uint8_t)'x', paramNumber, b[0], b[1], b[2], b[3], 1 };
  while (Serial2.availableForWrite() < 1) {}
  Serial2.write(bytesArray, 7);
}
