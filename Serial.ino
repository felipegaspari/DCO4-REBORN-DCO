#define DCO_PROTOCOL_IMPLEMENT_DMA 
#include "_build_libs/DCO-PROTOCOL/serial_dma_tx.h"
#include "include_all.h"

UartDmaTx Serial2Dma = {0};

// Ingress tag to prevent bouncing Mainboard frames back onto Serial2
enum ParamIngress : uint8_t {
  PARAM_SRC_MAINBOARD = 0, // Arrived from hardware UART (Serial2)
  PARAM_SRC_USB       = 1, // Arrived from Host PC CDC (Serial)
};
static ParamIngress g_param_ingress = PARAM_SRC_MAINBOARD;

// Forward to Mainboard ONLY if the command originated from USB Host
static void __not_in_flash_func(serial_forward_usb_edit_to_mb)(char cmd, const uint8_t* payload, uint8_t len) {
  if (g_param_ingress != PARAM_SRC_USB) return;
  serial_frame_write(Serial2Dma, (uint8_t)cmd, payload, len);
}

// =============================================================================
// Outgoing Block Senders to Mainboard
// =============================================================================

static void __not_in_flash_func(serial_send_adsr_block_to_mb)(uint8_t cmd, uint16_t a, uint16_t d, uint16_t s, uint16_t r) {
  uint8_t payload[SERIAL_LEN_ADSR_BLOCK];
  encode_u16_le(payload + 0, a);
  encode_u16_le(payload + 2, d);
  encode_u16_le(payload + 4, s);
  encode_u16_le(payload + 6, r);
  serial_frame_write(Serial2Dma, cmd, payload, SERIAL_LEN_ADSR_BLOCK);
}

void __not_in_flash_func(serial_send_adsr_vca_block_to_mb)() {
  serial_send_adsr_block_to_mb(CMD_ADSR1_BLOCK, ADSR_VCA_attack, ADSR_VCA_decay, ADSR_VCA_sustain, ADSR_VCA_release);
}

void __not_in_flash_func(serial_send_adsr_vcf_block_to_mb)() {
  serial_send_adsr_block_to_mb(CMD_ADSR2_BLOCK, ADSR_VCF_attack, ADSR_VCF_decay, ADSR_VCF_sustain, ADSR_VCF_release);
}

void __not_in_flash_func(serial_send_adsr_dco_block_to_mb)() {
  serial_send_adsr_block_to_mb(CMD_ADSR3_BLOCK, ADSR3_attack, ADSR3_decay, ADSR3_sustain, ADSR3_release);
}

void __not_in_flash_func(serial_send_filter_block_to_mb)() {
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
  
  // Check if there is a valid preset in RAM for this slot
  if (presetStoreRAM[slot][PRESET_OFF_MAGIC] == PRESET_MAGIC) {
    for (uint8_t i = 0; i < 16; ++i) {
      payload[1 + i] = presetStoreRAM[slot][PRESET_OFF_NAME + i];
    }
  } else {
    // Empty preset slot - send 16 blank characters
    memset(payload + 1, 0, 16);
  }
  
  serial_frame_write(Serial2Dma, (uint8_t)CMD_PRESET_NAME, payload, SERIAL_LEN_SCREEN_PRESET_SCROLL);
}

// =============================================================================
// Domain Block Senders (Preset Recall Burst)
// =============================================================================

