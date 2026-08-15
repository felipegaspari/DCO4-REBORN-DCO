// Instantiate the DMA implementation from the shared library ONCE here
#define DCO_PROTOCOL_IMPLEMENT_DMA 
#include "_build_libs/DCO-PROTOCOL/serial_dma_tx.h"

UartDmaTx Serial2Dma = {0};

enum ParamIngress : uint8_t {
  PARAM_SRC_INPUT = 0,
  PARAM_SRC_USB   = 1,
};
static ParamIngress g_param_ingress = PARAM_SRC_INPUT;

static void serial_forward_input_block_to_mb(char cmd, const uint8_t* payload, uint8_t len) {
  if (g_param_ingress != PARAM_SRC_USB) return;
  serial_frame_write(Serial2Dma, (uint8_t)cmd, payload, len);
}

static void serial_send_adsr_block_to_mb(uint8_t cmd, uint16_t a, uint16_t d, uint16_t s, uint16_t r) {
  uint8_t payload[SERIAL_LEN_ADSR_BLOCK];
  encode_u16_le(payload + 0, a);
  encode_u16_le(payload + 2, d);
  encode_u16_le(payload + 4, s);
  encode_u16_le(payload + 6, r);
  serial_frame_write(Serial2Dma, cmd, payload, SERIAL_LEN_ADSR_BLOCK);
}

void serial_send_adsr_vca_block_to_mb() {
  serial_send_adsr_block_to_mb(CMD_ADSR1_BLOCK, ADSR_VCA_attack, ADSR_VCA_decay, ADSR_VCA_sustain, ADSR_VCA_release);
}

void serial_send_adsr_vcf_block_to_mb() {
  serial_send_adsr_block_to_mb(CMD_ADSR2_BLOCK, ADSR_VCF_attack, ADSR_VCF_decay, ADSR_VCF_sustain, ADSR_VCF_release);
}

void serial_send_adsr_dco_block_to_mb() {
  serial_send_adsr_block_to_mb(CMD_ADSR3_BLOCK, ADSR1_attack, ADSR1_decay, ADSR1_sustain, ADSR1_release);
}

void serial_send_filter_block_to_mb() {
  uint8_t payload[SERIAL_LEN_FILTER_BLOCK];
  encode_u16_le(payload + 0, CUTOFF);
  encode_u16_le(payload + 2, RESONANCE);
  encode_u16_le(payload + 4, (uint16_t)ADSR2toVCF);
  encode_u16_le(payload + 6, LFO2toVCF);
  serial_frame_write(Serial2Dma, CMD_FILTER_BLOCK, payload, SERIAL_LEN_FILTER_BLOCK);
}

void serial_send_screen_signal_to_mb(uint8_t signal) {
  serial_frame_write(Serial2Dma, CMD_SCREEN_SIGNAL, &signal, SERIAL_LEN_SCREEN_SIGNAL);
}

void serial_send_preset_loaded_to_mb(uint8_t slot) {
  uint8_t payload[SERIAL_LEN_PRESET_LOADED] = { slot };
  serial_frame_write(Serial2Dma, CMD_PRESET_LOADED, payload, SERIAL_LEN_PRESET_LOADED);
}

void serial_send_preset_scroll_to_mb(uint8_t slot) {
  uint8_t payload[SERIAL_LEN_SCREEN_PRESET_SCROLL];
  payload[0] = slot;
  for (uint8_t i = 0; i < 16; ++i) {
    payload[1 + i] = presetName[i];
  }
  serial_frame_write(Serial2Dma, (uint8_t)'q', payload, SERIAL_LEN_SCREEN_PRESET_SCROLL);
}

// --- Frame Handlers ---

static void input_handle_adsr1(char cmd, const uint8_t* payload, uint8_t len) {
  uint16_t dirty = 0, v;
  v = decode_u16_le(payload + 0); if (v != ADSR_VCA_attack)  { ADSR_VCA_attack  = v; dirty |= ADSR_DIRTY_VCA_A; }
  v = decode_u16_le(payload + 2); if (v != ADSR_VCA_decay)   { ADSR_VCA_decay   = v; dirty |= ADSR_DIRTY_VCA_D; }
  v = decode_u16_le(payload + 4); if (v != ADSR_VCA_sustain) { ADSR_VCA_sustain = v; dirty |= ADSR_DIRTY_VCA_S; }
  v = decode_u16_le(payload + 6); if (v != ADSR_VCA_release) { ADSR_VCA_release = v; dirty |= ADSR_DIRTY_VCA_R; }
  if (dirty) mark_adsr_params_dirty(dirty);
  serial_forward_input_block_to_mb(cmd, payload, len);
}

