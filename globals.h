#include <cstddef>
#include "include_all.h"

#ifndef __GLOBALS_H__
#define __GLOBALS_H__



#define NUM_VOICES_TOTAL 1
#define NUM_OSCILLATORS 3
#ifndef NUM_FILTERS
#define NUM_FILTERS 2
#endif


#define MIDI_CHANNEL 1
//#define USE_ADC_STACK_VOICES // gpio 28 (adc 2)
//#define USE_ADC_DETUNE       // gpio 27 (adc 1)

// Unused SPI (kept commented — MOSI collided with PW_PINS[0]=3).
// #define SCK 2
// #define MOSI 3
// #define MISO 4
// #define CS 5

#define ENABLE_FS_CALIBRATION

static constexpr uint32_t sysClock = 225000;
static constexpr uint32_t sysClock_Hz = sysClock * 1000;

static constexpr uint16_t DIV_COUNTER = 14000;
static constexpr uint16_t DIV_COUNTER_PW = 1024;

static constexpr uint32_t pioPulseLength = 3000;
static constexpr uint32_t pioPulseLengthTimesEight = pioPulseLength * 8;
static constexpr uint32_t eightPioPulseLength = pioPulseLength / 8;

// --- PIO Program Timing Constants ---
// `jmp x-- lp` executes X+1 times: it jumps while X is non-zero, then spends one
// more cycle falling through on X == 0. All five delay loops therefore cost one
// cycle beyond their count, which the previous T_LOW_OVERHEAD_CYCLES = 5 did not
// account for (it was short by exactly those five cycles).
//
// frequency_sync_4_jumps, one full period, offset-relative addresses:
//   a11 set pins,1        1
//   a0  lp0               Y + 1
//   a1  mov x, OSR        1
//   a2  set pins,0        1
//   a3  lp1               clk_div + 1
//   a4  mov x, OSR        1
//   a5  lp2               clk_div + 1
//   a6  mov x, OSR        1
//   a7  lp3               clk_div + 1
//   a8  mov x, OSR        1
//   a9  loop_final        clk_div + 1
//   a10 mov x, y          1
// => period = Y + 4*clk_div + 12
static constexpr uint32_t T_HIGH_OVERHEAD_CYCLES = 2;
static constexpr uint32_t T_LOW_OVERHEAD_CYCLES = 10;
static constexpr uint32_t NUM_OSR_CHUNKS = 4;

// Per-program period model: period = Y + RAMP_WEIGHT * clk_div + PERIOD_OVERHEAD.
// The polled sync program spends 2 cycles per iteration in its final chunk
// (jmp pin + jmp x--), so that chunk counts twice toward the weight.
static constexpr uint32_t PIO_RAMP_WEIGHT_FREE = 4;
static constexpr uint32_t PIO_PERIOD_OVERHEAD_FREE = 12;
static constexpr uint32_t PIO_RAMP_WEIGHT_SYNC = 5;
static constexpr uint32_t PIO_PERIOD_OVERHEAD_SYNC = 13;

// --- DERIVED CONSTANTS (Pre-calculated at compile time ) ---
// The total, real duration of the high pulse in cycles.
static constexpr uint32_t T_HIGH_TOTAL_CYCLES = pioPulseLength + T_HIGH_OVERHEAD_CYCLES;

static constexpr uint32_t halfSysClock_Hz = sysClock_Hz / 2;
static constexpr uint32_t eightSysClock_Hz_u = sysClock_Hz / 8;
static constexpr uint32_t eightSysClockMinusPulseLength_Hz_u = (sysClock_Hz - pioPulseLength - 8) / 8;
// Q24-scaled clock constants to avoid per-loop shifts
// (removed) Q24-scaled clock constants; direct shift used at call-site


uint32_t loop0_micros;
uint32_t loop1_micros;
uint32_t loop0_microsLast;
uint32_t loop1_microsLast;

volatile uint8_t NUM_VOICES = NUM_VOICES_TOTAL;
volatile uint8_t STACK_VOICES = 1;

volatile uint8_t voiceMode = 1;
uint8_t syncMode = 0;
uint8_t oscSync = 0;
volatile uint8_t polyMode = 1;

volatile uint16_t phaseAlignOSC2 = 0;
// (removed) phaseAlignScale_Q16; use direct computation at call-site

