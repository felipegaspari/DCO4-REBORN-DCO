#include "include_all.h"

static const char PRESET_LAST_FILE[] = "pstLast";

int16_t presetParamShadow[PRESET_PARAM_COUNT];
uint8_t presetParamSetBitmap[PRESET_PARAM_COUNT / 8];
bool presetBootPending = true;

// --- FULL 153 KB IN-RAM PRESET STORE ---
uint8_t presetStoreRAM[PRESET_NUM_SLOTS][PRESET_RECORD_SIZE];
static uint8_t presetBulkStaging[PRESET_BULK_STAGING_SIZE];

static void preset_chunk_filename(uint8_t chunkIndex, char* out, size_t cap) {
  snprintf(out, cap, "pb%02u", (unsigned)chunkIndex);
}

static inline uint8_t preset_chunk_index(uint8_t slot) {
  return (uint8_t)(slot >> 2);
}

static inline uint16_t preset_slot_offset(uint8_t slot) {
  return (uint16_t)(slot & 3u) * PRESET_RECORD_SIZE;
}

static const char* preset_bulk_target_name(uint8_t target) {
  switch (target) {
    case PRESET_BULK_PRESET:        return "preset";
    case PRESET_BULK_VOICE_TABLES:  return "voiceTables";
    case PRESET_BULK_PW_CENTER:     return "PWCenter";
    case PRESET_BULK_PW_HIGH_LIMIT: return "PWHighLimit";
    case PRESET_BULK_PW_LOW_LIMIT:  return "PWLowLimit";
    case PRESET_BULK_MANUAL_OFFSET: return "ManualOffset";
    case PRESET_BULK_AMP_COMP_440:  return "AmpComp440";
    case PRESET_BULK_AMP_COMP_DUTY: return "AmpCompDutyOffset";
    default:                        return "unknown";
  }
}

// --- One-Time Boot Loader (Loads all 153 KB into RAM) ---
void preset_store_init_ram() {
  memset(presetStoreRAM, 0, sizeof(presetStoreRAM));
  char fname[8];

  for (uint8_t chunk = 0; chunk < PRESET_CHUNK_COUNT; ++chunk) {
    preset_chunk_filename(chunk, fname, sizeof(fname));
    if (!LittleFS.exists(fname)) continue;
    File f = LittleFS.open(fname, "r");
    if (!f || f.size() != PRESET_CHUNK_SIZE) {
      if (f) f.close();
      continue;
    }
    
    // Read the whole 2392-byte chunk directly into the RAM array
    const uint8_t startSlot = chunk * PRESET_RECORDS_PER_FILE;
    f.read(&presetStoreRAM[startSlot][0], PRESET_CHUNK_SIZE);
    f.close();
  }
}

// --- Dump text helpers ---

static void dump_print_begin(const char* target, int slot, uint32_t size) {
  if (slot >= 0) {
    Serial.printf("[dump] begin target=%s slot=%d size=%lu\n", target, slot, (unsigned long)size);
  } else {
    Serial.printf("[dump] begin target=%s size=%lu\n", target, (unsigned long)size);
  }
}

static void dump_print_data_line(uint16_t offset, const uint8_t* data, uint16_t len) {
  char line[96];
  int n = snprintf(line, sizeof(line), "[dump] d %04X ", (unsigned)offset);
  for (uint16_t i = 0; i < len && n + 2 < (int)sizeof(line); ++i) {
    n += snprintf(line + n, sizeof(line) - n, "%02X", data[i]);
  }
  Serial.println(line);
}

static void dump_print_end(const char* target, uint32_t crc) {
  Serial.printf("[dump] end target=%s crc=%08lX\n", target, (unsigned long)crc);
}

static void dump_print_err(const char* target, const char* reason) {
  Serial.printf("[dump] err target=%s reason=%s\n", target, reason);
}

