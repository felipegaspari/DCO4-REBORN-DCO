#ifndef __SERIAL_H__
#define __SERIAL_H__

#ifndef SERIAL_INNER_MAX_PAYLOAD
#define SERIAL_INNER_MAX_PAYLOAD 16
#endif

#include "serial_param_protocol.h"
#include "serial_protocol.h"
#include "serial_input_protocol.h"
#include "serial_frame.h"
#include "serial_parser.h"

// Serial1 = DIN MIDI @ 31250; Serial2 = Mainboard @ 2.5M
// ('n'/'o'/'e' TX, USB/MIDI 'a'/'b'/'d' mirror, 'm'/'p'/'t' RX).
// Gap 154 / cal 155 go out as 'x' on Serial2; Mainboard relays to Input → Screen.

void init_serial();
void init_usb();
void serial_panel_task();

#ifdef ENABLE_USB_CONTROL
// USB CDC bench link: Input-style inner frames (no Mainboard required).
void serial_usb_task();
#endif

void serialSendParam32(byte paramNumber, uint32_t paramValue);
void serialSendParam16(byte paramNumber, int16_t paramValue, bool force = false);
// Echo LittleFS-persistable 'p' to Mainboard (USB/MIDI only; never MB→DCO loop).
void serial_echo_persistable_param16(uint8_t id, int16_t value);
// Analog VCA/VCF blocks → Mainboard (MIDI CC; USB 'a'/'b'/'d' use the USB drain mirror).
void serial_send_adsr_vca_block_to_mb();
void serial_send_adsr_vcf_block_to_mb();
void serial_send_filter_block_to_mb();

void serial_send_note_on(uint8_t voice, uint8_t velocity, uint8_t note, uint8_t flags);
void serial_send_note_off(uint8_t voice);
void serial_send_expression();
void mb_bench_text_drain();

#endif
