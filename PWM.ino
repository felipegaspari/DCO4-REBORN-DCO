#include "hardware/dma.h"

#ifdef RANGE0_PIO_DITHER_TEST





// osc0/1: pio1 SM2/SM3 (SM0=subosc, SM1=noise). osc2: pio0 SM3 (SM0–2=voices).
static const uint8_t RANGE_PIO_BLOCK[NUM_OSCILLATORS] = { 1, 1, 0 };
static const uint8_t RANGE_PIO_SM[NUM_OSCILLATORS] = { 2, 3, 3 };

static uint32_t range_pio_duty[NUM_OSCILLATORS][RANGE_PIO_FRAMES];
static uint32_t range_dma_src_addr[NUM_OSCILLATORS];
static int range_dma_data[NUM_OSCILLATORS] = { -1, -1, -1 };
static int range_dma_ctrl[NUM_OSCILLATORS] = { -1, -1, -1 };
static bool range_pio_ready = false;

PwmRoutePlan active_voice_routes[12];
uint8_t num_voice_routes = 0;

void range_pio_set_level(uint8_t osc, uint16_t level) {
  if (osc >= NUM_OSCILLATORS) {
    return;
  }
  if (level > DIV_COUNTER) {
    level = DIV_COUNTER;
  }
  const uint32_t t =
      ((uint32_t)level * RANGE_PIO_LEVELS + (DIV_COUNTER / 2u)) / DIV_COUNTER;
  const uint16_t base = (uint16_t)(t / RANGE_PIO_FRAMES);
  const uint16_t rem = (uint16_t)(t % RANGE_PIO_FRAMES);
  for (uint32_t i = 0; i < RANGE_PIO_FRAMES; i++) {
    const uint16_t d = (uint16_t)(base + (i < rem ? 1u : 0u));
    range_pio_duty[osc][i] = ((uint32_t)(RANGE_PIO_PERIOD - d) << 16) | d;
  }
}

static void range_pio_enable_pin(uint8_t osc) {
  PIO p = pio[RANGE_PIO_BLOCK[osc]];
  const uint sm = RANGE_PIO_SM[osc];
  pio_gpio_init(p, RANGE_PINS[osc]);
  pio_sm_set_consecutive_pindirs(p, sm, RANGE_PINS[osc], 1, true);
  pio_sm_set_enabled(p, sm, true);
}

void init_range_pio_dither() {
  if (range_pio_ready) {
    for (uint8_t osc = 0; osc < NUM_OSCILLATORS; osc++) {
      range_pio_enable_pin(osc);
    }
    return;
  }

  const uint offset1 = pio_add_program(pio[1], &range_pwm_dither_program);
  const uint offset0 = pio_add_program(pio[0], &range_pwm_dither_program);

  for (uint8_t osc = 0; osc < NUM_OSCILLATORS; osc++) {
    PIO p = pio[RANGE_PIO_BLOCK[osc]];
    const uint sm = RANGE_PIO_SM[osc];
    const uint offset = (RANGE_PIO_BLOCK[osc] == 1) ? offset1 : offset0;
    const uint pin = RANGE_PINS[osc];

    pio_sm_claim(p, sm);
    pio_gpio_init(p, pin);
    pio_sm_set_consecutive_pindirs(p, sm, pin, 1, true);

    pio_sm_config c = range_pwm_dither_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, pin);
    pio_sm_init(p, sm, offset, &c);

    range_pio_set_level(osc, 0);
    for (uint32_t i = 0; i < RANGE_PIO_FRAMES; i++) {
      pio_sm_put(p, sm, range_pio_duty[osc][i]);
    }

    range_dma_src_addr[osc] = (uint32_t)(uintptr_t)range_pio_duty[osc];
    range_dma_data[osc] = dma_claim_unused_channel(true);
    range_dma_ctrl[osc] = dma_claim_unused_channel(true);

    dma_channel_config data_c = dma_channel_get_default_config(range_dma_data[osc]);
    channel_config_set_transfer_data_size(&data_c, DMA_SIZE_32);
    channel_config_set_read_increment(&data_c, true);
    channel_config_set_write_increment(&data_c, false);
    channel_config_set_dreq(&data_c, pio_get_dreq(p, sm, true));
    channel_config_set_chain_to(&data_c, (uint)range_dma_ctrl[osc]);
    dma_channel_configure(range_dma_data[osc], &data_c, &p->txf[sm], range_pio_duty[osc],
                          RANGE_PIO_FRAMES, false);

    dma_channel_config ctrl_c = dma_channel_get_default_config(range_dma_ctrl[osc]);
    channel_config_set_transfer_data_size(&ctrl_c, DMA_SIZE_32);
    channel_config_set_read_increment(&ctrl_c, false);
    channel_config_set_write_increment(&ctrl_c, false);
    dma_channel_configure(range_dma_ctrl[osc], &ctrl_c,
                          &dma_hw->ch[range_dma_data[osc]].al3_read_addr_trig,
                          &range_dma_src_addr[osc], 1, false);

    dma_channel_start(range_dma_ctrl[osc]);
    pio_sm_set_enabled(p, sm, true);
  }
  range_pio_ready = true;
}
#endif  // RANGE0_PIO_DITHER_TEST

