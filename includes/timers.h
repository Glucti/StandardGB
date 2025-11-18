#pragma once
#include <stdint.h> 
#include <stdbool.h>

typedef struct Timers {
  uint16_t DIV; 
  uint8_t TIMA, TMA, TAC;

  uint32_t div_count;
  uint32_t tima_count;

  bool tima_overflow;
  int32_t overflow_delay;
} Timers_t;

void tick_timers(Timers_t *timers, uint32_t cycles, uint8_t *IF_REG);
void timers_init(Timers_t *timers);
uint8_t timers_read(Timers_t *t, uint16_t addy);
void timers_write(Timers_t *t, uint16_t addy, uint8_t val);