static void dump_buffer(const char* target, int slot, const uint8_t* data, uint16_t size) {
  dump_print_begin(target, slot, size);
  for (uint16_t off = 0; off < size; off += PRESET_BULK_CHUNK_DATA) {
    uint16_t n = size - off;
    if (n > PRESET_BULK_CHUNK_DATA) n = PRESET_BULK_CHUNK_DATA;
    dump_print_data_line(off, data + off, n);
  }
  dump_print_end(target, preset_crc32(data, size));
}

static void dump_fs_file(const char* target, const char* filename, uint32_t expectedSize) {
  if (!LittleFS.exists(filename)) {
    dump_print_err(target, "missing");
    return;
  }
  File f = LittleFS.open(filename, "r");
  if (!f) {
    dump_print_err(target, "open");
    return;
  }
  const uint32_t fileSize = f.size();
  if (fileSize < expectedSize) {
    f.close();
    dump_print_err(target, "short");
    return;
  }
  const uint32_t size = expectedSize;
  dump_print_begin(target, -1, size);
  uint8_t chunk[PRESET_BULK_CHUNK_DATA];
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t off = 0;
  while (off < size) {
    uint16_t n = (uint16_t)((size - off > PRESET_BULK_CHUNK_DATA) ? PRESET_BULK_CHUNK_DATA : (size - off));
    f.read(chunk, n);
    crc = preset_crc32_update(crc, chunk, n);
    dump_print_data_line((uint16_t)off, chunk, n);
    off += n;
  }
  f.close();
  dump_print_end(target, crc ^ 0xFFFFFFFFu);
}

// --- Record build / validate / apply ---

static void preset_record_build(uint8_t* buf) {
  memset(buf, 0, PRESET_RECORD_SIZE);
  buf[PRESET_OFF_MAGIC]   = PRESET_MAGIC;
  buf[PRESET_OFF_VERSION] = PRESET_VERSION;

  for (int i = 0; i < 16; ++i) {
    buf[PRESET_OFF_NAME + i] = presetName[i];
  }

  memcpy(buf + PRESET_OFF_BITMAP, presetParamSetBitmap, sizeof(presetParamSetBitmap));
  for (uint16_t id = 0; id < PRESET_PARAM_COUNT; ++id) {
    encode_u16_le(buf + PRESET_OFF_PARAMS + id * 2, (uint16_t)presetParamShadow[id]);
  }

  const uint16_t blocks[PRESET_BLOCK_FIELDS] = {
    ADSR_VCA_attack, ADSR_VCA_decay, ADSR_VCA_sustain, ADSR_VCA_release,
    ADSR_VCF_attack, ADSR_VCF_decay, ADSR_VCF_sustain, ADSR_VCF_release,
    ADSR1_attack,    ADSR1_decay,    ADSR1_sustain,    ADSR1_release,
    CUTOFF,          RESONANCE,      (uint16_t)ADSR2toVCF, LFO2toVCF,
  };
  for (uint8_t i = 0; i < PRESET_BLOCK_FIELDS; ++i) {
    encode_u16_le(buf + PRESET_OFF_BLOCKS + i * 2, blocks[i]);
  }

  encode_u32_le(buf + PRESET_OFF_CRC, preset_crc32(buf, PRESET_OFF_CRC));
}

static bool preset_record_validate(const uint8_t* buf) {
  if (buf[PRESET_OFF_MAGIC] != PRESET_MAGIC) return false;
  if (buf[PRESET_OFF_VERSION] != PRESET_VERSION) return false;
  return decode_u32_le(buf + PRESET_OFF_CRC) == preset_crc32(buf, PRESET_OFF_CRC);
}

