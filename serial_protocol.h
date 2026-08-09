#ifndef SERIAL_PROTOCOL_H
#define SERIAL_PROTOCOL_H

#include <stdint.h>
#include "serial_input_protocol.h"

// -----------------------------------------------------------------------------
// Mainboard ↔ DCO slim LE command set (Serial2 @ 2.5 M).
// Panel Input protocol remains in serial_input_protocol.h (USB bench).
// See docs/MAINBOARD_REINTEGRATION.md.
// -----------------------------------------------------------------------------

enum SerialCmd : uint8_t {
  SERIAL_CMD_NOTE_ON     = 'n',  // DCO → MB
  SERIAL_CMD_NOTE_OFF    = 'o',  // DCO → MB
  SERIAL_CMD_EXPRESSION  = 'e',  // DCO → MB  AT / MW / pitch bend
  SERIAL_CMD_MOD_STREAM  = 'm',  // MB → DCO  LFO + EnvDCO + matrix pitch
  SERIAL_CMD_BENCH_TEXT  = 't',  // MB → DCO  dump ASCII chunk
  SERIAL_CMD_PARAM_16    = 'p',
  SERIAL_CMD_PARAM_32    = 'x',
};

static constexpr uint8_t SERIAL_PAYLOAD_LEN_NOTE_ON    = 4;
static constexpr uint8_t NOTE_FLAG_RETRIGGER           = (1u << 0);
static constexpr uint8_t NOTE_FLAG_PORTA_ONLY          = (1u << 1);
static constexpr uint8_t SERIAL_PAYLOAD_LEN_NOTE_OFF   = 1;
static constexpr uint8_t SERIAL_PAYLOAD_LEN_EXPRESSION = 4;
static constexpr uint8_t SERIAL_PAYLOAD_LEN_MOD_STREAM = 16;
static constexpr uint8_t SERIAL_PAYLOAD_LEN_BENCH_TEXT = 16;
static constexpr uint8_t SERIAL_BENCH_TEXT_DATA_MAX    = 15;

static inline uint8_t serial_protocol_payload_len(uint8_t cmd) {
  switch (cmd) {
    case SERIAL_CMD_NOTE_ON:     return SERIAL_PAYLOAD_LEN_NOTE_ON;
    case SERIAL_CMD_NOTE_OFF:    return SERIAL_PAYLOAD_LEN_NOTE_OFF;
    case SERIAL_CMD_EXPRESSION:  return SERIAL_PAYLOAD_LEN_EXPRESSION;
    case SERIAL_CMD_MOD_STREAM:  return SERIAL_PAYLOAD_LEN_MOD_STREAM;
    case SERIAL_CMD_BENCH_TEXT:  return SERIAL_PAYLOAD_LEN_BENCH_TEXT;
    case SERIAL_CMD_PARAM_16:    return INPUT_SERIAL_LEN_PARAM_16;
    case SERIAL_CMD_PARAM_32:    return INPUT_SERIAL_LEN_PARAM_32;
    default:                     return 0;
  }
}

#endif // SERIAL_PROTOCOL_H
