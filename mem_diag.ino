#include "include_all.h"
#include "mem_diag.h"

#ifdef ENABLE_MEM_DIAG

#include "hardware/sync.h"
#include <malloc.h>

#ifndef SRAM_BASE
#define SRAM_BASE 0x20000000u
#endif

// Linker symbols (Arduino-Pico / Pico SDK memmap). Heap span = StackLimit - bss_end.
// Arduino-Pico 6 already declares __bss_end__ / __StackLimit (char) and
// __scratch_x_start__ / __scratch_y_start__ (uint32_t) in RP2040Support.h.
extern char __bss_end__;
extern char __StackLimit;

// Present on some Pico SDK scripts when .time_critical is its own VMA. Weak: 0 if absent.
extern char __ram_text_start__ __attribute__((weak));
extern char __ram_text_end__ __attribute__((weak));

volatile bool mem_diag_runtime_enabled = true;
volatile bool mem_diag_pending = false;
volatile bool mem_diag_core1_ready = false;
static volatile int mem_diag_core1_free_stack = -1;

// PARAM_DEBUG_COMMAND 13: ask Core 1 for stack, then print from Core 0.
void mem_diag_request() {
  if (!mem_diag_runtime_enabled) {
    return;
  }
  mem_diag_core1_ready = false;
  __dmb();
  mem_diag_pending = true;
}

// loop1: instantaneous remaining stack on this core (not a high-water mark).
void mem_diag_poll_core1_work() {
  mem_diag_core1_free_stack = rp2040.getFreeStack();
  __dmb();
  mem_diag_core1_ready = true;
}

static int mem_diag_stack_used(int free_b, unsigned bank) {
  if (free_b < 0 || bank == 0u) {
    return -1;
  }
  if ((unsigned)free_b >= bank) {
    return 0;
  }
  return (int)(bank - (unsigned)free_b);
}

// loop: print once Core 1 has answered. Core 1 never Serial.print.
void mem_diag_poll_core0_work() {
  mem_diag_pending = false;
  mem_diag_core1_ready = false;

  const int heap_total = rp2040.getTotalHeap();
  const int heap_used = rp2040.getUsedHeap();
  const int heap_free = rp2040.getFreeHeap();
  const int core0_free = rp2040.getFreeStack();
  const int core1_free = mem_diag_core1_free_stack;
  const struct mallinfo mi = mallinfo();

  const uintptr_t sram_base = (uintptr_t)SRAM_BASE;
  const uintptr_t bss_end = (uintptr_t)&__bss_end__;
  const uintptr_t stack_limit = (uintptr_t)&__StackLimit;
  const unsigned long main_b = (stack_limit > sram_base) ? (unsigned long)(stack_limit - sram_base) : 0ul;
  const unsigned long static_b = (bss_end > sram_base) ? (unsigned long)(bss_end - sram_base) : 0ul;
  const unsigned static_pct = (main_b > 0ul) ? (unsigned)((static_b * 100ul) / main_b) : 0u;
  const unsigned heap_pct = (main_b > 0ul) ? (unsigned)(((unsigned long)heap_total * 100ul) / main_b) : 0u;

  const uintptr_t sx0 = (uintptr_t)&__scratch_x_start__;
  const uintptr_t sy0 = (uintptr_t)&__scratch_y_start__;
  unsigned bank = 0u;
  if (sy0 > sx0) {
    bank = (unsigned)(sy0 - sx0);
  }
  if (bank == 0u) {
    bank = 4096u;
  }
  const uintptr_t sx1 = sx0 + bank;
  const uintptr_t sy1 = sy0 + bank;
  const int core0_used = mem_diag_stack_used(core0_free, bank);
  const int core1_used = mem_diag_stack_used(core1_free, bank);

  Serial.println(F("=== DCO RAM ==="));
#if defined(PICO_RP2350)
  Serial.printf("mcu    RP2350  clk=%luMHz  polls=%s\n",
#else
  Serial.printf("mcu    RP2040  clk=%luMHz  polls=%s\n",
#endif
                (unsigned long)(rp2040.f_cpu() / 1000000ul),
                mem_diag_runtime_enabled ? "on" : "off");
  Serial.printf("sram   main=%lu static=%lu (%u%%) heap=%d (%u%%)\n",
                main_b, static_b, static_pct, heap_total, heap_pct);
  Serial.printf("heap   total=%d used=%d free=%d  arena=%d free_chunks=%d\n",
                heap_total, heap_used, heap_free, (int)mi.arena, (int)mi.ordblks);
  Serial.printf("stack  core0 %d free / %d used / %u   core1 %d free / %d used / %u\n",
                core0_free, core0_used, bank, core1_free, core1_used, bank);
  Serial.printf("layout sram=0x%08lx bss_end=0x%08lx stack_limit=0x%08lx\n",
                (unsigned long)sram_base, (unsigned long)bss_end, (unsigned long)stack_limit);
  Serial.printf("scratch_x=0x%08lx..0x%08lx (%u)  scratch_y=0x%08lx..0x%08lx (%u)\n",
                (unsigned long)sx0, (unsigned long)sx1, bank,
                (unsigned long)sy0, (unsigned long)sy1, bank);

  const uintptr_t rt0 = (uintptr_t)&__ram_text_start__;
  const uintptr_t rt1 = (uintptr_t)&__ram_text_end__;
  if (rt0 != 0 && rt1 != 0 && rt1 >= rt0) {
    Serial.printf("ram_text=%lu  (0x%08lx..0x%08lx)\n",
                  (unsigned long)(rt1 - rt0), (unsigned long)rt0, (unsigned long)rt1);
  } else {
    Serial.println(F("ram_text=(no __ram_text_* symbols — use the .map for .time_critical)"));
  }
  Serial.println(F("================"));
}

#endif  // ENABLE_MEM_DIAG
