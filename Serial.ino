// Configure Serial1 (MIDI DIN), Serial2 (Mainboard or Input @ 2.5M), optional Screen PIO UART.
#ifdef ENABLE_SCREEN_UART
#include <SerialPIO.h>
// Interim: Screen on SerialPIO so MIDI keeps HW UART0 @ GP0/1 (see docs/PINOUT.md).
// Claims a free PIO SM (not OSC freq SM0 on pio0/1/2). Move to HW UART1 when MIDI is PIO.
SerialPIO SerialScreen(8, 9, 512);
#endif

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

#ifdef ENABLE_SCREEN_UART
  SerialScreen.begin(2500000);
#endif

  Serial.begin(2000000);
}

/// -------------------------------
// Serial2: legacy Mainboard protocol (default)
// -------------------------------

#ifndef ENABLE_INPUT_UART

static void dco_handle_pw_update(char, const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_PAYLOAD_LEN_PW_UPDATE) {
    return;
  }
  uint16_t pwRaw = (uint16_t)payload[0] | (uint16_t(payload[1]) << 8);
  PW[0] = pwRaw / 4;
}

static void dco_handle_adsr_block(char, const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_PAYLOAD_LEN_ADSR_BLOCK) {
    return;
  }
  ADSR1_attack  = (uint16_t(payload[0]) << 8) | uint16_t(payload[1]);
  ADSR1_decay   = (uint16_t(payload[2]) << 8) | uint16_t(payload[3]);
  ADSR1_sustain = (uint16_t(payload[4]) << 8) | uint16_t(payload[5]);
  ADSR1_release = (uint16_t(payload[6]) << 8) | uint16_t(payload[7]);
}

static void dco_handle_param16(char, const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_PAYLOAD_LEN_PARAM_16) {
    return;
  }
  ParamFrame frame;
  decode_param_p(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static void dco_handle_param8(char, const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_PAYLOAD_LEN_PARAM_8) {
    return;
  }
  ParamFrame frame;
  decode_param_w(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static void dco_handle_param32(char, const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_PAYLOAD_LEN_PARAM_32) {
    return;
  }
  ParamFrame frame;
  decode_param_x(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static const SerialCommandDef dcoSerial2Commands[] = {
  { SERIAL_CMD_PW_UPDATE,  SERIAL_PAYLOAD_LEN_PW_UPDATE,  dco_handle_pw_update  },
  { SERIAL_CMD_ADSR_BLOCK, SERIAL_PAYLOAD_LEN_ADSR_BLOCK, dco_handle_adsr_block },
  { SERIAL_CMD_PARAM_16,   SERIAL_PAYLOAD_LEN_PARAM_16,   dco_handle_param16    },
  { SERIAL_CMD_PARAM_8,    SERIAL_PAYLOAD_LEN_PARAM_8,    dco_handle_param8     },
  { SERIAL_CMD_PARAM_32,   SERIAL_PAYLOAD_LEN_PARAM_32,   dco_handle_param32    },
};

static SerialParserContext dcoSerial2Parser = {
  SERIAL_WAIT_FOR_CMD, 0, nullptr, {0}, 0, 0, 0
};

void serial_panel_task() {
  if (dcoSerial2Parser.state == SERIAL_READ_PAYLOAD) {
    uint32_t now = micros();
    serial_parser_check_timeout(dcoSerial2Parser, now);
  }
  if (Serial2.available() > 0) {
    uint32_t now = micros();
    while (Serial2.available() > 0) {
      uint8_t b = Serial2.read();
      serial_parser_process_byte(
        dcoSerial2Parser,
        dcoSerial2Commands,
        sizeof(dcoSerial2Commands) / sizeof(dcoSerial2Commands[0]),
        b,
        now
      );
    }
  }
}

// Legacy Mainboard envelope peer (EnvVCA/EnvVCF lived on STM32).
inline void serial_send_note_on(uint8_t voice_n, uint8_t note_velo, uint8_t note) {
  byte sendArray[4];
  sendArray[0] = (uint8_t)'n';
  sendArray[1] = voice_n;
  sendArray[2] = note_velo;
  sendArray[3] = note;
  while (Serial2.availableForWrite() < 1) {}
  Serial2.write(sendArray, 4);
}

inline void serial_send_note_off(uint8_t voice_n) {
  byte sendArray[2] = { (uint8_t)'o', voice_n };
  while (Serial2.availableForWrite() < 1) {}
  Serial2.write(sendArray, 2);
}

#else  // ENABLE_INPUT_UART — Serial2 is Input hub

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

// No Mainboard envelope peer — note edges are local (EnvVCA/EnvVCF on Core1).
inline void serial_send_note_on(uint8_t, uint8_t, uint8_t) {}
inline void serial_send_note_off(uint8_t) {}

#endif  // ENABLE_INPUT_UART

static inline void serial_write_param32_frame(Stream& port, byte paramNumber, uint32_t paramValue) {
  uint8_t *b = (uint8_t *)&paramValue;
  byte bytesArray[7] = { (uint8_t)'x', paramNumber, b[0], b[1], b[2], b[3], 1 };
  while (port.availableForWrite() < 7) {}
  port.write(bytesArray, 7);
}

#ifdef ENABLE_SCREEN_UART
void serialSendParam32ToScreen(byte paramNumber, uint32_t paramValue) {
  serial_write_param32_frame(SerialScreen, paramNumber, paramValue);
}
#endif

// Route 'x' frames: gap → Screen (if enabled); else Input hub or legacy Mainboard.
void serialSendParam32(byte paramNumber, uint32_t paramValue) {
#ifdef ENABLE_SCREEN_UART
  if (paramNumber == (byte)PARAM_GAP_FROM_DCO) {
    serialSendParam32ToScreen(paramNumber, paramValue);
    return;
  }
#endif

#if defined(ENABLE_INPUT_UART)
  // Hub: cal offsets (155) and other 'x' go to Input (replaces Mainboard Serial8 forward).
  serial_write_param32_frame(Serial2, paramNumber, paramValue);
#elif defined(ENABLE_SCREEN_UART)
  // Screen bring-up without Input/Mainboard: non-gap 'x' has no peer — drop.
  (void)paramNumber;
  (void)paramValue;
#else
  // Legacy: Serial2 → Mainboard (forwards gap to Screen / offsets to Input).
  serial_write_param32_frame(Serial2, paramNumber, paramValue);
#endif
}