static void input_handle_adsr2(char cmd, const uint8_t* payload, uint8_t len) {
  uint16_t dirty = 0, v;
  v = decode_u16_le(payload + 0); if (v != ADSR_VCF_attack)  { ADSR_VCF_attack  = v; dirty |= ADSR_DIRTY_VCF_A; }
  v = decode_u16_le(payload + 2); if (v != ADSR_VCF_decay)   { ADSR_VCF_decay   = v; dirty |= ADSR_DIRTY_VCF_D; }
  v = decode_u16_le(payload + 4); if (v != ADSR_VCF_sustain) { ADSR_VCF_sustain = v; dirty |= ADSR_DIRTY_VCF_S; }
  v = decode_u16_le(payload + 6); if (v != ADSR_VCF_release) { ADSR_VCF_release = v; dirty |= ADSR_DIRTY_VCF_R; }
  if (dirty) mark_adsr_params_dirty(dirty);
  serial_forward_input_block_to_mb(cmd, payload, len);
}

static void input_handle_adsr3(char, const uint8_t* payload, uint8_t len) {
  uint16_t dirty = 0, v;
  v = decode_u16_le(payload + 0); if (v != ADSR1_attack)  { ADSR1_attack  = v; dirty |= ADSR_DIRTY_DCO_A; }
  v = decode_u16_le(payload + 2); if (v != ADSR1_decay)   { ADSR1_decay   = v; dirty |= ADSR_DIRTY_DCO_D; }
  v = decode_u16_le(payload + 4); if (v != ADSR1_sustain) { ADSR1_sustain = v; dirty |= ADSR_DIRTY_DCO_S; }
  v = decode_u16_le(payload + 6); if (v != ADSR1_release) { ADSR1_release = v; dirty |= ADSR_DIRTY_DCO_R; }
  if (dirty) mark_adsr_params_dirty(dirty);
  serial_forward_input_block_to_mb(CMD_ADSR3_BLOCK, payload, len);
}

static void input_handle_filter_block(char cmd, const uint8_t* payload, uint8_t len) {
  CUTOFF     = decode_u16_le(payload + 0);
  RESONANCE  = decode_u16_le(payload + 2);
  ADSR2toVCF = decode_i16_le(payload + 4);
  LFO2toVCF  = decode_u16_le(payload + 6);
  cv_bake_adsr2_to_vcf_scale();
  cv_bake_lfo2_to_vcf_scale();
  serial_forward_input_block_to_mb(cmd, payload, len);
}

// ----------------------------------------------------------------------------------
// Force Senders: Bypassing normal write wrapper to guarantee blocking when requested
// ----------------------------------------------------------------------------------
void serialSendParam16(byte paramNumber, int16_t paramValue, bool force) {
  uint8_t payload[SERIAL_LEN_PARAM_16];
  encode_param_p(payload, (uint8_t)paramNumber, paramValue);
  
  if (force) {
    uint8_t buf[SERIAL_STUFFED_MAX];
    int n = serial_frame_stuff(CMD_PARAM_16, payload, SERIAL_LEN_PARAM_16, buf, sizeof(buf));
    if (n > 0) {
      while (Serial2Dma.write(buf, (size_t)n) == 0) tight_loop_contents();
    }
  } else {
    serial_frame_write(Serial2Dma, CMD_PARAM_16, payload, SERIAL_LEN_PARAM_16);
  }
}

void serialSendParam32(byte paramNumber, uint32_t paramValue, bool force) {
  uint8_t payload[SERIAL_LEN_PARAM_32];
  encode_param32(payload, (uint8_t)paramNumber, paramValue);
  
  if (force) {
    uint8_t buf[SERIAL_STUFFED_MAX];
    int n = serial_frame_stuff(CMD_PARAM_32, payload, SERIAL_LEN_PARAM_32, buf, sizeof(buf));
    if (n > 0) {
      while (Serial2Dma.write(buf, (size_t)n) == 0) tight_loop_contents();
    }
  } else {
    serial_frame_write(Serial2Dma, CMD_PARAM_32, payload, SERIAL_LEN_PARAM_32);
  }
}

void serial_echo_persistable_param16(uint8_t id, int16_t value) {
  if (preset_param_is_persistable(id)) {
    serialSendParam16(id, value);
  }
}