// 2D Array of DMA Ring Buffers. MUST be aligned to a 32-byte boundary for ring wrap!
// 12 slices max * 8 words = 384 bytes in SRAM.
static uint32_t dither_ring_buffers[12][PWM_DITHER_STEPS] __attribute__((aligned(32)));
static int dither_dma_chans[12];

void init_pwm() {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    gpio_set_function(RANGE_PINS[i], GPIO_FUNC_PWM);
    RANGE_PWM_SLICES[i] = pwm_gpio_to_slice_num(RANGE_PINS[i]);
    RANGE_PWM_CHANNELS[i] = pwm_gpio_to_channel(RANGE_PINS[i]);
    pwm_set_wrap(RANGE_PWM_SLICES[i], DIV_COUNTER >> PWM_DITHER_BITS);
    pwm_set_enabled(RANGE_PWM_SLICES[i], true);
  }

  for (int i = 0; i < NUM_PW_CHANNELS; i++) {
    if (PW_PINS[i] == PW_PIN_UNASSIGNED) {
      PW_PWM_SLICES[i] = 0xFF;
      continue;
    }
    gpio_set_function(PW_PINS[i], GPIO_FUNC_PWM);
    PW_PWM_SLICES[i] = pwm_gpio_to_slice_num(PW_PINS[i]);
    pwm_set_wrap(PW_PWM_SLICES[i], DIV_COUNTER_PW >> PWM_DITHER_BITS);
    pwm_set_enabled(PW_PWM_SLICES[i], true);
  }

  // =========================================================================
  // BIND DIRECT HARDWARE POINTERS & ALLOCATE DMA SAFELY
  // =========================================================================
  num_voice_routes = 0;

  for (int s = 0; s < 12; s++) {
    const uint16_t* p_a = &PWM_STATIC_ZERO;
    const uint16_t* p_b = &PWM_STATIC_ZERO;
    bool slice_used = false;

    for (int i = 0; i < NUM_OSCILLATORS; i++) {
      if (RANGE_PWM_SLICES[i] != 0xFF && RANGE_PWM_SLICES[i] == s) {
        if (RANGE_PWM_CHANNELS[i] == 0) p_a = &RANGE_PWM[i];
        else                            p_b = &RANGE_PWM[i];
        slice_used = true;
      }
    }

    for (int i = 0; i < NUM_PW_CHANNELS; i++) {
      if (PW_PWM_SLICES[i] != 0xFF && PW_PWM_SLICES[i] == s) {
        uint8_t chan = PW_PINS[i] & 1u;
        if (chan == 0) p_a = &PW_PWM[i];
        else           p_b = &PW_PWM[i];
        slice_used = true;
      }
    }

    if (slice_used) {
      active_voice_routes[num_voice_routes].slice_num = s;
      active_voice_routes[num_voice_routes].hw_cc = &pwm_hw->slice[s].cc;
      active_voice_routes[num_voice_routes].src_a = p_a;
      active_voice_routes[num_voice_routes].src_b = p_b;
      active_voice_routes[num_voice_routes].dma_buffer = dither_ring_buffers[num_voice_routes];

      // SAFE CLAIM: `false` prevents CPU panic. If it returns -1, the fallback logic takes over.
      int dma_chan = dma_claim_unused_channel(false);
      active_voice_routes[num_voice_routes].dma_chan = dma_chan;

      if (dma_chan >= 0) {
        dma_channel_config c = dma_channel_get_default_config(dma_chan);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        
        // Pace transfer perfectly to PWM cycle
        channel_config_set_dreq(&c, pwm_get_dreq(s));
        
        // Read Ring wrap: 2^5 = 32 Bytes = 8 words
        channel_config_set_ring(&c, false, 5);

        dma_channel_configure(
          dma_chan, 
          &c,
          &pwm_hw->slice[s].cc,                         
          active_voice_routes[num_voice_routes].dma_buffer, 
          0xFFFFFFFF, // Start with maximum possible transfer count
          true                                          
        );
      }

      num_voice_routes++;
    }
  }

