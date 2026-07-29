#line 1 "/home/felipe/Documentos/DCO3-MONOSYNTH/DCO/Serial.h"
#ifndef __SERIAL_H__
#define __SERIAL_H__

#include "serial_param_protocol.h"
#include "serial_protocol.h"
#include "serial_input_protocol.h"
#include "serial_parser.h"

// ENABLE_INPUT_UART  — Serial2 = Input protocol (hub)
// ENABLE_SCREEN_UART — SerialPIO Screen @ GP8 TX / GP9 RX (interim; HW UART after PIO MIDI)

void init_serial();
void serial_panel_task();
#define serial_STM32_task serial_panel_task  // legacy name

void serialSendParam32(byte paramNumber, uint32_t paramValue);
#ifdef ENABLE_SCREEN_UART
void serialSendParam32ToScreen(byte paramNumber, uint32_t paramValue);
#endif

#endif