static void input_handle_param16(char, const uint8_t* payload, uint8_t len) {
  ParamFrame frame;
  decode_param_p(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
  if (g_param_ingress != PARAM_SRC_INPUT) {
    serial_echo_persistable_param16(frame.id, (int16_t)frame.value);
  }
}

static void input_handle_preset_name(char, const uint8_t* payload, uint8_t len) {
  for (int i = 0; i < 16; ++i) presetName[i] = payload[i];
}

static void usb_handle_screen_signal(char, const uint8_t* payload, uint8_t len) {
  serial_send_screen_signal_to_mb(payload[0]);
}

static void input_handle_bulk_chunk(char, const uint8_t* payload, uint8_t len) {
  preset_bulk_chunk(payload, len);
}

static void input_handle_bulk_commit(char, const uint8_t* payload, uint8_t len) {
  preset_bulk_commit(payload, len);
}

static void input_handle_preset_dir_request(char, const uint8_t*, uint8_t) {
  preset_store_send_directory_to_mb();
}

static const SerialCommandDef inputSerialCommands[] = {
  { CMD_ADSR1_BLOCK,   SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr1        },
  { CMD_ADSR2_BLOCK,   SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr2        },
  { CMD_ADSR3_BLOCK,   SERIAL_LEN_ADSR_BLOCK,   input_handle_adsr3        },
  { CMD_FILTER_BLOCK,  SERIAL_LEN_FILTER_BLOCK, input_handle_filter_block },
  { CMD_PARAM_16,      SERIAL_LEN_PARAM_16,     input_handle_param16      },
  { CMD_PRESET_NAME,   SERIAL_LEN_PRESET_NAME,  input_handle_preset_name  },
  { CMD_SCREEN_SIGNAL, SERIAL_LEN_SCREEN_SIGNAL, usb_handle_screen_signal },
  { CMD_BULK_CHUNK,    SERIAL_LEN_BULK_CHUNK,   input_handle_bulk_chunk   },
  { CMD_BULK_COMMIT,   SERIAL_LEN_BULK_COMMIT,  input_handle_bulk_commit  },
};

static void mb_handle_param16(char, const uint8_t* payload, uint8_t len) {
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
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC1] = applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC1_q24);
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC2] = applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC2_q24);
    lfo1_pitch_mod_q24[LFO1_PITCH_OSC3] = applyDepthQ24(LFO1Level, LFO1toDCO_q24 + LFO1toOSC3_q24);
  }
  lfo2_pitch_mod_q24[LFO2_PITCH_OSC2] = applyDepthQ24(LFO2Level, LFO2toOSC2_q24 + LFO2toOSC2_coarse_q24);
  lfo2_pitch_mod_q24[LFO2_PITCH_OSC3] = applyDepthQ24(LFO2Level, LFO2toOSC3_q24 + LFO2toOSC3_coarse_q24);
  __dmb();
#endif
}

static const SerialCommandDef mainboardSerialCommands[] = {
  { CMD_PARAM_16,    SERIAL_LEN_PARAM_16,      mb_handle_param16     },
  { CMD_MOD_STREAM,  SERIAL_LEN_MOD_STREAM,  mb_handle_mod_stream  },
  { CMD_BENCH_TEXT,  SERIAL_LEN_BENCH_TEXT,  mb_handle_bench_text  },
  { CMD_ADSR1_BLOCK,  SERIAL_LEN_ADSR_BLOCK,    input_handle_adsr1        },
  { CMD_ADSR2_BLOCK,  SERIAL_LEN_ADSR_BLOCK,    input_handle_adsr2        },
  { CMD_ADSR3_BLOCK,  SERIAL_LEN_ADSR_BLOCK,    input_handle_adsr3        },
  { CMD_FILTER_BLOCK, SERIAL_LEN_FILTER_BLOCK,  input_handle_filter_block },
  { CMD_PRESET_NAME,  SERIAL_LEN_PRESET_NAME,   input_handle_preset_name  },
  { CMD_PRESET_DIR_REQUEST, SERIAL_LEN_PRESET_DIR_REQUEST, input_handle_preset_dir_request },
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
  
  // Use the new shared DMA initializer
  serial_dma_init_rp2040(0, uart1);

  serial_command_table_init(inputSerialLut, inputSerialCommands, sizeof(inputSerialCommands) / sizeof(inputSerialCommands[0]));
  serial_command_table_init(mainboardSerialLut, mainboardSerialCommands, sizeof(mainboardSerialCommands) / sizeof(mainboardSerialCommands[0]));
}

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
  serial_dma_poll_one(0);
  g_param_ingress = PARAM_SRC_INPUT;
  serial_parser_drain(mainboardSerialParser, mainboardSerialLut, Serial2, SERIAL_DRAIN_BYTE_BUDGET);
}