#ifdef ENABLE_CV_OUTS
  init_cv_pwm();
#endif
}

#ifdef ENABLE_CV_OUTS

// Init cutoff / resonance / VCA PWM (12-bit CV domain). Reso1 shares slice with RANGE OSC2.
void init_cv_pwm() {
  for (int i = 0; i < NUM_FILTERS; i++) {
    gpio_set_function(CUTOFF_PINS[i], GPIO_FUNC_PWM);
    CUTOFF_PWM_SLICES[i] = pwm_gpio_to_slice_num(CUTOFF_PINS[i]);
    CUTOFF_PWM_CHANS[i] = pwm_gpio_to_channel(CUTOFF_PINS[i]);
    // Cut1+Reso0 share slice 2 — both CV, wrap 4095.
    pwm_set_wrap(CUTOFF_PWM_SLICES[i], DIV_COUNTER_CV);
    pwm_set_enabled(CUTOFF_PWM_SLICES[i], true);

    gpio_set_function(RESO_PINS[i], GPIO_FUNC_PWM);
    RESO_PWM_SLICES[i] = pwm_gpio_to_slice_num(RESO_PINS[i]);
    RESO_PWM_CHANS[i] = pwm_gpio_to_channel(RESO_PINS[i]);
    // Reso1 (GP7) shares slice 3 with RANGE OSC2 (GP22): keep DIV_COUNTER wrap from init_pwm.
    if (RESO_PWM_SLICES[i] != RANGE_PWM_SLICES[1]) {
      pwm_set_wrap(RESO_PWM_SLICES[i], DIV_COUNTER_CV);
    }
    pwm_set_enabled(RESO_PWM_SLICES[i], true);
  }

  gpio_set_function(VCA_PIN, GPIO_FUNC_PWM);
  VCA_PWM_SLICE = pwm_gpio_to_slice_num(VCA_PIN);
  VCA_PWM_CHAN = pwm_gpio_to_channel(VCA_PIN);
  pwm_set_wrap(VCA_PWM_SLICE, DIV_COUNTER_CV);
  pwm_set_enabled(VCA_PWM_SLICE, true);

  // Dist Mix shares slice 5 with VCA (wrap already 4095). Drive is alone on slice 4 B.
  // Dual-MCU (ENABLE_VOICE_AUX): RP2040 owns these pins — do not claim them here.
#ifndef ENABLE_VOICE_AUX
  gpio_set_function(DIST_DRIVE_PIN, GPIO_FUNC_PWM);
  DIST_DRIVE_PWM_SLICE = pwm_gpio_to_slice_num(DIST_DRIVE_PIN);
  DIST_DRIVE_PWM_CHAN = pwm_gpio_to_channel(DIST_DRIVE_PIN);
  pwm_set_wrap(DIST_DRIVE_PWM_SLICE, DIV_COUNTER_CV);
  pwm_set_enabled(DIST_DRIVE_PWM_SLICE, true);

  gpio_set_function(DIST_MIX_PIN, GPIO_FUNC_PWM);
  DIST_MIX_PWM_SLICE = pwm_gpio_to_slice_num(DIST_MIX_PIN);
  DIST_MIX_PWM_CHAN = pwm_gpio_to_channel(DIST_MIX_PIN);
  if (DIST_MIX_PWM_SLICE != VCA_PWM_SLICE) {
    pwm_set_wrap(DIST_MIX_PWM_SLICE, DIV_COUNTER_CV);
  }
  pwm_set_enabled(DIST_MIX_PWM_SLICE, true);
#endif

  init_level_pwm();
}