uint8_t unisonDetune = 0;
uint8_t analogDrift = 0;
uint8_t analogDriftSpeed = 0;
uint8_t analogDriftSpread = 0;

float DETUNE = 0.0f, LAST_DETUNE = 0.0f;
float DETUNE2 = 1.00f;

// LFO1 detune modulation (previously float) is now stored as Q24 fixed-point.
// This value represents the additive log-frequency modifier produced by LFO1.
int32_t DETUNE_INTERNAL_q24 = 0;

// LFO2 detune modulation for OSC2 / OSC3, in Q24 fixed-point.
volatile int32_t DETUNE_INTERNAL2_q24 = 0;
volatile int32_t DETUNE_INTERNAL3_q24 = 0;

// Raw 32-bit container used to transfer DETUNE_INTERNAL_q24 between cores via FIFO.
uint32_t DETUNE_INTERNAL_FIFO = 1;
uint32_t* detune_fifo_variable = &DETUNE_INTERNAL_FIFO;

// Detune value as received on core 1, in Q24 fixed-point.
int32_t DETUNE_INTERNAL_FIFO_q24 = (1 << 24);

float BASE_NOTE = 440.0f;


// WEACT RP2040 (legacy 8-osc map — kept for reference):
// static constexpr uint8_t RESET_PINS[8] = { 29, 27, 19, 18, 15, 13, 12, 8 };
// static constexpr uint8_t RANGE_PINS[8] = { 28, 22, 17, 16, 14, 11, 9, 7 };

// Raspberry Pi Pico (legacy):
// static constexpr uint8_t RESET_PINS[8] = { 28, 26, 19, 18, 15, 13, 12, 8 };
// static constexpr uint8_t RANGE_PINS[8] = { 27, 22, 17, 16, 14, 11,  9,  7 };

// Pico 2 provisional pinout: OSC1–3 taken from the legacy WEACT DCO4 map
// (first three oscillators). Replace when the monosynth PCB pinout is final.
// GPIO 24 is board fix-rail (see DCO.ino), not a DCO output.
//
// Hub + CV absorption (Mainboard retire) — provisional map:
//   docs/PINOUT.md , docs/MAINBOARD_ABSORPTION.md
//   Input UART  GP20 TX / GP21 RX  (recycle today's Mainboard Serial2)
//   Screen UART GP8  TX / GP9  RX
//   DIN MIDI    GP0/GP1 via PIO UART (interim: keep HW UART on 0/1)
//   Cutoff      GP15, GP4   Reso GP5, GP7   VCA GP11
//   Dist Drive  GP9         Dist Mix GP26 (slice 5 A with VCA on 5 B)
//   74HC595     GP12/13/14
//   OSC1..3+Sub level PWM → analog level VCAs (I2C level DAC removed):
//     GP16 / GP18 (ex-I2C); OSC3/Sub on freed Dist pins when ENABLE_VOICE_AUX,
//     else RP2350B GP32/33. GP2 aliases GP18; GP6 aliases RANGE OSC2 — do not use.
//
// CV PWM wrap is 4095 except Reso1 (GP7): shares slice 3 with RANGE OSC2 (GP22),
// so that channel scales duty into DIV_COUNTER (see write_cv_pwm). OSC1 level
// shares slice 0 with RANGE OSC3; OSC2 level shares slice 1 with PW — same idea.
#if defined(ENABLE_CV_OUTS) || defined(ENABLE_WAVE_MUX)
static constexpr uint8_t CUTOFF_PINS[NUM_FILTERS] = { 15, 4 };
static constexpr uint8_t RESO_PINS[NUM_FILTERS]   = { 5, 7 };
static constexpr uint8_t VCA_PIN                  = 11;
static constexpr uint8_t DIST_DRIVE_PIN           = 9;
static constexpr uint8_t DIST_MIX_PIN             = 26;
static constexpr uint8_t HC595_DATA_PIN           = 12;
static constexpr uint8_t HC595_LATCH_PIN          = 13;
static constexpr uint8_t HC595_CLK_PIN            = 14;
static constexpr uint8_t OSC1_LEVEL_PIN           = 16;
static constexpr uint8_t OSC2_LEVEL_PIN           = 18;
#ifdef ENABLE_VOICE_AUX
static constexpr uint8_t OSC3_LEVEL_PIN           = 9;   // Dist Drive pin freed on DCO
static constexpr uint8_t SUB_LEVEL_PIN            = 26;  // Dist Mix pin; slice 5 w/ VCA
#else
static constexpr uint8_t OSC3_LEVEL_PIN           = 32;  // RP2350B provisional
static constexpr uint8_t SUB_LEVEL_PIN            = 33;  // RP2350B provisional
#endif
static constexpr uint16_t DIV_COUNTER_CV          = 4095;
#endif

