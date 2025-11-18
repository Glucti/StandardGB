#pragma once
#include <stdbool.h>
#include "cpu.h"

typedef struct Interrupts {
  bool vblank;
  bool lcd;
  bool tima;
  bool serial;
  bool joypad;
} Interrupts_h;

typedef enum interrupt_source {
  INT_VBLANK = 0x0040,
  INT_STAT = 0x0048,
  INT_TIMER = 0x0050,
  INT_SERIAL = 0x0058,
  INT_JOYPAD = 0x0060,
} interrupt_source;

uint8_t handle_interrupts(registers_t *cpu);
