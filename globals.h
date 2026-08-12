#include <cstddef>
#include "include_all.h"

#ifndef __GLOBALS_H__
#define __GLOBALS_H__



// Voice / oscillator counts (4 MIDI voices × 2 analog DCOs).
//   NUM_OSCILLATORS     — physical DCOs (RANGE/RESET/PIO, amp-comp, drift).
//   NUM_VOICES_TOTAL    — voice-slot capacity (MIDI/ADSR/flags/PW).
//   NUM_VOICES / STACK_VOICES (runtime below) — from setVoiceMode():
//     0 mono:  one MIDI voice → osc pair 0/1
//     1 poly:  4 independent 2-osc voices
//     2 stack: all 4 voices, same note
#define NUM_VOICES_TOTAL 4
#define NUM_OSCILLATORS 8
#define NUM_PW_CHANNELS NUM_VOICES_TOTAL
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

// Arduino Tools → CPU Speed sets clk_sys (and F_CPU) before setup()/setup1().
// Cache actual Hz: clock_get_hz is not free — do not put it on the voice hot path.
// Call sys_clock_hz_refresh() once per core at boot before PIO/clkdiv runs.
uint32_t sysClock_Hz_cached = F_CPU;
static inline void sys_clock_hz_refresh(void) {
  sysClock_Hz_cached = clock_get_hz(clk_sys);
}
#define sysClock_Hz (sysClock_Hz_cached)
#define sysClock    (sysClock_Hz_cached / 1000u)

static constexpr uint16_t DIV_COUNTER = 14000;
static constexpr uint16_t DIV_COUNTER_PW = 1024;

// Reset pulse width in system clock cycles (Y). Runtime-settable via
// PARAM_DEBUG_COMMAND 160 with value in [200, 50000] (dco_control Calibration).
uint32_t pioPulseLength = 3200;

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
// Soft-sync poll variants spend 2 cycles per iteration in each trailing polled
// chunk (jmp pin + jmp x--), so each such chunk counts twice toward the weight.
// Index by softSyncChunks (0 = free/hard). Aliases keep the N=1 names readable.
static constexpr uint32_t PIO_RAMP_WEIGHT_BY_CHUNKS[4] = { 4, 5, 6, 7 };
static constexpr uint32_t PIO_PERIOD_OVERHEAD_BY_CHUNKS[4] = { 12, 13, 14, 15 };
static constexpr uint32_t PIO_RAMP_WEIGHT_FREE = PIO_RAMP_WEIGHT_BY_CHUNKS[0];
static constexpr uint32_t PIO_PERIOD_OVERHEAD_FREE = PIO_PERIOD_OVERHEAD_BY_CHUNKS[0];
static constexpr uint32_t PIO_RAMP_WEIGHT_SYNC = PIO_RAMP_WEIGHT_BY_CHUNKS[1];
static constexpr uint32_t PIO_PERIOD_OVERHEAD_SYNC = PIO_PERIOD_OVERHEAD_BY_CHUNKS[1];

#define halfSysClock_Hz (sysClock_Hz / 2u)
#define eightSysClock_Hz_u (sysClock_Hz / 8u)
#define eightSysClockMinusPulseLength_Hz_u ((sysClock_Hz - pioPulseLength - 8u) / 8u)
#define T_HIGH_TOTAL_CYCLES (pioPulseLength + T_HIGH_OVERHEAD_CYCLES)
// Q24-scaled clock constants to avoid per-loop shifts
// (removed) Q24-scaled clock constants; direct shift used at call-site


uint32_t loop0_micros;
uint32_t loop1_micros;
uint32_t loop0_microsLast;
uint32_t loop1_microsLast;

volatile uint8_t NUM_VOICES = 1;
volatile uint8_t STACK_VOICES = 1;

volatile uint8_t voiceMode = 1;
uint8_t syncMode = 0;
volatile uint8_t oscSync = 0;

// Voice allocation policy (PARAM_VOICE_ALLOC_MODE) lives on the voiceAlloc
// instance in voice_alloc_state.h; VoiceAllocMode comes from the shared library.