void __not_in_flash_func(serial_send_patch_osc_block_to_mb)() {
  PatchOscBlock blk;
  memset(&blk, 0, sizeof(blk));

  uint16_t waves = 0;
  if (presetParamShadow[PARAM_OSC1_SAW_ENABLE])   waves |= (1u << 0);
  if (presetParamShadow[PARAM_OSC1_PULSE_ENABLE]) waves |= (1u << 1);
  if (presetParamShadow[PARAM_OSC1_TRI_ENABLE])   waves |= (1u << 2);
  if (presetParamShadow[PARAM_OSC2_SAW_ENABLE])   waves |= (1u << 3);
  if (presetParamShadow[PARAM_OSC2_PULSE_ENABLE]) waves |= (1u << 4);
  if (presetParamShadow[PARAM_OSC2_TRI_ENABLE])   waves |= (1u << 5);
  if (presetParamShadow[PARAM_OSC3_SAW_ENABLE])   waves |= (1u << 6);
  if (presetParamShadow[PARAM_OSC3_PULSE_ENABLE]) waves |= (1u << 7);
  if (presetParamShadow[PARAM_OSC3_TRI_ENABLE])   waves |= (1u << 8);
  blk.wave_enables = waves;

  blk.osc1_interval       = (int8_t)presetParamShadow[PARAM_OSC1_INTERVAL];
  blk.osc2_interval       = (int8_t)presetParamShadow[PARAM_OSC2_INTERVAL];
  blk.osc3_interval       = (int8_t)presetParamShadow[PARAM_OSC3_INTERVAL];
  blk.osc2_detune         = (uint16_t)presetParamShadow[PARAM_OSC2_DETUNE_VAL];
  blk.unison_detune       = (int16_t)presetParamShadow[PARAM_UNISON_DETUNE];
  blk.voice_mode          = (uint8_t)presetParamShadow[PARAM_VOICE_MODE];
  blk.voice_alloc_mode    = (uint8_t)presetParamShadow[PARAM_VOICE_ALLOC_MODE];
  blk.sync_mode           = (uint8_t)presetParamShadow[PARAM_SYNC_MODE];
  blk.soft_sync           = (uint8_t)presetParamShadow[PARAM_SOFT_SYNC];
  blk.subosc_divide       = (uint8_t)presetParamShadow[PARAM_SUBOSC_DIVIDE];
  blk.analog_drift        = (int8_t)presetParamShadow[PARAM_ANALOG_DRIFT_AMOUNT];
  blk.analog_drift_speed  = (int16_t)presetParamShadow[PARAM_ANALOG_DRIFT_SPEED];
  blk.analog_drift_spread = (int8_t)presetParamShadow[PARAM_ANALOG_DRIFT_SPREAD];
  blk.portamento_time     = (uint16_t)presetParamShadow[PARAM_PORTAMENTO_TIME];
  blk.portamento_mode     = (uint8_t)presetParamShadow[PARAM_PORTAMENTO_MODE];
  blk.character           = (uint8_t)presetParamShadow[PARAM_CHARACTER];

  serial_frame_write(Serial2Dma, CMD_BLOCK_OSC, (const uint8_t*)&blk, SERIAL_LEN_BLOCK_OSC);
}

void __not_in_flash_func(serial_send_patch_lfo_block_to_mb)() {
  PatchLfoBlock blk;
  memset(&blk, 0, sizeof(blk));

  blk.lfo1_waveform       = (uint8_t)presetParamShadow[PARAM_LFO1_WAVEFORM];
  blk.lfo2_waveform       = (uint8_t)presetParamShadow[PARAM_LFO2_WAVEFORM];
  blk.lfo1_speed          = (uint16_t)presetParamShadow[PARAM_LFO1_SPEED];
  blk.lfo2_speed          = (uint16_t)presetParamShadow[PARAM_LFO2_SPEED];
  blk.lfo1_to_dco         = (uint16_t)presetParamShadow[PARAM_LFO1_TO_DCO];
  blk.lfo1_to_osc1        = (uint8_t)presetParamShadow[PARAM_LFO1_TO_OSC1];
  blk.lfo1_to_osc2        = (uint8_t)presetParamShadow[PARAM_LFO1_TO_OSC2];
  blk.lfo1_to_osc3        = (uint8_t)presetParamShadow[PARAM_LFO1_TO_OSC3];
  blk.lfo2_to_osc2        = (uint16_t)presetParamShadow[PARAM_LFO2_TO_OSC2];
  blk.lfo2_to_osc3        = (uint16_t)presetParamShadow[PARAM_LFO2_TO_OSC3];
  blk.lfo2_to_osc2_coarse = (uint16_t)presetParamShadow[PARAM_LFO2_TO_OSC2_COARSE];
  blk.lfo2_to_osc3_coarse = (uint16_t)presetParamShadow[PARAM_LFO2_TO_OSC3_COARSE];
  blk.lfo2_to_pw          = (uint16_t)presetParamShadow[PARAM_LFO2_TO_PW];
  blk.lfo1_to_vca         = (uint16_t)presetParamShadow[PARAM_LFO1_TO_VCA];
  blk.pw_value            = (uint16_t)presetParamShadow[PARAM_PW_VALUE];
  blk.adsr1_to_vca        = (int16_t)presetParamShadow[PARAM_ADSR1_TO_VCA];
  blk.adsr3_to_pwm        = (int16_t)presetParamShadow[PARAM_ADSR3_TO_PWM];
  blk.adsr3_to_detune1    = (int16_t)presetParamShadow[PARAM_ADSR3_TO_DETUNE1];
  blk.adsr3_pitch_mode    = (uint8_t)presetParamShadow[PARAM_ADSR3_PITCH_MODE];
  blk.adsr3_to_osc_select = (int8_t)presetParamShadow[PARAM_ADSR3_TO_OSC_SELECT];

  serial_frame_write(Serial2Dma, CMD_BLOCK_LFO, (const uint8_t*)&blk, SERIAL_LEN_BLOCK_LFO);
}