static constexpr uint8_t RESET_PINS[NUM_OSCILLATORS] = { 29, 27, 19 };
static constexpr uint8_t RANGE_PINS[NUM_OSCILLATORS] = { 28, 22, 17 };

// Freq SMs all live on pio0 so that the sideset hard-sync path works: a GPIO's
// function select can name only one PIO block, so oscillators spread across
// pio0/1/2 could not share a reset pin — `pio_gpio_init` on the second block
// simply stole the pin from the first. State machines inside one block do share
// the function select, which is why this worked on DCO4.
static constexpr uint8_t VOICE_TO_PIO[NUM_OSCILLATORS] = { 0, 0, 0 };

// Mutable: the slave always takes the lower SM index. When two SMs write the same
// pin on the same cycle the higher-numbered SM wins, so the master must outrank
// its slave or it would occasionally lose a sync edge. Rewritten by
// assign_sm_mapping() whenever syncMode changes.
uint8_t VOICE_TO_SM[NUM_OSCILLATORS] = { 0, 1, 2 };

// pio1 is reserved for the sub-oscillator, pio2 for ENABLE_PIO_MIDI.
static constexpr uint8_t SUBOSC_PIO = 1;

static constexpr uint8_t PW_PINS[NUM_VOICES_TOTAL] = { 3 };

// Sub-oscillator square output. GP8 was freed when the SerialPIO screen UART was
// removed; needs a mixer input on the carrier before it does anything audible.
static constexpr uint8_t SUBOSC_PIN = 8;

static constexpr int DCO_calibration_pin = 10;

uint8_t RANGE_PWM_SLICES[NUM_OSCILLATORS];
uint8_t VCO_PWM_SLICES[NUM_OSCILLATORS];
uint8_t PW_PWM_SLICES[NUM_VOICES_TOTAL];
#ifdef ENABLE_CV_OUTS
uint8_t CUTOFF_PWM_SLICES[NUM_FILTERS];
uint8_t CUTOFF_PWM_CHANS[NUM_FILTERS];
uint8_t RESO_PWM_SLICES[NUM_FILTERS];
uint8_t RESO_PWM_CHANS[NUM_FILTERS];
uint8_t VCA_PWM_SLICE;
uint8_t VCA_PWM_CHAN;
#ifndef ENABLE_VOICE_AUX
uint8_t DIST_DRIVE_PWM_SLICE;
uint8_t DIST_DRIVE_PWM_CHAN;
uint8_t DIST_MIX_PWM_SLICE;
uint8_t DIST_MIX_PWM_CHAN;
#endif
uint8_t OSC1_LEVEL_PWM_SLICE;
uint8_t OSC1_LEVEL_PWM_CHAN;
uint8_t OSC2_LEVEL_PWM_SLICE;
uint8_t OSC2_LEVEL_PWM_CHAN;
uint8_t OSC3_LEVEL_PWM_SLICE;
uint8_t OSC3_LEVEL_PWM_CHAN;
uint8_t SUB_LEVEL_PWM_SLICE;
uint8_t SUB_LEVEL_PWM_CHAN;
#endif

uint16_t PW_CENTER[NUM_VOICES_TOTAL] = { 570 };
uint16_t PW_LOW_LIMIT[NUM_VOICES_TOTAL] = { 0 };
uint16_t PW_HIGH_LIMIT[NUM_VOICES_TOTAL] = { DIV_COUNTER_PW };
uint16_t PW_LOOKUP[3] = { 0, (DIV_COUNTER_PW / 2) - 1, DIV_COUNTER_PW - 1 };
uint16_t PW_PWM[NUM_VOICES_TOTAL];

