// #include <stdint.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <math.h>

/*  *** TO DO ***
- Fix PULSE PWN not received or updated when loading patches.
- Ask the AI to optimize and clean the autotune code. 
*/

// =============================================================================
// ENGINE — pitch mode ids (needed by board defaults + overrides below)
// =============================================================================
// Deep detail: docs/ENGINE_OPTIONS.md
//   0 FLOAT (natural modifier→ratio; needs float voice)
//   1 RATIO_Q16 (slopeQ20 + fused y→ratio; fixed default / float A/B)
//   2 Q12 (slope A/B: IntQ16 y + reciprocal; float A/B OK)
#define PITCH_INTERP_FLOAT       0
#define PITCH_INTERP_RATIO_Q16   1
#define PITCH_INTERP_Q12         2

// =============================================================================
// ENGINE — board defaults (Arduino core: PICO_RP2350 / else)
// =============================================================================
#if defined(PICO_RP2350)
  // RP2350 has an FPU: float voice + float amp-comp dual-build (LUT + Q8 for A/B).
  #ifndef USE_FLOAT_VOICE_TASK
    #define USE_FLOAT_VOICE_TASK
  #endif
  #ifndef PITCH_INTERP_MODE
    #define PITCH_INTERP_MODE PITCH_INTERP_FLOAT
  #endif
  #ifndef USE_FLOAT_AMP_COMP
    #define USE_FLOAT_AMP_COMP
  #endif
  #ifndef AMP_COMP_METHOD_DEFAULT
    #define AMP_COMP_METHOD_DEFAULT 0   // FLOAT_QUAD (0); LUT=1, FIXED=2 — cmds 20–22
  #endif
  #ifndef HIGH_PRECISION_CLKDIV
    #define HIGH_PRECISION_CLKDIV 1     // fixed-voice clkdiv only; ignored by float voice
  #endif
#else
  // RP2040 / fallback: fixed voice + lean Q8 amp (no float amp tables / LUT RAM).
  #ifndef AMP_COMP_METHOD_DEFAULT
    #define AMP_COMP_METHOD_DEFAULT 2   // FIXED
  #endif
  #ifndef PITCH_INTERP_MODE
    #define PITCH_INTERP_MODE PITCH_INTERP_RATIO_Q16
  #endif
  #ifndef HIGH_PRECISION_CLKDIV
    #define HIGH_PRECISION_CLKDIV 1     // 1 = ~4µs/voice 64-bit div; 0 = ~1µs fast Q-format
  #endif
#endif

// =============================================================================
// ENGINE — overrides (uncomment to force; after board defaults)
// =============================================================================
// #undef USE_FLOAT_VOICE_TASK          // fixed voice_task on RP2350
// #define USE_FLOAT_VOICE_TASK         // float voice on RP2040 (soft-float; slow)
// #undef USE_FLOAT_AMP_COMP            // lean Q8 amp only (no float Hz tables / LUT)
 #define USE_FLOAT_AMP_COMP           // float amp dual-build on RP2040 (large RAM)
// #define HIGH_PRECISION_CLKDIV 0      // fast fixed clkdiv; ignored if float voice
// #define AMP_COMP_METHOD_DEFAULT 1    // 0 FLOAT_QUAD / 1 LUT / 2 FIXED; needs USE_FLOAT_AMP_COMP for 0/1
// Pitch A/B (ids above; default already set — #undef then redefine):
// #undef PITCH_INTERP_MODE
// #define PITCH_INTERP_MODE PITCH_INTERP_RATIO_Q16

// =============================================================================
// ENGINE — guards
// =============================================================================
#if PITCH_INTERP_MODE == PITCH_INTERP_FLOAT && !defined(USE_FLOAT_VOICE_TASK)
  #error "PITCH_INTERP_FLOAT requires USE_FLOAT_VOICE_TASK (board default or override)"
#endif

