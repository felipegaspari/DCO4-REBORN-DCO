/**
 * @file preset_store.h
 * @brief MCU-side Full In-RAM Preset Store (RP2350/RP2040) + LittleFS Backing Store.
 * @details All 256 presets (153 KB) stay resident in RAM for 0-latency live performance.
 */

 #ifndef PRESET_STORE_H
 #define PRESET_STORE_H
 
 #include <stdint.h>
 #include <stddef.h>
 #include "_build_libs/DCO-PROTOCOL/params_def.h"
 #include "_build_libs/DCO-PROTOCOL/serial_input_protocol.h"
 
 // PRESET_NUM_SLOTS (256) and PRESET_NAME_LEN (16) come from serial_input_protocol.h
 static constexpr uint8_t  PRESET_RECORDS_PER_FILE  = 4;
 static constexpr uint8_t  PRESET_CHUNK_COUNT       = PRESET_NUM_SLOTS / PRESET_RECORDS_PER_FILE; // 64
 static constexpr uint8_t  PRESET_MAGIC             = 0xA5;
 static constexpr uint8_t  PRESET_VERSION           = 1;
 
 // Record layout (all little-endian). CRC32 covers bytes [0 .. PRESET_OFF_CRC).
 static constexpr uint16_t PRESET_PARAM_COUNT  = 256;  
 static constexpr uint8_t  PRESET_BLOCK_FIELDS = 16;   
 static constexpr uint16_t PRESET_OFF_MAGIC   = 0;
 static constexpr uint16_t PRESET_OFF_VERSION = 1;
 static constexpr uint16_t PRESET_OFF_NAME    = 2;
 static constexpr uint16_t PRESET_OFF_BITMAP  = 18;   
 static constexpr uint16_t PRESET_OFF_PARAMS  = 50;   
 static constexpr uint16_t PRESET_OFF_BLOCKS  = 562;  
 static constexpr uint16_t PRESET_OFF_CRC     = 594;  
 static constexpr uint16_t PRESET_RECORD_SIZE = 598;
 static constexpr uint16_t PRESET_CHUNK_SIZE  =
     (uint16_t)PRESET_RECORDS_PER_FILE * PRESET_RECORD_SIZE;  // 2392
 
 enum PresetBulkTarget : uint8_t {
   PRESET_BULK_PRESET            = 0,
   PRESET_BULK_VOICE_TABLES      = 1,
   PRESET_BULK_PW_3PT            = 2, ///< 3-Point PW Calibration Bank ("PWCal3Pt")
   PRESET_BULK_AMP_COMP_TOP_PAIR = 3, ///< Amp Comp Top Valid Pair Indices ("AmpCompTopPair")
   PRESET_BULK_MANUAL_OFFSET     = 5,
   PRESET_BULK_AMP_COMP_440      = 6,
   PRESET_BULK_AMP_COMP_DUTY     = 7,
 
   // Legacy aliases
   PRESET_BULK_PW_CENTER         = 2,
 };
 
 static constexpr uint8_t  PRESET_BULK_CHUNK_DATA   = 32;
 static constexpr uint16_t PRESET_BULK_STAGING_SIZE = 1440;
 
 static constexpr int16_t CAL_DUMP_ALL               = 0;
 static constexpr int16_t CAL_DUMP_VOICE_TABLES      = 1;
 static constexpr int16_t CAL_DUMP_PW_3PT            = 2;
 static constexpr int16_t CAL_DUMP_AMP_COMP_TOP_PAIR = 3;
 static constexpr int16_t CAL_DUMP_MANUAL_OFFSET     = 5;
 static constexpr int16_t CAL_DUMP_AMP_COMP_440      = 6;
 static constexpr int16_t CAL_DUMP_AMP_COMP_DUTY     = 7;
 
 // --- Live patch shadow & Full 153 KB In-RAM Preset Bank ---
 extern int16_t presetParamShadow[PRESET_PARAM_COUNT];
 extern uint8_t presetParamSetBitmap[PRESET_PARAM_COUNT / 8];
 extern uint8_t presetStoreRAM[PRESET_NUM_SLOTS][PRESET_RECORD_SIZE]; // 153,088 bytes
 
 extern bool presetBootPending;
 static constexpr uint32_t PRESET_BOOT_RECALL_MS = 1500;
 
 static inline uint32_t preset_crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
   static const uint32_t tbl[16] = {
     0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
     0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
     0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
     0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C
   };
   for (size_t i = 0; i < len; ++i) {
     crc ^= data[i];
     crc = (crc >> 4) ^ tbl[crc & 0x0F];
     crc = (crc >> 4) ^ tbl[crc & 0x0F];
   }
   return crc;
 }
 
 static inline uint32_t preset_crc32(const uint8_t* data, size_t len) {
   return preset_crc32_update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
 }
 
 /**
  * @brief Compatibility wrapper delegating to shared param_is_persistable().
  */
 static inline bool preset_param_is_persistable(uint8_t id) {
   return param_is_persistable(id);
 }
 
 static inline void preset_shadow_capture(uint16_t id, int16_t value) {
   if (id >= PRESET_PARAM_COUNT) return;
   if (!param_is_persistable((uint8_t)id)) return;
 
   presetParamShadow[id] = value;
   presetParamSetBitmap[id >> 3] |= (uint8_t)(1u << (id & 7u));
 
   if (id == (uint16_t)PARAM_OSC1_PULSE_ENABLE) {
     pulseWaveOn = (value != 0);
   }
 }
 
 // --- API Prototypes ---
 void preset_store_init_ram();
 void preset_store_save(uint8_t slot);
 bool preset_store_load(uint8_t slot);
 void preset_store_dump(int16_t sel);      
 void preset_store_cal_dump(int16_t sel);  
 void preset_bulk_chunk(const uint8_t* payload, uint8_t len);
 void preset_bulk_commit(const uint8_t* payload, uint8_t len);
 void preset_store_boot_recall();
 void preset_store_send_directory_to_mb();
 void preset_store_dir_push_task();
 
 static inline void preset_store_boot_task() {
   if (!presetBootPending) return;
   if (millis() < PRESET_BOOT_RECALL_MS) return;
   presetBootPending = false;
   preset_store_boot_recall();
 }
 
 #endif  // PRESET_STORE_H