volatile uint32_t VOICES[NUM_VOICES_TOTAL];
volatile uint8_t VOICES_LAST[NUM_VOICES_TOTAL];
volatile uint8_t VOICES_LAST_SEQUENCE[NUM_VOICES_TOTAL] = { 0 };
volatile uint8_t VOICE_NOTES[NUM_VOICES_TOTAL];
volatile uint8_t NEXT_VOICE = 0;

uint32_t LED_BLINK_START = 0;

// PIO state machines for RP2350 Pico
// PIO pio[3] = { pio0, pio1, pio2 };

//PIO state machines for RP2040 (legacy)
PIO pio[2] = { pio0, pio1};

uint8_t midi_serial_status = 0;
int midi_pitch_bend = 8192, last_midi_pitch_bend = 8192;
uint8_t pitchBendRange = 2;

// Precompute 1/12 in Q24 for fast multiplier calculation
static constexpr int32_t RECIP_TWELVE_Q24 = (int32_t)((1.0f / 12.0f) * (float)(1 << 24));
// Precompute 1/360 in Q24 for fast phaseDelay calculation (full 0–360° range)
static constexpr uint32_t RECIP_360_Q24 = (uint32_t)(((1ULL << 24) + 180) / 360);
float pitchBendMultiplier = 1.00f / 12.00f * (float)pitchBendRange;
int32_t pitchBendMultiplier_q24 = 1 << 24;

uint16_t raw;

void init_sm(PIO pio, uint sm, uint offset, uint pin);
void set_frequency(PIO pio, uint sm, float freq);
float get_freq_from_midi_note(uint8_t note);
void led_blinking_task();
uint8_t get_free_voice();
void usb_midi_task();
void serial_midi_task();
void note_on(uint8_t note, uint8_t velocity);
void note_off(uint8_t note);
void voice_task();
void voice_task_float();
void voice_task_main();
void adc_task();


// Both oscillator programs stay resident in pio0; an SM just points at whichever
// one its role needs. The sub-oscillator program lives on SUBOSC_PIO.
uint32_t pio_offset_free = 0;
uint32_t pio_offset_sync = 0;
uint32_t subosc_offset_div2 = 0;
uint32_t subosc_offset_div4 = 0;

// Per-oscillator PIO state: which resident program the SM runs, and the reset
// pulse width (Y) last pushed to it. Y is only rewritten at note-on — see
// pio_period_split() for why it cannot safely be updated every control frame.
bool osc_uses_sync_program[NUM_OSCILLATORS] = { false, false, false };
uint32_t osc_last_y[NUM_OSCILLATORS] = { pioPulseLength, pioPulseLength, pioPulseLength };

// Last clk_div handed to each SM. Writing Y consumes the OSR, so clk_div always has to
// be re-pushed afterwards; without a remembered value the SM would read a shifted-out
// OSR as its ramp count and shriek for one control frame. 200 is the slow "park" rate
// used elsewhere during calibration.
uint32_t osc_last_clk_div[NUM_OSCILLATORS] = { 200, 200, 200 };

// Soft sync: number of trailing ramp chunks that poll the master's reset pin.
// 0 selects hard sync through the sideset pin instead (no resolution cost).
// Only 0 and 1 are implemented: with 1, the final chunk polls, and because that chunk runs
// at half speed it occupies 2/5 of the ramp, so master edges in roughly the first 60% of
// the slave's ramp are ignored. Deeper thresholds would need another program variant, and
// pio0 has no room for one alongside the two already resident.
uint8_t softSyncChunks = 0;

// Sub-oscillator divide ratio: 0 = off, 2 = one octave down, 4 = two octaves.
uint8_t subOscDivide = 0;

// Program-relative addresses. Both oscillator programs share the same layout up to the
// final chunk, so only the restart address differs.
//   RESTART: `mov x, y`, the top of the reset pulse. Jumping here retriggers a cycle.
//   RAMP_ENTRY[q]: enters the ramp with 4-q chunks left, advancing phase by q quarters.
//     This skips `set pins, 0` at address 2, so the caller must drive the reset pin low
//     first. Index 0 is unused; a zero advance is just a restart.
static constexpr uint8_t PIO_RESTART_ADDR_FREE = 10;
static constexpr uint8_t PIO_RESTART_ADDR_SYNC = 11;
static constexpr uint8_t PIO_RAMP_ENTRY[4] = { 0, 4, 6, 8 };

