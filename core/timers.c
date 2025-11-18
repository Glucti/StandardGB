#include <stdint.h>
#include "timers.h"

void timers_init(Timers_t *timers) {
  *timers = (Timers_t){0};
  timers->TAC = 0;
}

static uint16_t select_tima(uint8_t tac) {
  static uint16_t cycles[4] = {1024, 16, 64, 256};
  return cycles[tac & 0x03];
}

void tick_timers(Timers_t *timers, uint32_t cycles, uint8_t *IF_REG) {
  timers->div_count += cycles;

  // DIV is upper 8 bits of 16-bit div counter, increments every 256 cycles
  timers->DIV = (uint16_t)(timers->div_count >> 8);

  if (timers->tima_overflow) {
    timers->overflow_delay -= (int16_t)cycles;
    if (timers->overflow_delay <= 0) {
      timers->tima_overflow = false;
      timers->TIMA = timers->TMA;
      *IF_REG |= 0x04;
    }
  }

  if (timers->TAC & 0x04) {
    uint16_t period = select_tima(timers->TAC);
    timers->tima_count += cycles;

    while (timers->tima_count >= period) {
      timers->tima_count -= period;

      if (timers->TIMA == 0xFF) {
	timers->TIMA = 0x00;
	timers->tima_overflow = true;
	timers->overflow_delay = 4;
      } else {
	timers->TIMA++;
      }
    }
  }
}

uint8_t timers_read(Timers_t *t, uint16_t addy) {
  switch(addy) {
    case 0xFF04:
      return (uint8_t)(t->DIV >> 8);
    case 0xFF05:
      return t->TIMA;
    case 0xFF06:
      return t->TMA;
    case 0xFF07:
      return t->TAC & 0x07;
    default:
      return 0xFF;
  }
}

void timers_write(Timers_t *t, uint16_t addy, uint8_t val) {
  switch(addy) {
    case 0xFF04:
      t->DIV = 0x0000;
      t->div_count = 0;
      return;
    case 0xFF05:
      if (t->tima_overflow) {
        t->tima_overflow = false;
        t->overflow_delay = 0;
      }
      t->TIMA = val;
      return;
    case 0xFF06:
      t->TMA = val;
      return;
    case 0xFF07: {
      uint8_t old_tac = t->TAC;
      t->TAC = val & 0x07;
      if ((t->TAC ^ old_tac) & 0x07)
	t->tima_count = 0;
      }
      return;
  }
}