void __not_in_flash_func(serial_send_patch_mix_block_to_mb)() {
  PatchMixBlock blk;
  memset(&blk, 0, sizeof(blk));

  blk.osc1_level         = (uint8_t)presetParamShadow[PARAM_OSC1_LEVEL];
  blk.osc2_level         = (uint8_t)presetParamShadow[PARAM_OSC2_LEVEL];
  blk.osc3_level         = (uint8_t)presetParamShadow[PARAM_OSC3_LEVEL];
  blk.sub_level          = (uint8_t)presetParamShadow[PARAM_SUB_LEVEL];
  blk.vca_level          = (uint8_t)presetParamShadow[PARAM_VCA_LEVEL];
  blk.filter_mode        = (uint8_t)presetParamShadow[PARAM_FILTER_MODE];
  blk.velocity_to_vcf    = (int8_t)presetParamShadow[PARAM_VELOCITY_TO_VCF];
  blk.velocity_to_vca    = (int8_t)presetParamShadow[PARAM_VELOCITY_TO_VCA];
  blk.vcf_keytrack       = (int16_t)presetParamShadow[PARAM_VCF_KEYTRACK];
  blk.adsr1_to_vca       = (int16_t)presetParamShadow[PARAM_ADSR1_TO_VCA];
  blk.dist_drive         = (uint16_t)presetParamShadow[PARAM_DIST_DRIVE];
  blk.dist_mix           = (uint16_t)presetParamShadow[PARAM_DIST_MIX];
  blk.adsr1_attack_curve = (uint8_t)presetParamShadow[PARAM_ADSR1_ATTACK_CURVE];
  blk.adsr1_decay_curve  = (uint8_t)presetParamShadow[PARAM_ADSR1_DECAY_CURVE];
  blk.adsr2_attack_curve = (uint8_t)presetParamShadow[PARAM_ADSR2_ATTACK_CURVE];
  blk.adsr2_decay_curve  = (uint8_t)presetParamShadow[PARAM_ADSR2_DECAY_CURVE];

  // Pack boolean switches into the flag byte
  uint8_t flags = 0;
  if (presetParamShadow[PARAM_RESONANCE_COMPENSATION]) flags |= (1 << 0);
  if (presetParamShadow[PARAM_VCA_ADSR_RESTART])       flags |= (1 << 1);
  if (presetParamShadow[PARAM_VCF_ADSR_RESTART])       flags |= (1 << 2);
  blk.misc_flags = flags;

  serial_frame_write(Serial2Dma, CMD_BLOCK_MIX, (const uint8_t*)&blk, SERIAL_LEN_BLOCK_MIX);
}

void __not_in_flash_func(serial_send_patch_mod_block_to_mb)() {
  PatchModBlock blk;
  memset(&blk, 0, sizeof(blk));

  for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
    uint8_t srcId   = (uint8_t)(PARAM_MOD_SLOT0_SOURCE + i * 3);
    uint8_t destId  = (uint8_t)(PARAM_MOD_SLOT0_DEST   + i * 3);
    uint8_t depthId = (uint8_t)(PARAM_MOD_SLOT0_DEPTH  + i * 3);

    blk.slots[i].src   = (uint8_t)presetParamShadow[srcId];
    blk.slots[i].dest  = (uint8_t)presetParamShadow[destId];
    blk.slots[i].depth = (int16_t)presetParamShadow[depthId];
  }

  serial_frame_write(Serial2Dma, CMD_BLOCK_MOD, (const uint8_t*)&blk, SERIAL_LEN_BLOCK_MOD);
}

// =============================================================================
// Parameter Senders
// =============================================================================

