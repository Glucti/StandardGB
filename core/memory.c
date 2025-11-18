#include <string.h> 
#include <stdio.h>
#include "memory.h"
#include "mbc.h"
#include "ppu.h"
#include "logging.h"

void init_bus(Bus_t* b) {
  memset(b, 0, sizeof(*b));
  timers_init(&b->timers);
  b->JOYP = 0xFF; 
  b->buttons_dir = 0x0F; 
  b->buttons_action = 0x0F;
  
  memset(b->hram, 0x00, sizeof(b->hram));
  memset(b->oam, 0x00, sizeof(b->oam));

  b->VBK = 0;
  b->SVBK = 0;
  b->is_cgb = false;
  b->KEY1 = 0;
  b->RP = 0;
}

int bus_load_rom(Bus_t *bus, const char *path) {
  bus->cartridge = load_cart(path);
  if (bus->cartridge) {
    bus->is_cgb = bus->cartridge->is_cgb;
    fprintf(stderr, "[BUS] CGB mode: %s\n", bus->is_cgb ? "ENABLED" : "disabled");
    if (bus->cartridge->is_sgb && !bus->is_cgb) {
      fprintf(stderr, "[BUS] SGB Enhanced: YES (color palette applied)\n");
    }
  }
  return bus->cartridge ? 0 : 1;
}

