#pragma once
#include <stdint.h>
#include "mbc.h"
#include "timers.h"

/*

  0000-3FFF   16KB ROM Bank 00     (in cartridge, fixed at bank 00)
  4000-7FFF   16KB ROM Bank 01..NN (in cartridge, switchable bank number)
  8000-9FFF   8KB Video RAM (VRAM) (switchable bank 0-1 in CGB Mode)
  A000-BFFF   8KB External RAM     (in cartridge, switchable bank, if any)
  C000-CFFF   4KB Work RAM Bank 0 (WRAM)
  D000-DFFF   4KB Work RAM Bank 1 (WRAM)  (switchable bank 1-7 in CGB Mode)
  E000-FDFF   Same as C000-DDFF (ECHO)    (typically not used)
  FE00-FE9F   Sprite Attribute Table (OAM)
  FEA0-FEFF   Not Usable
  FF00-FF7F   I/O Ports
  FF80-FFFE   High RAM (HRAM)
  FFFF        Interrupt Enable Register
 */

struct Ppu;

typedef struct Bus {
  Cartridge_t *cartridge;
  Timers_t timers;
  struct Ppu *ppu;


  uint8_t rom[0x8000];
  uint8_t *bootrom;
  bool bootrom_enabled;
  size_t bootrom_size;

  uint8_t wram[0x8000];
  uint8_t hram[0x7F];

  uint8_t vram[0x4000];
  uint8_t oam[0xA0];

  uint8_t IE;
  uint8_t IF;
  uint8_t JOYP;
  uint8_t SB, SC;
  int serial_cycles;
  
  // Audio registers (stubs)
  uint8_t audio_regs[0x30];
  // cgb
  bool is_cgb;
  uint8_t VBK; // vram bank
  uint8_t SVBK; // wram bank
  uint8_t KEY1; // speed switch (0xFF4D)
  uint8_t RP; // infrared port (0xFF56)
	       
  // Button states (0=pressed, 1=released)
  uint8_t buttons_dir;    // Direction buttons: bits 0=Right, 1=Left, 2=Up, 3=Down
  uint8_t buttons_action; // Action buttons: bits 0=A, 1=B, 2=Select, 3=Start
} Bus_t;

void init_bus(Bus_t* b);
uint8_t read_byte_bus(Bus_t* bus, uint16_t addy);
void write_byte_bus(Bus_t* bus, uint16_t addy, uint8_t val);
int bus_load_rom(Bus_t *bus, const char* path);
void bus_update_serial(Bus_t *bus, int cycles);

static inline uint16_t bus_read16(Bus_t* b, uint16_t addr) {
  uint8_t lo = read_byte_bus(b, addr);
  uint8_t hi = read_byte_bus(b, addr+1);
  return (uint16_t)((hi << 8) | lo);
}
static inline void bus_write16(Bus_t* b, uint16_t addr, uint16_t val) {
  write_byte_bus(b, addr, (uint8_t)(val & 0xFF));
  write_byte_bus(b, addr+1, (uint8_t)(val >> 8));
}