volatile uint16_t phaseAlignOSC2 = 0;
// (removed) phaseAlignScale_Q16; use direct computation at call-site

uint8_t unisonDetune = 0;
uint8_t analogDrift = 0;
uint8_t analogDriftSpeed = 0;
uint8_t analogDriftSpread = 0;

// Character / imperfection knobs (dco_control Character tab).
// DSP: character_jitter.h — pitch every voice_task; amp/PW ~10 kHz.
// Hot path uses precomputed char_*_scale_q15 (recomputed on knob change).
uint8_t character = 0;                    // PARAM_CHARACTER 0..128 (master scale)
uint8_t ampCompJitter = 0;                // diag 0..128 (PARAM_DEBUG_COMMAND 0xC8xx)
uint8_t pitchJitter = 0;                  // diag 0..128 (PARAM_DEBUG_COMMAND 0xCAxx)
uint8_t pulsewidthJitter = 0;             // diag 0..128 (PARAM_DEBUG_COMMAND 0xCBxx)
// noise_q15 * scale >> 15 → ±peak; 0 when Character / axis off.
int32_t char_pitch_scale_q15 = 0;
int32_t char_amp_scale_q15 = 0;
int32_t char_pw_scale_q15 = 0;

float BASE_NOTE = 440.0f;


// WEACT RP2040 (legacy 8-osc map — kept for reference):
// static constexpr uint8_t RESET_PINS[8] = { 29, 27, 19, 18, 15, 13, 12, 8 };
// static constexpr uint8_t RANGE_PINS[8] = { 28, 22, 17, 16, 14, 11, 9, 7 };

// Raspberry Pi Pico (legacy):
// static constexpr uint8_t RESET_PINS[8] = { 28, 26, 19, 18, 15, 13, 12, 8 };
// static constexpr uint8_t RANGE_PINS[8] = { 27, 22, 17, 16, 14, 11,  9,  7 };

// Live WEACT 8-osc RESET/RANGE/PW/cal (old DCO4). CV/mux in PINOUT.md is DCO3 leftover — not live.
// GPIO 24 is board fix-rail (see DCO.ino), not a DCO output.
//
// Hub UART:
//   Input UART  GP20 TX / GP21 RX
//   DIN MIDI    GP0/GP1 (HW UART0 interim; PIO UART later)
// SUB ×4 GPIOs TBD (`SUBOSC_PINS[]` still 0xFF). GP8 is OSC8 RESET — not sub-osc.
//
// CV PWM wrap is 4095 except Reso1 (GP7): shares slice 3 with RANGE OSC2 (GP22),
// so that channel scales duty into DIV_COUNTER (see write_cv_pwm).
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

static constexpr uint8_t RESET_PINS[NUM_OSCILLATORS] = { 29, 27, 19, 18, 15, 13, 12, 8 };
static constexpr uint8_t RANGE_PINS[NUM_OSCILLATORS] = { 28, 22, 17, 16, 14, 11, 9, 7 };

// Freq SMs: voices 0–1 on pio0 (osc 0–3), voices 2–3 on pio1 (osc 4–7).
// A voice pair always shares a PIO block so hard-sync sideset can share RESET.
static constexpr uint8_t VOICE_TO_PIO[NUM_OSCILLATORS] = { 0, 0, 0, 0, 1, 1, 1, 1 };

// Mutable local SM index within each PIO block. Slave always takes the lower SM
// so the master outranks it on a same-cycle sideset tie. Rewritten by
// assign_sm_mapping() whenever syncMode changes.
uint8_t VOICE_TO_SM[NUM_OSCILLATORS] = { 0, 1, 2, 3, 0, 1, 2, 3 };

// Noise PIO LFSR is not used (CPU DCO_Noise only). Constants kept for dcoNoisePioBegin no-op.
static constexpr uint8_t NOISE_PIO = 1;
static constexpr uint8_t NOISE_SM = 1;
static constexpr uint8_t NOISE_OUT_PIN = 2;

// Pulse-width PWM: one pin per MIDI voice (old DCO4). 0xFF = not wired.
static constexpr uint8_t PW_PIN_UNASSIGNED = 0xFF;
static constexpr uint8_t PW_PINS[NUM_PW_CHANNELS] = { 3, 2, 4, 5 };

