#ifndef SERIAL_INPUT_PROTOCOL_H
#define SERIAL_INPUT_PROTOCOL_H

#include <stdint.h>

// -----------------------------------------------------------------------------
// Input-board → hub protocol: command bytes and payload sizes.
//
// Historically Mainboard Serial8; after absorption this is DCO Serial2.
// Frame form:
//
//   [1 byte] command character
//   [N bytes] payload (length depends on command)
// -----------------------------------------------------------------------------

enum InputSerialCmd : char {
  INPUT_CMD_ADSR1_BLOCK     = 'a',  // EnvVCA times
  INPUT_CMD_ADSR2_BLOCK     = 'b',  // EnvVCF times
  INPUT_CMD_ADSR3_BLOCK     = 'c',  // EnvDCO times (maps to ADSR1_* on DCO)
  INPUT_CMD_FILTER_BLOCK    = 'd',
  INPUT_CMD_ADSR1_TO_VCA    = 'e',
  INPUT_CMD_PW_VALUE        = 'f',  // big-endian PW (unlike Mainboard→DCO 'f' LE)
  INPUT_CMD_PARAM_16        = 'p',
  INPUT_CMD_PARAM_8         = 'w',
  INPUT_CMD_PRESET_NAME     = 'q',
};

static constexpr uint8_t INPUT_SERIAL_LEN_ADSR_BLOCK   = 8;
static constexpr uint8_t INPUT_SERIAL_LEN_FILTER_BLOCK = 8;
static constexpr uint8_t INPUT_SERIAL_LEN_ADSR1_TO_VCA = 2;
static constexpr uint8_t INPUT_SERIAL_LEN_PW_VALUE     = 2;
static constexpr uint8_t INPUT_SERIAL_LEN_PARAM_16     = 4;
static constexpr uint8_t INPUT_SERIAL_LEN_PARAM_8      = 3;
static constexpr uint8_t INPUT_SERIAL_LEN_PRESET_NAME  = 9;

#endif // SERIAL_INPUT_PROTOCOL_H