static inline uint32_t osc_program_base(uint8_t osc) {
  return osc_uses_sync_program[osc] ? pio_offset_sync : pio_offset_free;
}

static inline uint32_t osc_restart_target(uint8_t osc) {
  return osc_uses_sync_program[osc] ? (pio_offset_sync + PIO_RESTART_ADDR_SYNC)
                                    : (pio_offset_free + PIO_RESTART_ADDR_FREE);
}

static inline uint32_t osc_ramp_entry_target(uint8_t osc, uint8_t quarters) {
  return osc_program_base(osc) + PIO_RAMP_ENTRY[quarters & 3];
}

// Period model of whichever program this oscillator is running.
static inline uint32_t osc_ramp_weight(uint8_t osc) {
  return osc_uses_sync_program[osc] ? PIO_RAMP_WEIGHT_SYNC : PIO_RAMP_WEIGHT_FREE;
}

static inline uint32_t osc_period_overhead(uint8_t osc) {
  return osc_uses_sync_program[osc] ? PIO_PERIOD_OVERHEAD_SYNC : PIO_PERIOD_OVERHEAD_FREE;
}

// Result of splitting a target period into the PIO's reset pulse and ramp chunks.
struct PioPeriod {
  uint32_t clk_div;  // per-chunk count pushed to the SM's OSR
  uint32_t y;        // reset pulse width; pioPulseLength plus the division remainder
};

// Split `total_cycles` exactly into y + weight*clk_div + overhead.
//
// The remainder of the chunk division lands in the reset pulse rather than being rounded
// away, so the generated period matches the target to the cycle instead of quantising to
// `weight` cycles (about 0.2 cents at 7 kHz with weight 4). The pulse wobbles by
// 0..weight-1 cycles, at most 13 ns at 225 MHz, which is nothing against a reset pulse
// measured in microseconds.
//
// Caveat that shapes how this is used: `y` can only be pushed to the SM by way of the OSR
// (put -> pull -> out y), and the OSR simultaneously holds clk_div for the four
// `mov x, OSR` chunk reads. A Y update on a running SM therefore leaves a window where a
// chunk can latch the pulse width as its ramp count. Callers must only push Y while the SM
// is stopped, which in practice means at note-on.
static inline PioPeriod pio_period_split(uint32_t total_cycles,
                                         uint32_t weight,
                                         uint32_t overhead) {
  PioPeriod p;

  // Guard the subtraction: very high frequencies can leave no room for a ramp.
  uint32_t fixed = overhead + pioPulseLength;
  if (total_cycles <= fixed) {
    p.clk_div = 0;
    p.y = pioPulseLength;
    return p;
  }

  uint32_t ramp = total_cycles - fixed;
  p.clk_div = ramp / weight;
  p.y = pioPulseLength + (ramp % weight);
  return p;
}

// clk_div for an oscillator whose Y is already loaded and must not be disturbed. Rounded
// rather than exact, so the period error stays within +/- weight/2 cycles. Used every
// control frame; pio_period_split() takes over at note-on, where Y can be rewritten and
// the period becomes exact.
static inline uint32_t pio_clk_div_for_y(uint32_t total_cycles,
                                         uint32_t y,
                                         uint32_t weight,
                                         uint32_t overhead) {
  uint32_t fixed = overhead + y;
  if (total_cycles <= fixed) {
    return 0;
  }
  uint32_t ramp = total_cycles - fixed;
  return (ramp + weight / 2u) / weight;
}

uint8_t dataArray[4];

float LFOMultiplier = 1;
float voiceFreq[NUM_OSCILLATORS];
uint16_t dato_serial;
float dato_serial_float;
uint8_t OSC1_interval = 24;
uint8_t OSC2_serial_detune = 127;
uint8_t OSC2_interval = 36;
uint8_t OSC3_interval = 36;
float OSC2_detune = 127;
uint16_t OSC2DetuneVal = 256;
uint16_t OSC3DetuneVal = 256;

bool PWMPotsControlManual;

uint16_t PW[NUM_VOICES_TOTAL];

void serial_panel_task();
float get_chan_level(float freq_to_amp_comp);

volatile uint8_t note_on_flag[NUM_VOICES_TOTAL];

bool ledstat = false;

#endif