// =============================================================================
// PROFILING / BENCH (see docs/BENCHMARKING.md)
// =============================================================================
// RUNNING_AVERAGE: hot-path profiler in bench.h (count/mean/min/max/total + core share).
// Off = zero cost. Needed for paced bench_out_* TX (profiler dump, amp/pitch benches).
// RUNNING_AVERAGE_FINE: also probes tiny stages; every probe is an opt barrier — changes
// codegen; for measuring that distortion, not for leaving on.
// RUNNING_AVERAGE_PERIOD: only loop/loop1 BENCH_PERIOD; stage probes compile out.
// Overrides FINE. Needs RUNNING_AVERAGE.
#define RUNNING_AVERAGE
// #define RUNNING_AVERAGE_FINE
 #define RUNNING_AVERAGE_PERIOD

// Float vs double clkdiv comparison in voice_task_float; needs RUNNING_AVERAGE.
// #define CLKDIV_BENCHMARK

// Amp-comp speed/accuracy reports (debug cmds 24–25); needs RUNNING_AVERAGE + USE_FLOAT_AMP_COMP.
#define AMP_COMP_BENCHMARK

// =============================================================================
// BOARD / IO
// =============================================================================
// Serial hub: Serial2 GP20/21 is the only peer link (Input panel protocol + 'x'
// gap/cal TX). Screen is reached by Input relaying gap 154 on its Screen port.

// Accept Input panel protocol on USB CDC too (tools/dco_control). Comment out for
// production: stray terminal bytes are read as frame headers while enabled.
#define ENABLE_USB_CONTROL

// Phase 3 CV hardware (provisional pins in globals.h / docs/PINOUT.md).
// Leave commented on benches without filter/VCA/mux/DAC attached.
// #define ENABLE_CV_OUTS
// #define ENABLE_WAVE_MUX

// Dual-MCU: RP2040 voice-aux owns Dist Drive/Mix PWM + filter mode GPIO (later FX).
// Keep apply handlers/state; skip local pin writers so they do not fight the aux.
// Leave commented for solo RP2350B / single-MCU (full local IO). See docs/DUAL_MCU.md.
// #define ENABLE_VOICE_AUX

// Note-on sync retrigger (oscSync >= 1): 0 = EXACT_Y, 1 = SYNC_JMP. Runtime: cmds 26/27.
#ifndef NOTE_RETRIG_MODE_DEFAULT
  #define NOTE_RETRIG_MODE_DEFAULT 0
#endif

#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
//#include "tusb_config.h"

#include "pico/stdlib.h"
// #include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico-dco.pio.h"
#include "hardware/pwm.h"
// #include "hardware/spi.h"

#include "LittleFS.h"
// #include <SingleFileDrive.h>
// #include <EEPROM.h>

#include <stdint.h>
#include "params_def.h"
#include "param_router.h"

#include "globals.h"
#include "amp_comp.h"
#include "bench.h"
#include "cv_state.h"
#include "cv_out.h"

#include "FS.h"

#include "noteList.h"

#include "Serial.h"
#include "midi.h"
#include "voices.h"
#include "state_machines.h"
#include "PWM.h"
#include "utils.h"
#include "Timer_millis.h"

#include "LFO.h"
#include "adsr.h"
#include "midi_cc.h"
#include "midi_cc_map.h"  // generated; defines midiCcMap[], so include it once, here
#include "wave_mux.h"

#include "PID.h"
#include "autotune.h"


// ****************************************************************************************** //

// Core 0 boot: USB, UART serial, MIDI handlers, LFOs, board fix pins, calibration input pin.
void setup() {
  //set_sys_clock_khz(sysClock, true);
  // EEPROM.begin(512);
  bench_init_core();  // SysTick is per core; core 1 arms its own in setup1()
  init_usb();
  init_serial();
  init_midi();

  init_LFOs();
  init_DRIFT_LFOs();

  
  // init_tuner();
  // init_tuning_tables();

  pinMode(23, OUTPUT);
  digitalWrite(23, HIGH);

  pinMode(24, OUTPUT);  // Fix pin on DCO BOARD
  digitalWrite(24, HIGH);

  pinMode(DCO_calibration_pin, INPUT_PULLUP);

  // gpio_init(11);
  // gpio_set_dir(11, GPIO_IN);
  // gpio_pull_down(11);
}