// RP2350: one sub-osc SM per voice on pio2. Pinout later (0xFF = skip gpio init).
static constexpr uint8_t SUBOSC_PIN_UNASSIGNED = 0xFF;
#if defined(PICO_RP2350)
static constexpr uint8_t SUBOSC_PIO = 2;
static constexpr uint8_t SUBOSC_PINS[NUM_VOICES_TOTAL] = {
  SUBOSC_PIN_UNASSIGNED, SUBOSC_PIN_UNASSIGNED, SUBOSC_PIN_UNASSIGNED, SUBOSC_PIN_UNASSIGNED
};
#endif

// Old DCO4 cal sense. GP6 is free (later SUB candidate). GP25 is Pico LED — not cal.
static constexpr int DCO_calibration_pin = 10;

uint8_t RANGE_PWM_SLICES[NUM_OSCILLATORS];
uint8_t RANGE_PWM_CHANNELS[NUM_OSCILLATORS];
uint8_t VCO_PWM_SLICES[NUM_OSCILLATORS];
uint8_t PW_PWM_SLICES[NUM_PW_CHANNELS];
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

uint16_t PW_CENTER[NUM_PW_CHANNELS] = { 570, 552, 540, 553 };
uint16_t PW_LOW_LIMIT[NUM_PW_CHANNELS] = { 0, 0, 0, 0 };
uint16_t PW_HIGH_LIMIT[NUM_PW_CHANNELS] = {
  DIV_COUNTER_PW, DIV_COUNTER_PW, DIV_COUNTER_PW, DIV_COUNTER_PW
};
uint16_t PW_LOOKUP[3] = { 0, (DIV_COUNTER_PW / 2) - 1, DIV_COUNTER_PW - 1 };
uint16_t PW_PWM[NUM_PW_CHANNELS];

// Gate flag: 1 while the key is down, 0 from note-off onwards. The allocator in
// voice_alloc_state.h carries the finer distinction it needs on top of this (a
// released voice is still audible); voice_mark_on/off keep the two in step.
volatile uint32_t VOICES[NUM_VOICES_TOTAL];
volatile uint8_t VOICE_NOTES[NUM_VOICES_TOTAL];

uint32_t LED_BLINK_START = 0;

#if defined(PICO_RP2350)
PIO pio[3] = { pio0, pio1, pio2 };
#else
PIO pio[2] = { pio0, pio1 };
#endif

uint8_t midi_serial_status = 0;
int midi_pitch_bend = 8192, last_midi_pitch_bend = 8192;
uint8_t midi_aftertouch = 0;
uint8_t midi_mod_wheel = 0;
uint8_t pitchBendRange = 2;
// MIDI Bank Select (CC 0 / CC 32): 0 = slots 0..127, 1 = slots 128..255.
uint8_t midiPresetBank = 0;

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
uint8_t voice_alloc();
void voice_mark_on(uint8_t voice, uint8_t note, uint8_t velocity);
void voice_mark_off(uint8_t voice);
void usb_midi_task();
void serial_midi_task();
void note_on(uint8_t note, uint8_t velocity);
void note_off(uint8_t note);
void voice_task_fixed_point();
void voice_task_float();
void voice_task_main();
void adc_task();


// Free-running program + one soft-sync poll image per freq PIO block (pio0 and pio1).
uint32_t pio_offset_free[2] = { 0, 0 };
uint32_t pio_offset_sync[2] = { 0, 0 };
uint8_t pio_loaded_sync_chunks[2] = { 0, 0 };  // 0 = none; else 1..3
uint32_t subosc_offset_div2 = 0;
uint32_t subosc_offset_div4 = 0;

// Per-oscillator PIO state: which resident program the SM runs, and the reset
// pulse width (Y) last pushed to it. Y is only rewritten at note-on — see
// pio_period_split() for why it cannot safely be updated every control frame.
bool osc_uses_sync_program[NUM_OSCILLATORS] = {
  false, false, false, false, false, false, false, false
};
uint32_t osc_last_y[NUM_OSCILLATORS] = {
  pioPulseLength, pioPulseLength, pioPulseLength, pioPulseLength,
  pioPulseLength, pioPulseLength, pioPulseLength, pioPulseLength
};

