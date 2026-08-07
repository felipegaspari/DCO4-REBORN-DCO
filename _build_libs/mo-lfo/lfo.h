//----------------------------------//
// LFO class for Arduino
// by mo-thunderz
// modified by felipegaspari
// version 1.2
//----------------------------------//

#include "Arduino.h"
#include <stdint.h>

// -------------------------------------------------
// Configuration macros
// -------------------------------------------------
// Sine lookup table resolution: LFO_SINE_TABLE_BITS = log2(table_size)
//   e.g. 8 -> 256, 9 -> 512 (default), 10 -> 1024
// Override with #define before including this header.
#ifndef LFO_SINE_TABLE_BITS
#define LFO_SINE_TABLE_BITS 9
#endif

#define LFO_SINE_TABLE_SIZE (1u << LFO_SINE_TABLE_BITS)
#define LFO_SINE_FRAC_BITS  (16 - LFO_SINE_TABLE_BITS)

// Math / output backend:
//   0 = legacy unipolar DAC integer output (default, examples)
//   1 = bipolar Q15 hot path (±32767 ≈ ±1.0), for synth engines
#ifndef MO_LFO_USE_Q15
#define MO_LFO_USE_Q15 0
#endif

#ifndef MO_LFO_CONFIG_REPORTED
#define MO_LFO_CONFIG_REPORTED
#if MO_LFO_USE_Q15
#pragma message("MO-LFO: math=Q15 bipolar (MO_LFO_USE_Q15=1)")
#else
#pragma message("MO-LFO: math=DAC unipolar int (MO_LFO_USE_Q15=0)")
#endif
#endif

#ifndef mo_lfo_h
#define mo_lfo_h

// Q15 full scale in int16_t (±32767 ≈ ±1.0)
static const int16_t MO_LFO_Q15_ONE = 32767;

class lfo
{
    public:
        // constructor
        // dacSize: vertical range for DAC mode; in Q15 mode used only when bridging setAmpl(dac counts)
        lfo(int dacSize);

        void setAmpl(int l_ampl);                                   // DAC counts 0..dacSize-1 (Q15 mode: converts once to _ampl_q15)
#if MO_LFO_USE_Q15
        void setAmplQ15(int16_t l_ampl_q15);                        // direct Q15 amplitude 0..32767
        int16_t getAmplQ15() const { return _ampl_q15; }
#endif
        void setAmplOffset(int l_ampl_offset);                      // DAC mode only; no-op in Q15 mode
        void setWaveForm(int l_waveForm);                           // 0 -> off, 1 -> saw, 2 -> triangle, 3 -> sin, 4 -> square [0,4]
        void setMode(bool l_freq_sync);                             // false -> free running, true -> BPM locked
        void setMode0Freq(float l_mode0_freq);                      // free-running frequency in Hz
        void setMode0Freq(float l_mode0_freq, unsigned long l_t);   // compatibility overload (timestamp ignored)
        void setMode1Bpm(float l_mode1_bpm);                        // BPM for sync mode
        void setMode1Rate(float l_mode1_rate);                      // LFO cycles per quarter note (see table below)
        void setMode1Phase(float l_mode1_phase_offset);             // reserved, currently no effect
        void sync(unsigned long l_t);                               // hard reset phase at timestamp t (use micros())

        int getWaveForm();
        int getAmpl();
        int getAmplOffset();
        bool getMode();
        float getMode0Freq();
        float getMode1Rate();
        float getPhase();                                           // normalized phase [0,1)

#if MO_LFO_USE_Q15
        // Bipolar Q15 wave (±32767 ≈ ±1.0), amplitude-scaled by setAmpl / setAmplQ15.
        int16_t getWave(unsigned long l_t);
        // Drop-in alias for synth code that already calls getWaveQ15().
        int16_t getWaveQ15(unsigned long l_t) { return getWave(l_t); }
        // Octave-fraction Q24 product: (wave_q15 * depth_q24) >> 15.
        static inline int32_t applyDepthQ24(int16_t wave_q15, int32_t depth_q24)
        {
            return (int32_t)(((int64_t)wave_q15 * (int64_t)depth_q24) >> 15);
        }
#else
        int getWave(unsigned long l_t);                             // unipolar [0, dacSize-1]
#endif

    private:
        int             _dacSize;
        int             _waveForm = 1;                      // 0..4
        int             _ampl = 0;                          // DAC-count amplitude (for getAmpl / setAmpl bridge)
        int             _ampl_offset = 0;                   // DAC mode offset; unused in Q15 hot path
#if MO_LFO_USE_Q15
        int16_t         _ampl_q15 = MO_LFO_Q15_ONE;         // cached Q15 amplitude
#endif
        bool            _mode = 0;                          // false -> free, true -> synced
        float           _mode0_freq = 30;
        float           _mode1_bpm = 120;
        float           _mode1_rate = 1;

        uint32_t        _phase = 0;
        uint32_t        _phase_inc_free = 0;
        uint32_t        _phase_inc_sync = 0;
        unsigned long   _t_last = 0;
        bool            _initialized = false;

        void            _updatePhaseIncFree();
        void            _updatePhaseIncSync();
#if MO_LFO_USE_Q15
        void            _updateAmplQ15FromDac();
#endif
};

#endif

// -----------------------------------------------------
// setMode1Rate table (mode 1 only):
//
//l_mode1_rate | lfo cycle duration
//---------------------------------
//         .125|   2 bars
//         .25 |   1 bar
//         .5  |   half note
//         1   |   quarter note
//         2   |   1/8 note
//         3   |   1/12 note
//         4   |   1/16 note
//         5   |   1/20 note
//         6   |   1/24 note
//         7   |   1/28 note
//         8   |   1/32 note
//         9   |   1/36 note
//        10   |   1/40 note
//        11   |   1/44 note
//        12   |   1/48 note
//        13   |   1/52 note
//        14   |   1/56 note
//        15   |   1/60 note
//        16   |   1/64 note
//
// Big THANKS to othmar52 for providing the table :-)