#ifdef ENABLE_USB_CONTROL

static SerialParserContext usbSerialParser = {};

void __not_in_flash_func(serial_usb_task)() {
  if (Serial.available() <= 0) return;
  g_param_ingress = PARAM_SRC_USB;
  while (Serial.available() > 0) {
    serial_parser_drain(
      usbSerialParser,
      inputSerialLut,
      Serial,
      255
    );
  }
}

#endif  // ENABLE_USB_CONTROL

void serial_send_note_on(uint8_t voice, uint8_t velocity, uint8_t note, uint8_t flags) {
  uint8_t payload[SERIAL_LEN_NOTE_ON] = { voice, velocity, note, flags };
  serial_frame_write(Serial2Dma, CMD_NOTE_ON, payload, SERIAL_LEN_NOTE_ON);
}

void serial_send_note_off(uint8_t voice) {
  serial_frame_write(Serial2Dma, CMD_NOTE_OFF, &voice, SERIAL_LEN_NOTE_OFF);
}

void serial_send_expression() {
  uint8_t payload[SERIAL_LEN_EXPRESSION];
  payload[0] = midi_aftertouch;
  payload[1] = midi_mod_wheel;
  encode_u16_le(payload + 2, (uint16_t)midi_pitch_bend);
  serial_frame_write(Serial2Dma, CMD_EXPRESSION, payload, SERIAL_LEN_EXPRESSION);
}

void serial_send_patch_osc_block_to_mb() {
  PatchOscBlock blk;
  blk.wave_enables        = 0;
  blk.osc1_interval       = octave_shift;
  blk.osc2_interval       = OSC2_interval;
  blk.osc3_interval       = OSC3_interval;
  blk.osc2_detune         = OSC2_detune;
  blk.unison_detune       = unisonDetune;
  blk.voice_mode          = voiceMode;
  blk.voice_alloc_mode    = 0;
  blk.sync_mode           = syncMode;
  blk.soft_sync           = softSyncChunks;
  blk.subosc_divide       = subOscDivide;
  blk.analog_drift        = analogDrift;
  blk.analog_drift_speed  = analogDriftSpeed;
  blk.analog_drift_spread = analogDriftSpread;
  blk.portamento_time     = portamento_time;
  blk.portamento_mode     = portamento_mode;
  blk.character           = 0;

  serial_frame_write(Serial2Dma, CMD_BLOCK_OSC, (const uint8_t*)&blk, sizeof(blk));
}

void serial_send_patch_lfo_block_to_mb() {
  PatchLfoBlock blk;
  blk.lfo1_waveform       = LFO1Waveform;
  blk.lfo2_waveform       = LFO2Waveform;
  blk.lfo1_speed          = LFO1SpeedVal;
  blk.lfo2_speed          = LFO2SpeedVal;
  blk.lfo1_to_dco         = LFO1toDCOVal;
  blk.lfo1_to_osc1        = 0;
  blk.lfo1_to_osc2        = 0;
  blk.lfo1_to_osc3        = 0;
  blk.lfo2_to_osc2        = 0;
  blk.lfo2_to_osc3        = 0;
  blk.lfo2_to_osc2_coarse = 0;
  blk.lfo2_to_osc3_coarse = 0;
  blk.lfo2_to_pw          = LFO2toPW;
  blk.lfo1_to_vca         = 0;
  blk.pw_value            = PW[0] << 2;
  blk.adsr1_to_vca        = 0;
  blk.adsr3_to_pwm        = ADSR1toPWM;
  blk.adsr3_to_detune1    = ADSR1toDETUNE1;
  blk.adsr3_pitch_mode    = env_dco_pitch_centered;
  blk.adsr3_to_osc_select = ADSR3ToOscSelect;

  serial_frame_write(Serial2Dma, CMD_BLOCK_LFO, (const uint8_t*)&blk, sizeof(blk));
}

void serial_send_patch_mod_block_to_mb() {
  PatchModBlock blk;
  memset(&blk, 0xFF, sizeof(blk)); // Initialize empty
  serial_frame_write(Serial2Dma, CMD_BLOCK_MOD, (const uint8_t*)&blk, sizeof(blk));
}