static void preset_record_apply(const uint8_t* buf) {
  // Apply parameters locally to DCO audio engine
  for (uint16_t id = 0; id < PRESET_PARAM_COUNT; ++id) {
    if (!(buf[PRESET_OFF_BITMAP + (id >> 3)] & (1u << (id & 7u)))) continue;
    if (!preset_param_is_persistable((uint8_t)id)) continue;
    const int16_t value = (int16_t)decode_u16_le(buf + PRESET_OFF_PARAMS + id * 2);
    update_parameters(id, value);
    // (serial_echo_persistable_param16 removed here to prevent packet storm)
  }

  // Unpack envelopes and filter
  const uint8_t* b = buf + PRESET_OFF_BLOCKS;
  ADSR_VCA_attack  = decode_u16_le(b + 0);
  ADSR_VCA_decay   = decode_u16_le(b + 2);
  ADSR_VCA_sustain = decode_u16_le(b + 4);
  ADSR_VCA_release = decode_u16_le(b + 6);
  ADSR_VCF_attack  = decode_u16_le(b + 8);
  ADSR_VCF_decay   = decode_u16_le(b + 10);
  ADSR_VCF_sustain = decode_u16_le(b + 12);
  ADSR_VCF_release = decode_u16_le(b + 14);
  ADSR1_attack     = decode_u16_le(b + 16);
  ADSR1_decay      = decode_u16_le(b + 18);
  ADSR1_sustain    = decode_u16_le(b + 20);
  ADSR1_release    = decode_u16_le(b + 22);
  CUTOFF           = decode_u16_le(b + 24);
  RESONANCE        = decode_u16_le(b + 26);
  ADSR2toVCF       = (int16_t)decode_u16_le(b + 28);
  LFO2toVCF        = decode_u16_le(b + 30);
  
  mark_adsr_params_dirty(ADSR_DIRTY_VCA_ALL | ADSR_DIRTY_VCF_ALL | ADSR_DIRTY_DCO_ALL);
  cv_bake_adsr2_to_vcf_scale();
  cv_bake_lfo2_to_vcf_scale();

  for (int i = 0; i < 16; ++i) {
    presetName[i] = buf[PRESET_OFF_NAME + i];
  }

  // Send fast blocks to Mainboard
  serial_send_adsr_vca_block_to_mb();
  serial_send_adsr_vcf_block_to_mb();
  serial_send_adsr_dco_block_to_mb();
  serial_send_filter_block_to_mb();

  // Send the 3 new domain blocks
  serial_send_patch_osc_block_to_mb();
  serial_send_patch_lfo_block_to_mb();
  serial_send_patch_mod_block_to_mb();
}

static void preset_store_write_last(uint8_t slot) {
  File f = LittleFS.open(PRESET_LAST_FILE, "w");
  if (!f) return;
  f.write(&slot, 1);
  f.close();
}

static bool preset_chunk_write_record(uint8_t slot, const uint8_t* record, const char* errTag) {
  const uint8_t chunk = preset_chunk_index(slot);
  const uint16_t offset = preset_slot_offset(slot);
  char fname[8];
  preset_chunk_filename(chunk, fname, sizeof(fname));

  const bool exists = LittleFS.exists(fname);
  bool needCreate = !exists;
  if (exists) {
    File check = LittleFS.open(fname, "r");
    if (!check || check.size() != PRESET_CHUNK_SIZE) needCreate = true;
    if (check) check.close();
  }

  if (needCreate) {
    File f = LittleFS.open(fname, "w");
    if (!f) {
      Serial.printf("[%s] err slot=%u reason=open\n", errTag, (unsigned)slot);
      return false;
    }
    static uint8_t emptyRecord[PRESET_RECORD_SIZE];
    for (uint8_t i = 0; i < PRESET_RECORDS_PER_FILE; ++i) {
      const uint8_t* src = ((slot & 3u) == i) ? record : emptyRecord;
      if (f.write(src, PRESET_RECORD_SIZE) != PRESET_RECORD_SIZE) {
        f.close();
        Serial.printf("[%s] err slot=%u reason=write\n", errTag, (unsigned)slot);
        return false;
      }
    }
    f.close();
    return true;
  }

  File f = LittleFS.open(fname, "r+");
  if (!f || !f.seek(offset) || f.write(record, PRESET_RECORD_SIZE) != PRESET_RECORD_SIZE) {
    if (f) f.close();
    Serial.printf("[%s] err slot=%u reason=write\n", errTag, (unsigned)slot);
    return false;
  }
  f.close();
  return true;
}

// --- Public API ---

