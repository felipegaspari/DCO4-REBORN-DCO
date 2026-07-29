#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.h"
#ifndef __SERIAL_H__
#define __SERIAL_H__

#include "serial_param_protocol.h"
#include "serial_protocol.h"
#include "serial_input_protocol.h"
#include "serial_parser.h"

// ENABLE_INPUT_UART may be set in DCO.ino (hub mode: Serial2 = Input protocol).

void init_serial();

#endif
