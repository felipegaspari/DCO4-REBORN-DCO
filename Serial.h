#ifndef __SERIAL_H__
#define __SERIAL_H__

// DCO accepts the 36-byte 'B' bulk-restore chunk, so inner cap is 36.
#ifndef SERIAL_INNER_MAX_PAYLOAD
#define SERIAL_INNER_MAX_PAYLOAD 36
#endif

// Shared Protocol Includes
#include "_build_libs/DCO-PROTOCOL/serial_param_protocol.h"
#include "_build_libs/DCO-PROTOCOL/serial_input_protocol.h"
#include "_build_libs/DCO-PROTOCOL/serial_frame.h"
#include "_build_libs/DCO-PROTOCOL/serial_parser.h"

// NEW: Shared DMA Library replaces serial2_dma.h
#include "_build_libs/DCO-PROTOCOL/serial_dma_tx.h"


extern UartDmaTx Serial2Dma;

void init_serial();
void init_usb();
void serial_panel_task();

#ifdef ENABLE_USB_CONTROL
void serial_usb_task();
#endif

void serialSendParam32(byte paramNumber, uint32_t paramValue, bool force = false);
void serialSendParam16(byte paramNumber, int16_t paramValue, bool force = false);
void serial_echo_persistable_param16(uint8_t id, int16_t value);

void serial_send_adsr_vca_block_to_mb();
void serial_send_adsr_vcf_block_to_mb();
void serial_send_adsr_dco_block_to_mb();
void serial_send_filter_block_to_mb();

void serial_send_preset_loaded_to_mb(uint8_t slot);
void serial_send_preset_scroll_to_mb(uint8_t slot);
void serial_send_screen_signal_to_mb(uint8_t signal);

void serial_send_note_on(uint8_t voice, uint8_t velocity, uint8_t note, uint8_t flags);
void serial_send_note_off(uint8_t voice);
void serial_send_expression();
void mb_bench_text_drain();

void serial_send_patch_osc_block_to_mb();
void serial_send_patch_lfo_block_to_mb();
void serial_send_patch_mod_block_to_mb();

#endif