// PARAM_PRESET_SAVE: builds record into RAM and persists to LittleFS
void preset_store_save(uint8_t slot) {
  preset_record_build(presetStoreRAM[slot]);
  if (!preset_chunk_write_record(slot, presetStoreRAM[slot], "preset")) return;
  preset_store_write_last(slot);
  Serial.printf("[preset] saved slot=%u name=\"%.16s\"\n", (unsigned)slot, (const char*)presetName);
}

// PARAM_PRESET_LOAD: 0 ms Pure In-RAM Recall! Zero LittleFS reads!
bool preset_store_load(uint8_t slot) {
  const uint8_t* record = presetStoreRAM[slot];

  if (!preset_record_validate(record)) {
    Serial.printf("[preset] err slot=%u reason=empty_or_corrupt\n", (unsigned)slot);
    return false;
  }

  serial_send_screen_signal_to_mb(SCREEN_SIGNAL_SILENT);
  preset_record_apply(record);
  preset_store_write_last(slot);

  Serial.printf("[preset] loaded slot=%u name=\"%.16s\"\n", (unsigned)slot, (const char*)presetName);
  serial_send_preset_loaded_to_mb(slot);
  serial_send_preset_scroll_to_mb(slot);
  serial_send_screen_signal_to_mb(SCREEN_SIGNAL_PRESET_SCROLL);
  return true;
}

// --- In-RAM Directory Push (Streams from RAM over DMA) ---
static uint8_t presetDirPushChunk = PRESET_CHUNK_COUNT;

void preset_store_send_directory_to_mb() {
  presetDirPushChunk = 0; // Arm the task
}

void preset_store_dir_push_task() {
  if (presetDirPushChunk >= PRESET_CHUNK_COUNT) return;

  const uint8_t chunk = presetDirPushChunk++;
  uint8_t entry[1 + PRESET_NAME_LEN];

  for (uint8_t i = 0; i < PRESET_RECORDS_PER_FILE; ++i) {
    const uint8_t slot = (uint8_t)((chunk << 2) | i);
    entry[0] = slot;
    
    // Copy name directly from RAM! If magic is invalid, it stays zeroed.
    if (presetStoreRAM[slot][PRESET_OFF_MAGIC] == PRESET_MAGIC) {
      memcpy(entry + 1, &presetStoreRAM[slot][PRESET_OFF_NAME], PRESET_NAME_LEN);
    } else {
      memset(entry + 1, 0, PRESET_NAME_LEN);
    }

    serial_frame_write(Serial2Dma, CMD_PRESET_DIR_ENTRY, entry, sizeof(entry));
  }
}

// --- Instant USB Dump directly from RAM ---
void preset_store_dump(int16_t sel) {
  if (sel < 0) {
    Serial.println("[pdir] begin");
    uint16_t count = 0;
    for (uint16_t slot = 0; slot < PRESET_NUM_SLOTS; ++slot) {
      if (presetStoreRAM[slot][PRESET_OFF_MAGIC] == PRESET_MAGIC) {
        char name[PRESET_NAME_LEN + 1];
        memcpy(name, &presetStoreRAM[slot][PRESET_OFF_NAME], PRESET_NAME_LEN);
        name[PRESET_NAME_LEN] = 0;
        Serial.printf("[pdir] slot=%03u name=\"%s\"\n", (unsigned)slot, name);
        ++count;
      }
    }
    Serial.printf("[pdir] end count=%u\n", (unsigned)count);
    return;
  }

  if (sel >= (int16_t)PRESET_NUM_SLOTS) return;
  if (presetStoreRAM[sel][PRESET_OFF_MAGIC] != PRESET_MAGIC) {
    dump_print_err("preset", "empty");
    return;
  }
  dump_buffer("preset", sel, presetStoreRAM[sel], PRESET_RECORD_SIZE);
}

