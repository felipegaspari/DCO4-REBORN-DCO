#ifndef __SERIAL_H__
#define __SERIAL_H__

#include "serial_param_protocol.h"
#include "serial_protocol.h"
#include "serial_input_protocol.h"
#include "serial_parser.h"

// Serial1 = DIN MIDI @ 31250; Serial2 = Input panel protocol + 'x' TX @ 2.5M.
// Screen has no DCO port: Input relays gap 154 to it on its own Screen port.

void init_serial();
void serial_panel_task();

void serialSendParam32(byte paramNumber, uint32_t paramValue);

#endif