// True if this slice already owns a RANGE or PW wrap that must not become 4095.
static bool level_pwm_slice_shares_voice_wrap(uint8_t slice) {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    if (RANGE_PWM_SLICES[i] == 0xFF) continue;
    if (slice == RANGE_PWM_SLICES[i]) return true;
  }
  for (int i = 0; i < NUM_PW_CHANNELS; i++) {
    if (PW_PWM_SLICES[i] == 0xFF) continue;
    if (slice == PW_PWM_SLICES[i]) return true;
  }
  return false;
}

static void init_one_level_pwm(uint8_t pin, uint8_t& slice, uint8_t& chan) {
  gpio_set_function(pin, GPIO_FUNC_PWM);
  slice = pwm_gpio_to_slice_num(pin);
  chan = pwm_gpio_to_channel(pin);
  // OSC1 level (GP16) shares slice 0 with RANGE OSC3; OSC2 (GP18) shares slice 1 with PW.
  if (!level_pwm_slice_shares_voice_wrap(slice)) {
    pwm_set_wrap(slice, DIV_COUNTER_CV);
  }
  pwm_set_enabled(slice, true);
}

// Per-slice wrap for shared RANGE/PW domains; 0 = CV wrap (no scale). Filled in init_level_pwm.
static uint16_t level_wrap_for_slice[8] = { 0 };

static inline uint16_t scale_level_cv_to_wrap(uint16_t level_cv, uint8_t slice) {
  const uint16_t wrap = level_wrap_for_slice[slice & 7];
  if (wrap) {
    // ÷4096 via >>12 (same as lerp_0_4095); avoids hot /4095 on M0+.
    return (uint16_t)(((uint32_t)level_cv * (uint32_t)wrap) >> 12);
  }
  return level_cv;
}

void init_level_pwm() {
  for (int s = 0; s < 8; s++) {
    level_wrap_for_slice[s] = 0;
  }
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    if (RANGE_PWM_SLICES[i] != 0xFF) {
      level_wrap_for_slice[RANGE_PWM_SLICES[i] & 7] = DIV_COUNTER;
    }
  }
  for (int i = 0; i < NUM_PW_CHANNELS; i++) {
    if (PW_PWM_SLICES[i] != 0xFF) {
      level_wrap_for_slice[PW_PWM_SLICES[i] & 7] = DIV_COUNTER_PW;
    }
  }

  init_one_level_pwm(OSC1_LEVEL_PIN, OSC1_LEVEL_PWM_SLICE, OSC1_LEVEL_PWM_CHAN);
  init_one_level_pwm(OSC2_LEVEL_PIN, OSC2_LEVEL_PWM_SLICE, OSC2_LEVEL_PWM_CHAN);
  init_one_level_pwm(OSC3_LEVEL_PIN, OSC3_LEVEL_PWM_SLICE, OSC3_LEVEL_PWM_CHAN);
  init_one_level_pwm(SUB_LEVEL_PIN, SUB_LEVEL_PWM_SLICE, SUB_LEVEL_PWM_CHAN);
  write_level_pwm();
}

void write_level_pwm_raw(uint16_t osc1, uint16_t osc2, uint16_t osc3, uint16_t sub) {
  pwm_set_chan_level(OSC1_LEVEL_PWM_SLICE, OSC1_LEVEL_PWM_CHAN,
                     scale_level_cv_to_wrap(osc1, OSC1_LEVEL_PWM_SLICE));
  pwm_set_chan_level(OSC2_LEVEL_PWM_SLICE, OSC2_LEVEL_PWM_CHAN,
                     scale_level_cv_to_wrap(osc2, OSC2_LEVEL_PWM_SLICE));
  pwm_set_chan_level(OSC3_LEVEL_PWM_SLICE, OSC3_LEVEL_PWM_CHAN,
                     scale_level_cv_to_wrap(osc3, OSC3_LEVEL_PWM_SLICE));
  pwm_set_chan_level(SUB_LEVEL_PWM_SLICE, SUB_LEVEL_PWM_CHAN,
                     scale_level_cv_to_wrap(sub, SUB_LEVEL_PWM_SLICE));
}

void write_level_pwm() {
  write_level_pwm_raw(OSC1Level, OSC2Level, OSC3Level, SubLevel);
}