// Core 1 boot: PID, LittleFS cal load, ADSR, amp-comp precompute, PWM/PIO, voices.
// Clears calibrationFlag so the init_DCO_calibration block below is currently unreachable.
void setup1() {

  //set_sys_clock_khz(sysClock, true);

  bench_init_core();

  init_PID();

  init_FS();

  init_ADSR();
  init_cv_out();
  mod_matrix_init();
  init_waveSelector();

  // Select amplitude-compensation precompute based on engine type.
  precompute_amp_comp_for_engine();

  calibrationFlag = false;
  manualCalibrationFlag = false;
  firstTuneFlag = true;

  init_pwm();
  init_pio();
  init_voices();

  if (calibrationFlag == true) {
    init_DCO_calibration();
    voice_task_autotune(0, ampCompCalibrationVal);
  }
}

// Core 0 forever loop: MIDI read, Serial2 parser, LFO1; ~100 µs LFO2 + drift + FIFO push of detune.
void loop() {
  BENCH_PERIOD(loop0_period);
  loop0_micros = micros();

  {
    BENCH_BEGIN(loop0_midi);
    MIDI_USB.read();
    MIDI_SERIAL.read();
    BENCH_END(loop0_midi);
  }

  {
    BENCH_BEGIN(loop0_serial);
    serial_panel_task();
#ifdef ENABLE_USB_CONTROL
    serial_usb_task();
#endif
    BENCH_END(loop0_serial);
  }

  {
    BENCH_BEGIN(loop0_lfo1);
    LFO1();
    BENCH_END(loop0_lfo1);
  }

  if ((loop0_micros - loop0_microsLast) > 100) {
    {
      BENCH_BEGIN(loop0_lfo2);
      LFO2();
      BENCH_END(loop0_lfo2);
    }

    {
      BENCH_BEGIN(loop0_drift);
      DRIFT_LFOs();
      BENCH_END(loop0_drift);
    }

    {
      // Transfer LFO1 detune modulation as a raw Q24 fixed-point integer via FIFO.
      BENCH_BEGIN(loop0_fifo_push);
      rp2040.fifo.push_nb((uint32_t)DETUNE_INTERNAL_q24);
      BENCH_END(loop0_fifo_push);
    }

    loop0_microsLast = loop0_micros;
  }

  // Snapshot core 0's probes and print once core 1 has handed its own over. All profiler
  // serial traffic happens here, never on the audio core.
  bench_poll_core0();
}

// Core 1 forever loop: soft timers; auto/manual calibration OR ADSR + FIFO pop + voice_task_main.
void loop1() {
  BENCH_PERIOD(loop1_period);

  {
    BENCH_BEGIN(loop1_millis);
    millisTimer();
    BENCH_END(loop1_millis);
  }

  if (calibrationFlag == true) {
    if (manualCalibrationFlag == true) {
      VOICE_NOTES[0]               = manual_DCO_calibration_start_note;
      DCO_calibration_current_note = manual_DCO_calibration_start_note;
      ampCompCalibrationVal = initManualAmpCompCalibrationValPreset + manualCalibrationOffset[manualCalibrationStage];
      voice_task_autotune(0, ampCompCalibrationVal);
      update_CV_outs_manual_calibration();
      // In manual calibration mode, continuously measure and report the duty
      // difference so the screen can display live feedback for the user.
      DCO_calibration_debug();
      //Serial.println((String) "PW value: " + (PW[0] / 4));

    } else {
      DCO_calibration();
    }
  } else {

    loop1_micros = micros();

    if ((loop1_micros - loop1_microsLast) > 100) {
      {
        BENCH_BEGIN(loop1_adsr);
        ADSR_update();
        BENCH_END(loop1_adsr);
      }
      {
        BENCH_BEGIN(loop1_cv_outs);
        update_CV_outs();
        BENCH_END(loop1_cv_outs);
      }
      loop1_microsLast = loop1_micros;
    }

    {
      // Receive Q24 detune value from core 0; reinterpret raw bits back to signed.
      BENCH_BEGIN(loop1_fifo_pop);
      rp2040.fifo.pop_nb(detune_fifo_variable);
      DETUNE_INTERNAL_FIFO_q24 = (int32_t)DETUNE_INTERNAL_FIFO;
      BENCH_END(loop1_fifo_pop);
    }

    {
      BENCH_BEGIN(voice_task);
      voice_task_main();
      BENCH_END(voice_task);
    }
  }

  // Hand this core's counters to core 0, which does all the printing.
  bench_service(1);
}