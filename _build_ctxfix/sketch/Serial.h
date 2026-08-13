#line 1 "/home/felipe/Documentos/DCO4-REBORN/DCO/Serial.h"
#ifndef __SERIAL_H__
#define __SERIAL_H__

// DCO accepts the 36-byte 'B' bulk-restore chunk (preset_store.h), so the inner
// payload cap must be raised before serial_frame.h locks its default of 8.
#ifndef SERIAL_INNER_MAX_PAYLOAD
#define SERIAL_INNER_MAX_PAYLOAD 36
#endif

#include "serial_param_protocol.h"
#include "serial_protocol.h"
#include "serial_input_protocol.h"
#include "serial_frame.h"
#include "serial_parser.h"
#include "serial2_dma.h"

// Serial1 = DIN MIDI @ 31250; Serial2 = Mainboard @ 2.5M
// ('n'/'o'/'e' TX, USB/MIDI 'a'/'b'/'d' mirror, 'm'/'p'/'t' RX).
// Serial2 TX is uart1 DMA; RX stays Arduino IRQ. MIDI and USB CDC unchanged.
// Gap 154 / cal 155 go out as 'x' on Serial2; Mainboard relays to Input → Screen.

void init_serial();
void init_usb();
void serial_panel_task();

#ifdef ENABLE_USB_CONTROL
// USB CDC bench link: Input-style inner frames (no Mainboard required).
void serial_usb_task();
#endif

void serialSendParam32(byte paramNumber, uint32_t paramValue, bool force = false);
void serialSendParam16(byte paramNumber, int16_t paramValue, bool force = false);
// Echo LittleFS-persistable 'p' to Mainboard (USB/MIDI only; never MB→DCO loop).
void serial_echo_persistable_param16(uint8_t id, int16_t value);
// Analog VCA/VCF blocks → Mainboard (MIDI CC + preset recall; USB 'a'-'d' use
// the USB drain mirror). The EnvDCO block is DCO-local engine state; it goes out
// so the Mainboard can relay it to the panel and Screen.
void serial_send_adsr_vca_block_to_mb();
void serial_send_adsr_vcf_block_to_mb();
void serial_send_adsr_dco_block_to_mb();
void serial_send_filter_block_to_mb();

// 'L' [slot] out on Serial2 at the end of every successful preset_store_load()
// (boot recall, MIDI PC, USB/dco_control, Input-triggered). The Mainboard relays
// it to Input so the Screen's preset display reflects the DCO's actual current
// slot even for loads Input didn't itself trigger.
void serial_send_preset_loaded_to_mb(uint8_t slot);

// Screen 'q' [slot][name:16] out on Serial2. Mainboard relays to Screen USART1
// so dco_control / MIDI / boot recall show number and name without waiting on
// Input's directory cache.
void serial_send_preset_scroll_to_mb(uint8_t slot);

// 's' [ScreenMode] out on Serial2, relayed verbatim to the Screen by the
// Mainboard. Used to bracket a preset recall in ScreenMode::Silent so the burst
// of mirrored params does not bury the preset name under parameter toasts.
void serial_send_screen_signal_to_mb(uint8_t signal);

void serial_send_note_on(uint8_t voice, uint8_t velocity, uint8_t note, uint8_t flags);
void serial_send_note_off(uint8_t voice);
void serial_send_expression();
void mb_bench_text_drain();

#endif
