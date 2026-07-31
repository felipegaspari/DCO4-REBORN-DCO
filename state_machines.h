#ifndef __STATE_MACHINES_H__
#define __STATE_MACHINES_H__

#include <stdint.h>

void init_pio();
void start_voice_sms();
void assign_sm_mapping();
void set_subosc_divide(uint8_t divide);
void osc_load_period_stopped(uint8_t osc, uint32_t y, uint32_t clk_div);
void osc_set_reset_pulse(uint8_t osc, uint32_t y);
void pio_topology_report();
void pio_period_probe(uint8_t osc, uint32_t clk_div);
void pio_solve_period_model(uint32_t clk_div_a, double measured_hz_a,
                            uint32_t clk_div_b, double measured_hz_b, uint32_t y);

// The period model itself (PioPeriod, pio_period_split, pio_clk_div_for_y) and the PIO
// jump-target helpers live in globals.h, next to the state they read.

#endif