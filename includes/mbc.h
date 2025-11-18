#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef enum {
  MBC_0 = 0,
  MBC_1,
  MBC_3, 
  MBC_5
} mbc_t;

typedef struct Cartridge {
  mbc_t type;
  uint8_t *rom;
  size_t rom_size;

  uint8_t *ram; 
  size_t ram_size; 
  bool ram_enable;

  uint8_t rom_bank;
  uint8_t ram_bank;
  uint8_t mode;

  uint16_t rom_banks;
  uint16_t ram_banks;

  uint8_t rtc_regs[5];	// 0 S | 1 M | 2 H | 3 DL | 4 DH
  uint8_t rtc_reg_select;
  bool rtc_latched;
  uint8_t rtc_latched_regs[5];
  bool rtc_halt;
  bool rtc_day_carry;
  time_t rtc_last_update;
  uint32_t rtc_total_seconds;
  uint8_t rtc_latch_prev;

  // GBC
  bool is_cgb;
  
  // SGB
  bool is_sgb;
} Cartridge_t;

Cartridge_t *load_cart(const char *path);
void free_cart(Cartridge_t *cart);
void cart_write(Cartridge_t *cart, uint16_t addy, uint8_t val); 
uint8_t cart_read(Cartridge_t *cart, uint16_t addy);

