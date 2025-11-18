#include "interrupts.h"
#include "cpu.h"
#include "memory.h"
#include "logging.h"
#include "timers.h"
#include "ppu.h"

uint8_t read_byte_bus(Bus_t *bus, uint16_t addy);

#define GET_FLAG(num) ( (uint8_t)(1u << ( ((num)-0x40) / 8)) )

#define IF_ADDY 0xFF0F
#define IE_ADDY 0xFFFF


static inline void push_16(registers_t *cpu, u16 val) {
  cpu->SP--;
  cpu->cycle += 4;
  if (!cpu->stopped) {
    tick_timers(&cpu->bus->timers, 4, &cpu->bus->IF);
    display_cycle(cpu->ppu, cpu->bus, 4);
  }
  write_byte_bus(cpu->bus, cpu->SP, (u8)(val >> 8));
  cpu->SP--;
  cpu->cycle += 4;
  if (!cpu->stopped) {
    tick_timers(&cpu->bus->timers, 4, &cpu->bus->IF);
    display_cycle(cpu->ppu, cpu->bus, 4);
  }
  write_byte_bus(cpu->bus, cpu->SP, (u8)(val & 0xFF));
}

u8 read_interrupt(registers_t *cpu, uint16_t addy) {
  if (addy == IF_ADDY) {
    return (cpu->bus->IF & 0x1F) | 0xE0;
  }
  if (addy == IE_ADDY) {
    return (cpu->bus->IE & 0x1F);
  }
  return 0xFF;
}

void write_interrupt(registers_t *cpu, uint16_t addy, uint8_t val) {
  if (addy == IF_ADDY) {
    cpu->bus->IF = val & 0x1F;
  }
  if (addy == IE_ADDY) {
    cpu->bus->IE = val & 0x1F;
  }
}

void interrupt_req(registers_t *cpu, interrupt_source interrupt) {
  cpu->bus->IF = cpu->bus->IF | GET_FLAG(interrupt);
}

bool interrupt_isset(registers_t *cpu, interrupt_source interrupt) {
  return cpu->bus->IF & GET_FLAG(interrupt);
}

bool interrupt_isEnabled(registers_t *cpu, interrupt_source interrupt) {
  return cpu->bus->IE & GET_FLAG(interrupt);
}

void set_IME(registers_t *cpu, bool val) {
  cpu->IME = val;
}

static inline void handle_interrupt(registers_t *cpu, interrupt_source interrupt) {
  cpu->halt = false;
  cpu->IME = 0;

  cpu->bus->IF &= (uint8_t)~GET_FLAG(interrupt);

  push_16(cpu, cpu->PC);
  cpu->PC = (uint16_t)interrupt;
  
  // Debug: check what code is at the interrupt handler
  //uint8_t handler_code = read_byte_bus(cpu->bus, cpu->PC);
  //
  //const char *int_name = "UNKNOWN";
  //if (interrupt == 0x40) int_name = "VBLANK";
  //else if (interrupt == 0x48) int_name = "STAT";
  //else if (interrupt == 0x50) int_name = "TIMER";
  //else if (interrupt == 0x58) int_name = "SERIAL";
  //else if (interrupt == 0x60) int_name = "JOYPAD";
  
  //write_log("[INT] Handling %s interrupt | old PC=%04X -> handler=%04X (code=%02X) | IF=%02X IE=%02X IME=%d\n",
  //          int_name, old_pc, cpu->PC, handler_code, cpu->bus->IF, cpu->bus->IE, cpu->IME);
}

u8 handle_interrupts(registers_t *cpu) {
  for (interrupt_source i = INT_VBLANK; i <= INT_JOYPAD; i += 0x8) {
    if (interrupt_isset(cpu, i) && interrupt_isEnabled(cpu, i)) {
      cpu->halt = false;

      handle_interrupt(cpu, i);
      return 12;
    }
  }
  return 0;
}