uint8_t read_byte_bus(Bus_t *bus, uint16_t addy) {
  if (bus->ppu && bus->ppu->dma_active) {
    if (addy >= 0xFF80 && addy <= 0xFFFE) {
      return bus->hram[addy - 0xFF80];
    }
    if (addy >= 0xFE00 && addy <= 0xFE9F) {
      return 0xFF;
    }
    return 0xFF;
  }
  
  if (bus->bootrom_enabled && bus->bootrom) {
    if (addy < 0x0100) {
      return bus->bootrom[addy];
    }
    if (bus->bootrom_size > 256 && addy >= 0x0200 && addy < 0x0900) {
      return bus->bootrom[addy - 0x0100];
    }
  }
  if (addy < 0x8000) {
    uint8_t v = cart_read(bus->cartridge, addy);
    static int rom_reads = 0;
    if (rom_reads < 80) {
      rom_reads++;
    }
    return v;
  }
  if (addy <= 0x9FFF) {
      uint16_t offset = addy - 0x8000;
      uint8_t bank = bus->VBK & 0x01;
      uint16_t real_addy = offset + (bank * 0x2000);
    if (bus->ppu) {
      return ppu_vram_read(bus->ppu, real_addy);
    }
    return bus->vram[real_addy];
  }

  if (addy >= 0xA000 && addy <= 0xBFFF) return cart_read(bus->cartridge, addy); 
  if (addy >= 0xC000 && addy <= 0xCFFF) {
    return bus->wram[addy - 0xC000];
  }
  if (addy >= 0xD000 && addy <= 0xDFFF) {
    uint8_t bank = bus->SVBK & 0x07;
    if (bank == 0) 
      bank = 1;
    uint16_t offset = addy - 0xD000;
    uint16_t real_address = 0x1000 + ((bank - 1) * 0x1000) + offset;
    return bus->wram[real_address];
  }
  if (addy >= 0xE000 && addy <= 0xEFFF) {
    return bus->wram[addy - 0xE000]; // Mirrors 0xC000-0xCFFF
  }
  if (addy >= 0xF000 && addy <= 0xFDFF) {
    uint8_t bank = bus->SVBK & 0x07;
    if (bank == 0)
      bank = 1;

    uint16_t offset = addy - 0xF000;
    uint16_t real_addr = 0x1000 + ((bank - 1) * 0x1000) + offset;
    return bus->wram[real_addr]; // Mirrors 0xD000-0xDFFF
  }
  if (addy >= 0xFE00 && addy <= 0xFE9F) return bus->oam[addy - 0xFE00];
  if (addy >= 0xFEA0 && addy <= 0xFEFF) return 0xFF;

  switch(addy) {
    case 0xFF00: {
      // JOYP register: bits 7-6 always 1, bits 5-4 are selection, bits 3-0 are button states
      uint8_t select = bus->JOYP & 0x30; 
      uint8_t buttons = 0x0F; 
      
      if ((select & 0x20) == 0) {
        // Action buttons
        buttons = bus->buttons_action & 0x0F;
      }
      if ((select & 0x10) == 0) {
        // Direction buttons
        if ((select & 0x20) == 0) {
          // Both selected
          buttons = (bus->buttons_action & bus->buttons_dir) & 0x0F;
        } else {
          // Only direction
          buttons = bus->buttons_dir & 0x0F;
        }
      }
      return 0xC0 | select | buttons;
    }
    case 0xFF01: 
      return bus->SB;
    case 0xFF02:
      return bus->SC;
    case 0xFF04:
    case 0xFF05: 
    case 0xFF06:
    case 0xFF07:
      return timers_read(&bus->timers, addy);
    case 0xFF0F:
      return (uint8_t)(0xE0 | (bus->IF & 0x1F));
    
    // Audio registers (0xFF10-0xFF3F) mayb 1 day
    case 0xFF10: case 0xFF11: case 0xFF12: case 0xFF13: case 0xFF14:
    case 0xFF16: case 0xFF17: case 0xFF18: case 0xFF19:
    case 0xFF1A: case 0xFF1B: case 0xFF1C: case 0xFF1D: case 0xFF1E:
    case 0xFF20: case 0xFF21: case 0xFF22: case 0xFF23:
    case 0xFF24: case 0xFF25: case 0xFF26:
    case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33: case 0xFF34:
    case 0xFF35: case 0xFF36: case 0xFF37: case 0xFF38: case 0xFF39:
    case 0xFF3A: case 0xFF3B: case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
      return bus->audio_regs[addy - 0xFF10];
    case 0xFF40: return bus->ppu->LCDC;
    case 0xFF41: return bus->ppu->STAT;
    case 0xFF42: return bus->ppu->SCY;
    case 0xFF43: return bus->ppu->SCX;
    case 0xFF44: return bus->ppu->LY;
    case 0xFF45: return bus->ppu->LYC;
    case 0xFF46: return bus->ppu->DMA;
    case 0xFF47: return bus->ppu->BGP;
    case 0xFF48: return bus->ppu->OBP0;
    case 0xFF49: return bus->ppu->OBP1;
    case 0xFF4A: return bus->ppu->WY;
    case 0xFF4B: return bus->ppu->WX;
    //cgb
    case 0xFF4D: return bus->KEY1 | 0x7E;
    case 0xFF4F: return bus->VBK | 0xFE;
    // CGB HDMA registers
    case 0xFF51: return bus->ppu->HDMA1;
    case 0xFF52: return bus->ppu->HDMA2;
    case 0xFF53: return bus->ppu->HDMA3;
    case 0xFF54: return bus->ppu->HDMA4;
    case 0xFF55: return bus->ppu->HDMA5;
    case 0xFF70: return bus->SVBK | 0xF8;
    case 0xFF68: return bus->ppu->BCPS;
    case 0xFF69: {
      uint8_t byte = bus->ppu->BCPS & 0x3F;
      return bus->ppu->bg_pallete[byte];
    }
    case 0xFF6A: return bus->ppu->OCPS;
    case 0xFF6B: {
      uint8_t byte = bus->ppu->OCPS & 0x3F;
      return bus->ppu->obj_pallete[byte];
    }
    default: break;
  }

  if (addy >= 0xFF80 && addy <= 0xFFFE)
    return bus->hram[addy - 0xFF80];
  if (addy == 0xFFFF) return bus->IE;

  return 0xFF;
}