// Push raw compare values to the cutoff / per-filter resonance / VCA / dist slices.
void write_cv_pwm_raw(uint16_t cutoff, const uint16_t resonance[NUM_FILTERS], uint16_t vca,
                      uint16_t dist_drive, uint16_t dist_mix) {
  for (int i = 0; i < NUM_FILTERS; i++) {
    pwm_set_chan_level(CUTOFF_PWM_SLICES[i], CUTOFF_PWM_CHANS[i], cutoff);

    uint16_t reso_level = resonance[i];
    if (RESO_PWM_SLICES[i] == RANGE_PWM_SLICES[1]) {
      // Shared wrap DIV_COUNTER with RANGE OSC2 — scale 0..4095 → 0..DIV_COUNTER via >>12.
      reso_level = (uint16_t)(((uint32_t)resonance[i] * (uint32_t)DIV_COUNTER) >> 12);
    }
    pwm_set_chan_level(RESO_PWM_SLICES[i], RESO_PWM_CHANS[i], reso_level);
  }
  pwm_set_chan_level(VCA_PWM_SLICE, VCA_PWM_CHAN, vca);
#ifndef ENABLE_VOICE_AUX
  pwm_set_chan_level(DIST_DRIVE_PWM_SLICE, DIST_DRIVE_PWM_CHAN, dist_drive);
  pwm_set_chan_level(DIST_MIX_PWM_SLICE, DIST_MIX_PWM_CHAN, dist_mix);
#else
  (void)dist_drive;
  (void)dist_mix;
#endif
}

// Push soft VCF/VCA/reso/dist levels to Pico PWM compares (panel Dist Drive; matrix may override).
void write_cv_pwm() {
  write_cv_pwm_raw(VCF_PWM[0], RESONANCE_PWM, VCA_PWM[0], DIST_DRIVE, DIST_MIX);
}

#endif  // ENABLE_CV_OUTS

void print_dma_pwm_report() {
  uint8_t active_count = 0;
  uint8_t fallback_count = 0;

  Serial.println("\n========== [ PWM DMA ALLOCATION REPORT ] ==========");
  for (uint8_t i = 0; i < num_voice_routes; i++) {
    int chan = active_voice_routes[i].dma_chan;
    if (chan >= 0) {
      active_count++;
      Serial.printf("  Route %02d | PWM Slice %02d -> DMA Ch %02d [DITHER ACTIVE]\n", 
                    i, active_voice_routes[i].slice_num, chan);
    } else {
      fallback_count++;
      Serial.printf("  Route %02d | PWM Slice %02d -> [FALLBACK / STANDARD PWM]\n", 
                    i, active_voice_routes[i].slice_num);
    }
  }
  Serial.println("---------------------------------------------------");
  Serial.printf("  Total Routes: %d | Dithered: %d | Fallback: %d\n", 
                num_voice_routes, active_count, fallback_count);
  
  if (fallback_count > 0) {
    Serial.println("  [!] WARNING: Some slices dropped to fallback standard PWM.");
  } else {
    Serial.println("  [*] SUCCESS: All PWM slices are fully DMA-dithered.");
  }
  Serial.println("===================================================\n");
}

void print_mcu_dma_map() {
  Serial.println("\n========== [ MCU DMA CHANNEL ALLOCATION MAP ] ==========");
  
  // NUM_DMA_CHANNELS is 12 on RP2040, 16 on RP2350
  for (uint i = 0; i < NUM_DMA_CHANNELS; i++) {
    bool claimed = dma_channel_is_claimed(i);
    
    // Check if this specific channel is used by our PWM dither engine
    int pwm_route = -1;
    for (uint8_t r = 0; r < num_voice_routes; r++) {
      if (active_voice_routes[r].dma_chan == (int)i) {
        pwm_route = r;
        break;
      }
    }

    if (pwm_route >= 0) {
      Serial.printf("  DMA Ch %02d: [CLAIMED] -> PWM Dither (Route %02d, Slice %02d)\n", 
                    i, pwm_route, active_voice_routes[pwm_route].slice_num);
    } else if (claimed) {
      Serial.printf("  DMA Ch %02d: [CLAIMED] -> Other Peripheral (UART / PIO / Drivers)\n", i);
    } else {
      Serial.printf("  DMA Ch %02d: [FREE]\n", i);
    }
  }
  Serial.println("=========================================================\n");
}