// Last clk_div handed to each SM. Writing Y consumes the OSR, so clk_div always has to
// be re-pushed afterwards; without a remembered value the SM would read a shifted-out
// OSR as its ramp count and shriek for one control frame. 200 is the slow "park" rate
// used elsewhere during calibration.
uint32_t osc_last_clk_div[NUM_OSCILLATORS] = { 200, 200, 200, 200, 200, 200, 200, 200 };

// Soft sync: number of trailing ramp chunks that poll the master's reset pin.
// 0 = hard sync through the sideset pin (no resolution cost).
// 1..3 = soft sync with that many trailing polled chunks (receptive ~40% / ~67% / ~86%).
// Only one poll program image is resident at a time; changing 1..3 reloads it.
uint8_t softSyncChunks = 0;

// Note-on OSC1/OSC2 restart when oscSync >= 1 (A/B listen + profiler).
// 0 EXACT_Y: disable + period_split + load Y + jmp + enable_in_sync (default).
// 1 SYNC_JMP: jmp only on running SMs (keep last Y; no disable / enable_in_sync).
#ifndef NOTE_RETRIG_MODE_DEFAULT
#define NOTE_RETRIG_MODE_DEFAULT 0
#endif
enum NoteRetrigMode : uint8_t {
  NOTE_RETRIG_EXACT_Y  = 0,
  NOTE_RETRIG_SYNC_JMP = 1,
};
volatile uint8_t note_retrig_mode = (uint8_t)NOTE_RETRIG_MODE_DEFAULT;
volatile bool note_retrig_mode_ack_pending = false;

static inline const char *note_retrig_mode_name(uint8_t m) {
  return (m == NOTE_RETRIG_SYNC_JMP) ? "SYNC_JMP" : "EXACT_Y";
}

static inline void note_retrig_set_mode(uint8_t m) {
  if (m > NOTE_RETRIG_SYNC_JMP) m = NOTE_RETRIG_EXACT_Y;
  note_retrig_mode = m;
  __dmb();
}

// Sub-oscillator divide ratio: 0 = off, 2 = one octave down, 4 = two octaves.
uint8_t subOscDivide = 0;

// Program-relative addresses.
//   RESTART: `mov x, y`, the top of the reset pulse. Jumping here retriggers a cycle.
//   PHASE_HOLD: last `jmp x--` before `mov x, y` / `set pins, 1` (loop_final). Preload X and
//     jump here for a one-shot delay until OSC2's first flyback. On the old 8-chunk
//     `frequency` program this was hardcoded jmp 10; on frequency_sync_4_jumps it is 9.
//   RAMP_ENTRY[q]: leftover 90° chunk entries (unused by live phase-align).
// Free and poll-1 share entry addresses {0,4,6,8}; poll-2/3 shift later entries because
// polled chunks insert an extra instruction before each countdown.
static constexpr uint8_t PIO_RESTART_ADDR_FREE = 10;
static constexpr uint8_t PIO_RESTART_ADDR_SYNC[4] = { 10, 11, 12, 13 };  // index = softSyncChunks
static constexpr uint8_t PIO_PHASE_HOLD_ADDR_FREE = 9;
static constexpr uint8_t PIO_PHASE_HOLD_ADDR_SYNC[4] = { 9, 10, 11, 12 };  // index = softSyncChunks
static constexpr uint8_t PIO_RAMP_ENTRY_FREE[4] = { 0, 4, 6, 8 };
static constexpr uint8_t PIO_RAMP_ENTRY_SYNC_1[4] = { 0, 4, 6, 8 };
static constexpr uint8_t PIO_RAMP_ENTRY_SYNC_2[4] = { 0, 4, 6, 9 };
static constexpr uint8_t PIO_RAMP_ENTRY_SYNC_3[4] = { 0, 4, 7, 10 };

static inline uint8_t soft_sync_chunks_clamped() {
  uint8_t n = softSyncChunks;
  if (n > 3) n = 3;
  return n;
}