void write_byte_bus(Bus_t *bus, uint16_t addy, uint8_t val) {
  if (bus->ppu && bus->ppu->dma_active) {
    if (addy >= 0xFF80 && addy <= 0xFFFE) {
      bus->hram[addy - 0xFF80] = val;
    }
    return;
  }

  if (addy < 0x8000) {
    cart_write(bus->cartridge, addy, val);
    return;
  };
  if (addy <= 0x9FFF) {
    uint16_t offset = addy - 0x8000;
    uint8_t bank = bus->VBK & 0x01;
    uint16_t real_addy = offset + (bank * 0x2000);
    if (bus->ppu) {
      ppu_vram_write(bus->ppu, real_addy, val);
      return;
    }
    bus->vram[real_addy] = val;
    return;
  }

  if (addy >= 0xA000 && addy <= 0xBFFF) {
    cart_write(bus->cartridge, addy, val);
    return;
  }
  if (addy >= 0xC000 && addy <= 0xCFFF) {
    bus->wram[addy - 0xC000] = val;
    return;
  }

  if (addy >= 0xD000 && addy <= 0xDFFF) {
    uint8_t bank = bus->SVBK & 0x07;
    if (bank == 0) bank = 1;  
    
    uint16_t offset = addy - 0xD000;
    uint16_t real_addr = 0x1000 + ((bank - 1) * 0x1000) + offset;
    bus->wram[real_addr] = val;
    return;
  }
  if (addy >= 0xE000 && addy <= 0xEFFF) {
    bus->wram[addy - 0xE000] = val;  // Mirrors 0xC000-0xCFFF
    return;
  }
  if (addy >= 0xF000 && addy <= 0xFDFF) {
    uint8_t bank = bus->SVBK & 0x07;
    if (bank == 0) bank = 1;
    
    uint16_t offset = addy - 0xF000;
    uint16_t real_addr = 0x1000 + ((bank - 1) * 0x1000) + offset;
    bus->wram[real_addr] = val;  // Mirrors 0xD000-0xDFFF
    return;
  }
  if (addy >= 0xFE00 && addy <= 0xFE9F) {
    bus->oam[addy - 0xFE00] = val;
    return;
  }
  if (addy >= 0xFEA0 && addy <= 0xFEFF) return;

  switch(addy) {
    case 0xFF00: 
      bus->JOYP = (bus->JOYP & 0xCF) | (val & 0x30);
      return;
    case 0xFF01: 
      bus->SB = val;
      return;
    case 0xFF02:
      bus->SC = val;
      if ((val & 0x81) == 0x81) {
	bus->serial_cycles = 512;
      }
      return;
    case 0xFF04: case 0xFF05: case 0xFF06:
    case 0xFF07:
      timers_write(&bus->timers, addy, val);
      return;
    case 0xFF0F:
      bus->IF = (bus->IF & ~0x1F) | (val & 0x1F); 
      return;
    
    case 0xFF10: case 0xFF11: case 0xFF12: case 0xFF13: case 0xFF14:
    case 0xFF16: case 0xFF17: case 0xFF18: case 0xFF19:
    case 0xFF1A: case 0xFF1B: case 0xFF1C: case 0xFF1D: case 0xFF1E:
    case 0xFF20: case 0xFF21: case 0xFF22: case 0xFF23:
    case 0xFF24: case 0xFF25: case 0xFF26:
    case 0xFF30: case 0xFF31: case 0xFF32: case 0xFF33: case 0xFF34:
    case 0xFF35: case 0xFF36: case 0xFF37: case 0xFF38: case 0xFF39:
    case 0xFF3A: case 0xFF3B: case 0xFF3C: case 0xFF3D: case 0xFF3E: case 0xFF3F:
      bus->audio_regs[addy - 0xFF10] = val;
      return;
    case 0xFF40: {
      uint8_t old_lcdc = bus->ppu->LCDC;
      bus->ppu->LCDC = val;
      
      bool was_enabled = (old_lcdc & 0x80) != 0;
      bool is_enabled = (val & 0x80) != 0;
      
      if (!was_enabled && is_enabled) {
        bus->ppu->LY = 0;
        bus->ppu->cycles_in_line = 0;
        bus->ppu->STAT = (bus->ppu->STAT & ~0x03) | 2; // Start in mode 2 (OAM scan)
      } else if (was_enabled && !is_enabled) {
        bus->ppu->LY = 0;
        bus->ppu->cycles_in_line = 0;
        bus->ppu->STAT = (bus->ppu->STAT & ~0x03) | 0; 
      }
      return;
    }
    case 0xFF41: bus->ppu->STAT = (val & 0x78) | (bus->ppu->STAT & 0x07); return;
    case 0xFF42: bus->ppu->SCY = val; return;
    case 0xFF43: bus->ppu->SCX = val; return;
    case 0xFF44: return; // LY is read only
    case 0xFF45: bus->ppu->LYC = val; return;
    case 0xFF46:
      bus->ppu->DMA = val;
      bus->ppu->dma_pending = true;
      return;
    case 0xFF47: bus->ppu->BGP = val; return;
    case 0xFF48: bus->ppu->OBP0 = val; return;
    case 0xFF49: bus->ppu->OBP1 = val; return;
    case 0xFF50:
      return;
    case 0xFF4A: bus->ppu->WY = val; return;
    case 0xFF4B: bus->ppu->WX = val; return;
    case 0xFF4D: bus->KEY1 = (bus->KEY1 & 0x80) | (val & 0x01); return;
    case 0xFF4F: bus->VBK = val & 0x01; return;
    case 0xFF56: bus->RP = val; return;
    case 0xFF70: bus->SVBK = val & 0x07; return;
    // CGB HDMA registers
    case 0xFF51: bus->ppu->HDMA1 = val; return;
    case 0xFF52: bus->ppu->HDMA2 = val; return;
    case 0xFF53: bus->ppu->HDMA3 = val; return;
    case 0xFF54: bus->ppu->HDMA4 = val; return;
    case 0xFF55: {
      // HDMA5: length/mode/start
      // Only meaningful in CGB mode
      if (!bus->is_cgb) {
        bus->ppu->HDMA5 = 0xFF;
        return;
      }
      // Cancel any ongoing HBlank transfer if bit7 is set while already active
      if (bus->ppu->hdma_active && (val & 0x80)) {
        bus->ppu->hdma_active = false;
        bus->ppu->HDMA5 = 0xFF;
        return;
      }

      // Compute source and destination addresses
      uint16_t src = ((uint16_t)bus->ppu->HDMA1 << 8) | (bus->ppu->HDMA2 & 0xF0);
      uint16_t dst = (uint16_t)(0x8000 | ((bus->ppu->HDMA3 & 0x1F) << 8) | (bus->ppu->HDMA4 & 0xF0));

      uint16_t length = (uint16_t)(((val & 0x7F) + 1) * 0x10);

      // General-purpose DMA (bit7 == 0): transfer all at once
      if (!(val & 0x80)) {
        bus->ppu->hdma_active = false;
        bus->ppu->HDMA5 = 0xFF;
        for (uint16_t i = 0; i < length; i++) {
          uint8_t data = read_byte_bus(bus, src + i);
          uint16_t dst_addr = dst + i;
          if (dst_addr >= 0x8000 && dst_addr < 0xA000) {
            write_byte_bus(bus, dst_addr, data);
          }
        }
      } else {
        // HBlank HDMA (bit7 == 1): set up state, transfer 16 bytes at each HBlank
        bus->ppu->hdma_active = true;
        bus->ppu->hdma_src = src;
        bus->ppu->hdma_dst = dst;
        bus->ppu->hdma_remaining = length;
        // Store remaining blocks-1 in lower 7 bits, bit7 cleared while active
        uint8_t blocks = (uint8_t)(length / 0x10);
        if (blocks) blocks -= 1;
        bus->ppu->HDMA5 = blocks & 0x7F;
      }
      return;
    }
    case 0xFF68: 
      bus->ppu->BCPS = val; 
      return;
    case 0xFF69: {
      uint8_t byte = bus->ppu->BCPS & 0x3F;
      bus->ppu->bg_pallete[byte] = val;
      if (bus->ppu->BCPS & 0x80) {
	bus->ppu->BCPS = 0x80 | ((byte + 1) & 0x3F);
      }
      return;
    }
    case 0xFF6A: bus->ppu->OCPS = val; return;
    case 0xFF6B: {
      uint8_t byte = bus->ppu->OCPS & 0x3F;
      bus->ppu->obj_pallete[byte] = val;
      if (bus->ppu->OCPS & 0x80) {
	bus->ppu->OCPS = 0x80 | ((byte + 1) & 0x3F);
      }
      return;
    }
    default: break;
  }

  if (addy >= 0xFF80 && addy <= 0xFFFE) {
    bus->hram[addy - 0xFF80] = val; 
    return;
  }
  if (addy == 0xFFFF) {
    bus->IE = (val & 0x1F);
    return;
  }
}

void bus_update_serial(Bus_t *bus, int cycles) {
  if (bus->serial_cycles > 0) {
    bus->serial_cycles -= cycles;
    if (bus->serial_cycles <= 0) {
      bus->serial_cycles = 0;
      bus->SC &= ~0x80;
      bus->IF |= 0x08;
      bus->SB = 0xFF;
    }
  }
}