void __not_in_flash_func(serialSendParam16)(byte paramNumber, int16_t paramValue, bool force) {
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

void __not_in_flash_func(serialSendParam32)(byte paramNumber, uint32_t paramValue, bool force) {
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

// =============================================================================
// Inbound Frame Handlers (From Mainboard UART or USB CDC)
// =============================================================================

static void __not_in_flash_func(dco_rx_handle_adsr1)(char cmd, const uint8_t* payload, uint8_t len) {
  uint16_t dirty = 0, v;
  v = decode_u16_le(payload + 0); if (v != ADSR_VCA_attack)  { ADSR_VCA_attack  = v; dirty |= ADSR_DIRTY_VCA_A; }
  v = decode_u16_le(payload + 2); if (v != ADSR_VCA_decay)   { ADSR_VCA_decay   = v; dirty |= ADSR_DIRTY_VCA_D; }
  v = decode_u16_le(payload + 4); if (v != ADSR_VCA_sustain) { ADSR_VCA_sustain = v; dirty |= ADSR_DIRTY_VCA_S; }
  v = decode_u16_le(payload + 6); if (v != ADSR_VCA_release) { ADSR_VCA_release = v; dirty |= ADSR_DIRTY_VCA_R; }
  if (dirty) mark_adsr_params_dirty(dirty);
  serial_forward_usb_edit_to_mb(cmd, payload, len);
}

static void __not_in_flash_func(dco_rx_handle_adsr2)(char cmd, const uint8_t* payload, uint8_t len) {
  uint16_t dirty = 0, v;
  v = decode_u16_le(payload + 0); if (v != ADSR_VCF_attack)  { ADSR_VCF_attack  = v; dirty |= ADSR_DIRTY_VCF_A; }
  v = decode_u16_le(payload + 2); if (v != ADSR_VCF_decay)   { ADSR_VCF_decay   = v; dirty |= ADSR_DIRTY_VCF_D; }
  v = decode_u16_le(payload + 4); if (v != ADSR_VCF_sustain) { ADSR_VCF_sustain = v; dirty |= ADSR_DIRTY_VCF_S; }
  v = decode_u16_le(payload + 6); if (v != ADSR_VCF_release) { ADSR_VCF_release = v; dirty |= ADSR_DIRTY_VCF_R; }
  if (dirty) mark_adsr_params_dirty(dirty);
  serial_forward_usb_edit_to_mb(cmd, payload, len);
}

static void __not_in_flash_func(dco_rx_handle_adsr3)(char cmd, const uint8_t* payload, uint8_t len) {
  uint16_t dirty = 0, v;
  v = decode_u16_le(payload + 0); if (v != ADSR3_attack)  { ADSR3_attack  = v; dirty |= ADSR_DIRTY_DCO_A; }
  v = decode_u16_le(payload + 2); if (v != ADSR3_decay)   { ADSR3_decay   = v; dirty |= ADSR_DIRTY_DCO_D; }
  v = decode_u16_le(payload + 4); if (v != ADSR3_sustain) { ADSR3_sustain = v; dirty |= ADSR_DIRTY_DCO_S; }
  v = decode_u16_le(payload + 6); if (v != ADSR3_release) { ADSR3_release = v; dirty |= ADSR_DIRTY_DCO_R; }
  if (dirty) mark_adsr_params_dirty(dirty);
  serial_forward_usb_edit_to_mb(cmd, payload, len);
}

static void __not_in_flash_func(dco_rx_handle_filter_block)(char cmd, const uint8_t* payload, uint8_t len) {
  CUTOFF     = decode_u16_le(payload + 0);
  RESONANCE  = decode_u16_le(payload + 2);
  ADSR2toVCF = decode_i16_le(payload + 4);
  LFO2toVCF  = decode_u16_le(payload + 6);
  cv_bake_adsr2_to_vcf_scale();
  cv_bake_lfo2_to_vcf_scale();
  serial_forward_usb_edit_to_mb(cmd, payload, len);
}

static void __not_in_flash_func(dco_rx_handle_param16)(char, const uint8_t* payload, uint8_t) {
  ParamFrame frame;
  decode_param_p(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
  if (g_param_ingress == PARAM_SRC_USB) {
    serial_echo_persistable_param16(frame.id, (int16_t)frame.value);
  }
}

static void __not_in_flash_func(dco_rx_handle_preset_name)(char, const uint8_t* payload, uint8_t) {
  for (int i = 0; i < 16; ++i) presetName[i] = payload[i];
}

static void __not_in_flash_func(dco_rx_handle_screen_signal)(char, const uint8_t* payload, uint8_t) {
  if (payload[0] == SCREEN_SIGNAL_SILENT) {
    serialSendParam16(ParamId::PARAM_ALL_CONTROLS_MANUAL, 0, true);
  }
  serial_send_screen_signal_to_mb(payload[0]);
}

static void __not_in_flash_func(dco_rx_handle_bulk_chunk)(char, const uint8_t* payload, uint8_t len) {
  preset_bulk_chunk(payload, len);
}

static void __not_in_flash_func(dco_rx_handle_bulk_commit)(char, const uint8_t* payload, uint8_t len) {
  preset_bulk_commit(payload, len);
}

static void dco_rx_handle_preset_dir_request(char, const uint8_t*, uint8_t) {
  preset_store_send_directory_to_mb();
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

static void __not_in_flash_func(mb_handle_mod_stream)(char, const uint8_t* payload, uint8_t) {
  LFO1Level = (int16_t)decode_u16_le(payload + 0);
  LFO2Level = (int16_t)decode_u16_le(payload + 2);
  for (uint8_t i = 0; i < 4 && i < NUM_VOICES_TOTAL; i++) {
    ADSR3Level_q15[i] = (int16_t)decode_u16_le(payload + 4 + i * 2);
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

// =============================================================================
// Tables & Init
// =============================================================================

// USB CDC Command Table (Host PC / dco_control)
static const SerialCommandDef usbSerialCommands[] = {
  { CMD_ADSR1_BLOCK,   SERIAL_LEN_ADSR_BLOCK,   dco_rx_handle_adsr1        },
  { CMD_ADSR2_BLOCK,   SERIAL_LEN_ADSR_BLOCK,   dco_rx_handle_adsr2        },
  { CMD_ADSR3_BLOCK,   SERIAL_LEN_ADSR_BLOCK,   dco_rx_handle_adsr3        },
  { CMD_FILTER_BLOCK,  SERIAL_LEN_FILTER_BLOCK, dco_rx_handle_filter_block },
  { CMD_PARAM_16,      SERIAL_LEN_PARAM_16,     dco_rx_handle_param16      },
  { CMD_PRESET_NAME,   SERIAL_LEN_PRESET_NAME,  dco_rx_handle_preset_name  },
  { CMD_SCREEN_SIGNAL, SERIAL_LEN_SCREEN_SIGNAL,dco_rx_handle_screen_signal},
  { CMD_BULK_CHUNK,    SERIAL_LEN_BULK_CHUNK,   dco_rx_handle_bulk_chunk   },
  { CMD_BULK_COMMIT,   SERIAL_LEN_BULK_COMMIT,  dco_rx_handle_bulk_commit  },
};

// Mainboard UART Command Table (Serial2)
static const SerialCommandDef mainboardSerialCommands[] = {
  { CMD_PARAM_16,           SERIAL_LEN_PARAM_16,           dco_rx_handle_param16           },
  { CMD_MOD_STREAM,         SERIAL_LEN_MOD_STREAM,         mb_handle_mod_stream            },
  { CMD_BENCH_TEXT,         SERIAL_LEN_BENCH_TEXT,         mb_handle_bench_text            },
  { CMD_ADSR1_BLOCK,        SERIAL_LEN_ADSR_BLOCK,         dco_rx_handle_adsr1             },
  { CMD_ADSR2_BLOCK,        SERIAL_LEN_ADSR_BLOCK,         dco_rx_handle_adsr2             },
  { CMD_ADSR3_BLOCK,        SERIAL_LEN_ADSR_BLOCK,         dco_rx_handle_adsr3             },
  { CMD_FILTER_BLOCK,       SERIAL_LEN_FILTER_BLOCK,       dco_rx_handle_filter_block      },
  { CMD_PRESET_NAME,        SERIAL_LEN_PRESET_NAME,        dco_rx_handle_preset_name       },
  { CMD_PRESET_DIR_REQUEST, SERIAL_LEN_PRESET_DIR_REQUEST, dco_rx_handle_preset_dir_request },
};

static SerialCommandTable usbSerialLut;
static SerialParserContext usbSerialParser = {};
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
  
  serial_dma_init_rp2040(0, uart1);

  serial_command_table_init(usbSerialLut, usbSerialCommands, sizeof(usbSerialCommands) / sizeof(usbSerialCommands[0]));
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
  g_param_ingress = PARAM_SRC_MAINBOARD; // Frames from Serial2 will NOT echo back to Serial2
  serial_parser_drain(mainboardSerialParser, mainboardSerialLut, Serial2, SERIAL_DRAIN_BYTE_BUDGET);
}

#ifdef ENABLE_USB_CONTROL
void __not_in_flash_func(serial_usb_task)() {
  if (Serial.available() <= 0) return;
  g_param_ingress = PARAM_SRC_USB; // Frames from USB WILL mirror down to Mainboard
  serial_parser_drain(usbSerialParser, usbSerialLut, Serial, SERIAL_DRAIN_BYTE_BUDGET);
}
#endif

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