static inline uint32_t osc_program_base(uint8_t osc) {
  const uint8_t blk = VOICE_TO_PIO[osc];
  return osc_uses_sync_program[osc] ? pio_offset_sync[blk] : pio_offset_free[blk];
}

static inline uint32_t osc_restart_target(uint8_t osc) {
  const uint8_t blk = VOICE_TO_PIO[osc];
  if (!osc_uses_sync_program[osc]) {
    return pio_offset_free[blk] + PIO_RESTART_ADDR_FREE;
  }
  return pio_offset_sync[blk] + PIO_RESTART_ADDR_SYNC[soft_sync_chunks_clamped()];
}

static inline uint32_t osc_phase_hold_target(uint8_t osc) {
  const uint8_t blk = VOICE_TO_PIO[osc];
  if (!osc_uses_sync_program[osc]) {
    return pio_offset_free[blk] + PIO_PHASE_HOLD_ADDR_FREE;
  }
  return pio_offset_sync[blk] + PIO_PHASE_HOLD_ADDR_SYNC[soft_sync_chunks_clamped()];
}

// X preload for osc_phase_align_hold_stopped so the first flyback lands at
// remaining ≈ total * (360 - deg) / 360. 0 → caller should jmp restart (0°).
// RECIP_360_Q24 mul/shift; −3 is loop_final fallthrough + mov x,y + set pins,1.
static inline uint32_t osc_phase_hold_x(uint32_t total_cycles, uint16_t deg) {
  if (deg >= 360u) deg = (uint16_t)(deg % 360u);
  if (deg == 0) return 0;
  uint32_t per_deg = (uint32_t)(((uint64_t)total_cycles * RECIP_360_Q24 + (1u << 23)) >> 24);
  uint32_t remaining = per_deg * (uint32_t)(360u - deg);
  return (remaining > 3u) ? remaining - 3u : 0u;
}

static inline uint32_t osc_ramp_entry_target(uint8_t osc, uint8_t quarters) {
  quarters &= 3;
  const uint8_t blk = VOICE_TO_PIO[osc];
  if (!osc_uses_sync_program[osc]) {
    return pio_offset_free[blk] + PIO_RAMP_ENTRY_FREE[quarters];
  }
  switch (soft_sync_chunks_clamped()) {
    case 2:  return pio_offset_sync[blk] + PIO_RAMP_ENTRY_SYNC_2[quarters];
    case 3:  return pio_offset_sync[blk] + PIO_RAMP_ENTRY_SYNC_3[quarters];
    default: return pio_offset_sync[blk] + PIO_RAMP_ENTRY_SYNC_1[quarters];
  }
}

// Period model of whichever program this oscillator is running.
static inline uint32_t osc_ramp_weight(uint8_t osc) {
  if (!osc_uses_sync_program[osc]) return PIO_RAMP_WEIGHT_FREE;
  return PIO_RAMP_WEIGHT_BY_CHUNKS[soft_sync_chunks_clamped()];
}

static inline uint32_t osc_period_overhead(uint8_t osc) {
  if (!osc_uses_sync_program[osc]) return PIO_PERIOD_OVERHEAD_FREE;
  return PIO_PERIOD_OVERHEAD_BY_CHUNKS[soft_sync_chunks_clamped()];
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
// Global octave offset for note math: table_index = midi - 36 + octave_shift (36 ⇒ unison).
// Wire param remains PARAM_OSC1_INTERVAL (13) / CC 2.
volatile uint8_t octave_shift = 24;
uint8_t OSC2_serial_detune = 127;
volatile uint8_t OSC2_interval = 36;
volatile uint8_t OSC3_interval = 36;
float OSC2_detune = 127;
uint16_t OSC2DetuneVal = 256;
uint16_t OSC3DetuneVal = 256;

bool PWMPotsControlManual;

uint16_t PW[NUM_PW_CHANNELS];  // panel / mod PW target per voice

void serial_panel_task();
float get_chan_level(float freq_to_amp_comp);

volatile uint8_t note_on_flag[NUM_VOICES_TOTAL];

bool ledstat = false;

#endif