void preset_store_cal_dump(int16_t sel) {
  const bool all = (sel <= CAL_DUMP_ALL);
  if (all || sel == CAL_DUMP_VOICE_TABLES)  dump_fs_file("voiceTables", "voiceTables", FSBankSize);
  if (all || sel == CAL_DUMP_PW_CENTER)     dump_fs_file("PWCenter", "PWCenter", FSPWBankSize);
  if (all || sel == CAL_DUMP_PW_HIGH_LIMIT) dump_fs_file("PWHighLimit", "PWHighLimit", FSPWBankSize);
  if (all || sel == CAL_DUMP_PW_LOW_LIMIT)  dump_fs_file("PWLowLimit", "PWLowLimit", FSPWBankSize);
  if (all || sel == CAL_DUMP_MANUAL_OFFSET) dump_fs_file("ManualOffset", "ManualOffset", FSManualOffsetBankSize);
  if (all || sel == CAL_DUMP_AMP_COMP_440)  dump_fs_file("AmpComp440", "AmpComp440", FSAmpComp440BankSize);
  if (all || sel == CAL_DUMP_AMP_COMP_DUTY) dump_fs_file("AmpCompDutyOffset", "AmpCompDutyOffset", FSAmpCompDutyOffsetBankSize);
}

void preset_bulk_chunk(const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_LEN_BULK_CHUNK) return;
  const uint16_t offset = decode_u16_le(payload + 2);
  if ((uint32_t)offset + PRESET_BULK_CHUNK_DATA > PRESET_BULK_STAGING_SIZE) return;
  memcpy(presetBulkStaging + offset, payload + 4, PRESET_BULK_CHUNK_DATA);
}

void preset_bulk_commit(const uint8_t* payload, uint8_t len) {
  if (len != SERIAL_LEN_BULK_COMMIT) return;
  const uint8_t  target = payload[0];
  const uint8_t  slot   = payload[1];
  const uint16_t size   = decode_u16_le(payload + 2);
  const uint32_t crc    = decode_u32_le(payload + 4);
  const char* tname = preset_bulk_target_name(target);

  if (size == 0 || size > PRESET_BULK_STAGING_SIZE) return;
  if (preset_crc32(presetBulkStaging, size) != crc) return;

  if (target == PRESET_BULK_PRESET) {
    if (!preset_record_validate(presetBulkStaging)) return;
    
    // Copy into RAM store and commit to LittleFS
    memcpy(presetStoreRAM[slot], presetBulkStaging, PRESET_RECORD_SIZE);
    if (!preset_chunk_write_record(slot, presetStoreRAM[slot], "bulk")) return;
    
    Serial.printf("[bulk] ok target=%s slot=%u\n", tname, (unsigned)slot);
    return;
  }

  uint16_t want = 0;
  const char* calFile = nullptr;
  switch (target) {
    case PRESET_BULK_VOICE_TABLES:  want = FSBankSize;             calFile = "voiceTables"; break;
    case PRESET_BULK_PW_CENTER:     want = FSPWBankSize;           calFile = "PWCenter"; break;
    case PRESET_BULK_PW_HIGH_LIMIT: want = FSPWBankSize;           calFile = "PWHighLimit"; break;
    case PRESET_BULK_PW_LOW_LIMIT:  want = FSPWBankSize;           calFile = "PWLowLimit"; break;
    case PRESET_BULK_MANUAL_OFFSET: want = FSManualOffsetBankSize; calFile = "ManualOffset"; break;
    case PRESET_BULK_AMP_COMP_440:  want = FSAmpComp440BankSize;   calFile = "AmpComp440"; break;
    case PRESET_BULK_AMP_COMP_DUTY: want = FSAmpCompDutyOffsetBankSize; calFile = "AmpCompDutyOffset"; break;
    default: return;
  }
  if (size != want) return;

  write_fs_bank(calFile, presetBulkStaging, size);
  init_FS();
  if (target == PRESET_BULK_VOICE_TABLES) precompute_amp_comp_for_engine();
  Serial.printf("[bulk] ok target=%s\n", tname);
}

void preset_store_boot_recall() {
  if (calibrationFlag) return;
  if (!LittleFS.exists(PRESET_LAST_FILE)) return;
  File f = LittleFS.open(PRESET_LAST_FILE, "r");
  if (!f) return;
  uint8_t slot = 0;
  const bool ok = (f.read(&slot, 1) == 1);
  f.close();
  if (!ok) return;
  preset_store_load(slot);
}