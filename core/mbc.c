#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "mbc.h"

static const uint32_t MBC3_SECONDS_PER_DAY = 24u * 60u * 60u;
static const uint16_t MBC3_DAY_MAX = 512u;

static void mbc3_rtc_update_regs(Cartridge_t *cart);
static void mbc3_rtc_tick(Cartridge_t *cart);
static void mbc3_rtc_get_components(const Cartridge_t *cart,
                                    uint16_t *days,
                                    uint8_t *hours,
                                    uint8_t *minutes,
                                    uint8_t *seconds);
static void mbc3_rtc_set_components(Cartridge_t *cart,
                                    uint16_t days,
                                    uint8_t hours,
                                    uint8_t minutes,
                                    uint8_t seconds);


mbc_t get_cartridge_type(uint8_t type) {
  switch(type) {
    case 0x00: 
      return MBC_0;
    case 0x01:
    case 0x02:
    case 0x03:
      return MBC_1;
    case 0x0F:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
      return MBC_3;
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
      return MBC_5;
    case 0x22:
      return MBC_5;
    default:
      fprintf(stderr, "[MBC] Unknown cartridge type 0x%02X, defaulting to MBC5\n", type);
      return MBC_5;
  }
}

size_t get_cartridge_rom_size(uint8_t val) {
  if (val <= 8)
    return (size_t)0x8000u << val; 
  switch (val) {
    case 0x52: 
      return 1152 * 1024;
    case 0x53:
      return 1280 * 1024;
    case 0x54:
      return 1536 * 1024;
    default: 
      return 0;
  }
}

size_t get_cartridge_ram_size(uint8_t val) {
  switch(val) {
    case 0x00:
      return 0;
    case 0x01:
      return 2 * 1024;
    case 0x02:
      return 8 * 1024;
    case 0x03: 
      return 32 * 1024;
    case 0x04:
      return 128 * 1024;
    case 0x05:
      return 64 * 1024;
    default: return 0;
  }
}

Cartridge_t *load_cart(const char *path) {

  FILE *f = fopen(path, "rb");
  if (!f) { perror("Open ROM"); return NULL; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0) {
    fclose(f);
    return NULL;
  }
  
  uint8_t *buf = (uint8_t*)malloc((size_t)len);
  if (!buf) {
    fprintf(stderr, "Failed to allocate buffer for cartridge\n");
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);

  Cartridge_t *cart = (Cartridge_t*)calloc(1, sizeof(Cartridge_t));

  if (!cart) {
    fprintf(stderr, "failed to allocate cart\n");
    free(buf);
    return NULL;
  }

  // detect cgb 
  uint8_t cgb_flag = buf[0x0143];
  if (cgb_flag == 0x80 || cgb_flag == 0xC0) {
    cart->is_cgb = true;
  } else {
    cart->is_cgb = false;
  }
  
  // detect sgb
  uint8_t sgb_flag = buf[0x0146];
  cart->is_sgb = (sgb_flag == 0x03);

  uint8_t cart_type = buf[0x0147];
  uint8_t rom_size_code = buf[0x148];
  uint8_t ram_size_code = buf[0x149];

  size_t file_size = (size_t)len;
  size_t header_size = get_cartridge_rom_size(rom_size_code);

  cart->rom = buf;
  cart->rom_size = file_size;

  cart->type = get_cartridge_type(cart_type);
  
  cart->rom_banks = (uint16_t)(cart->rom_size / 0x4000);
  if (cart->rom_banks == 0) {
    cart->rom_banks = 1;
  }

  if (header_size && header_size != file_size) {
    fprintf(stderr, "[warn] ROM header size (%zu) != file size (%zu)\n"
	,header_size, file_size);
}

  cart->ram_size = get_cartridge_ram_size(ram_size_code);
  if (cart->ram_size) {
    cart->ram = (uint8_t*)calloc(1, cart->ram_size);
    cart->ram_banks = (uint8_t)(cart->ram_size / 0x2000);
    if (cart->ram_banks == 0) cart->ram_banks = 1;
  }

  // default
  cart->rom_bank = 1;
  cart->ram_bank = 0;
  cart->mode = 0;
  cart->ram_enable = false;
  memset(cart->rtc_regs, 0, sizeof(cart->rtc_regs));
  memset(cart->rtc_latched_regs, 0, sizeof(cart->rtc_latched_regs));
  cart->rtc_latched = false;
  cart->rtc_halt = false;
  cart->rtc_day_carry = false;
  cart->rtc_total_seconds = 0;
  cart->rtc_latch_prev = 0;
  cart->rtc_last_update = time(NULL);
  return cart;
}

void free_cart(Cartridge_t *cart) {
  if (!cart) return;
  free(cart->ram);
  free(cart->rom);
  free(cart);
}

static inline uint8_t read_mbc0(Cartridge_t *cart, uint16_t addy) {
  if (addy < 0x8000) {
    return (addy < cart->rom_size) ? cart->rom[addy] : 0xFF;
  }

  if (addy >= 0xA000 && addy <= 0xBFFF) {
    if (!cart->ram || cart->ram_size == 0) return 0xFF;
    size_t offset = (size_t)(addy - 0xA000);
    return (offset < cart->ram_size) ? cart->ram[offset] : 0xFF;
  }
  return 0xFF;
}

static inline void write_mbc0(Cartridge_t *cart, uint16_t addy, uint8_t val) {
  if (addy >= 0xA000 && addy <= 0xBFFF && cart->ram && cart->ram_size) {
    size_t offset = (size_t)(addy - 0xA000);
    cart->ram[offset] = val;
  }
}

static inline uint8_t read_mbc_bytes(Cartridge_t *cart, uint32_t bank_num, uint16_t addy) {
  // skip legacy
  //bank_num %= cart->rom_banks;
  uint32_t base = bank_num * 0x4000u;
  uint32_t offset = base + (addy & 0x3FFF);

  if (offset < cart->rom_size) {
    return cart->rom[offset];
  }

  return 0xFF;
}

static void mbc3_rtc_get_components(const Cartridge_t *cart,
                                    uint16_t *days,
                                    uint8_t *hours,
                                    uint8_t *minutes,
                                    uint8_t *seconds) {
  uint32_t total = cart->rtc_total_seconds;
  uint32_t day_count = total / MBC3_SECONDS_PER_DAY;
  uint32_t remainder = total % MBC3_SECONDS_PER_DAY;

  uint8_t hour_val = (uint8_t)(remainder / 3600u);
  remainder %= 3600u;
  uint8_t minute_val = (uint8_t)(remainder / 60u);
  uint8_t second_val = (uint8_t)(remainder % 60u);

  if (days)    *days = (uint16_t)(day_count % MBC3_DAY_MAX);
  if (hours)   *hours = hour_val;
  if (minutes) *minutes = minute_val;
  if (seconds) *seconds = second_val;
}

static void mbc3_rtc_update_regs(Cartridge_t *cart) {
  uint16_t days;
  uint8_t hours, minutes, seconds;
  mbc3_rtc_get_components(cart, &days, &hours, &minutes, &seconds);

  cart->rtc_regs[0] = (uint8_t)(seconds % 60u);
  cart->rtc_regs[1] = (uint8_t)(minutes % 60u);
  cart->rtc_regs[2] = (uint8_t)(hours % 24u);
  cart->rtc_regs[3] = (uint8_t)(days & 0xFFu);

  uint8_t dh = (uint8_t)((days >> 8) & 0x01u);
  if (cart->rtc_halt) dh |= 0x40u;
  if (cart->rtc_day_carry) dh |= 0x80u;
  cart->rtc_regs[4] = dh;
}

static void mbc3_rtc_set_components(Cartridge_t *cart,
                                    uint16_t days,
                                    uint8_t hours,
                                    uint8_t minutes,
                                    uint8_t seconds) {
  days %= MBC3_DAY_MAX;
  hours %= 24u;
  minutes %= 60u;
  seconds %= 60u;

  cart->rtc_total_seconds =
    (uint32_t)days * MBC3_SECONDS_PER_DAY +
    (uint32_t)hours * 3600u +
    (uint32_t)minutes * 60u +
    (uint32_t)seconds;

  mbc3_rtc_update_regs(cart);
}

static void mbc3_rtc_tick(Cartridge_t *cart) {
  time_t now = time(NULL);
  if (now == (time_t)-1) {
    now = cart->rtc_last_update;
  }

  if (cart->rtc_last_update == 0) {
    cart->rtc_last_update = now;
  }

  if (!cart->rtc_halt && now > cart->rtc_last_update) {
    time_t delta = now - cart->rtc_last_update;
    if (delta > 0) {
      uint64_t total = (uint64_t)cart->rtc_total_seconds + (uint64_t)delta;
      uint64_t limit = (uint64_t)MBC3_DAY_MAX * (uint64_t)MBC3_SECONDS_PER_DAY;
      if (total >= limit) {
        cart->rtc_day_carry = true;
        total %= limit;
      }
      cart->rtc_total_seconds = (uint32_t)total;
    }
  }

  cart->rtc_last_update = now;
  mbc3_rtc_update_regs(cart);
}

/* --------------- MBC1 --------------- */

uint8_t read_mbc1(Cartridge_t *cart, uint16_t addy) {

  if (addy < 0x4000) {
    if (cart->mode == 1) {
      uint32_t bank = ((uint32_t)(cart->ram_bank & 0x03)) << 5;
      if (bank >= cart->rom_banks) bank %= cart->rom_banks;
      return read_mbc_bytes(cart, bank, addy);
    }
    return read_mbc_bytes(cart, 0, addy);
}


  if (addy >= 0x4000 && addy <= 0x7FFF) {  
    uint32_t low5 = (uint32_t)(cart->rom_bank & 0x1F);
    uint32_t hi2  = (cart->mode == 0)
                      ? (uint32_t)(cart->ram_bank & 0x03)
                      : 0;
    uint32_t bank = (hi2 << 5) | low5;

    if (bank >= cart->rom_banks) bank %= cart->rom_banks;
    if ((bank & 0x1F) == 0) bank |= 1;          

    static int log_cnt = 0;
    if (log_cnt < 32) {
      fprintf(stderr, "[MBC1] ROM read bank=%u pc_bank=%u hi=%u mode=%u\n",
              bank, cart->rom_bank & 0x1F, hi2, cart->mode);
      log_cnt++;
    }
    return read_mbc_bytes(cart, bank, addy);
}

  if (addy >= 0xA000 && addy <= 0xBFFF) {
    if (!cart->ram || !cart->ram_enable) return 0xFF;
    uint32_t bank = (cart->mode == 1) ? (uint32_t)(cart->ram_bank & 0x03) : 0;
    bank %= (cart->ram_banks ? cart->ram_banks : 1);
    size_t off = bank * 0x2000u + (addy - 0xA000);
    return (off < cart->ram_size) ? cart->ram[off] : 0xFF;
  }

  return 0xFF;
}

void write_mbc1(Cartridge_t *cart, uint16_t addy, uint8_t val) {
  if (addy < 0x2000) {
    cart->ram_enable = ((val & 0x0F) == 0xA);
    static int log_cnt = 0;
    if (log_cnt < 16) {
      fprintf(stderr, "[MBC1] RAM enable <= %d (val=%02X)\n", cart->ram_enable, val);
      log_cnt++;
    }
    return; 
  }

  if (addy >= 0x2000 && addy <= 0x3FFF) {
    uint8_t low5 = val & 0x1F;
    if (low5 == 0) low5 = 1;                  
    cart->rom_bank = (cart->rom_bank & ~0x1F) | low5;
    static int log_cnt = 0;
    if (log_cnt < 32) {
      fprintf(stderr, "[MBC1] ROM bank low set -> %u (val=%02X)\n",
              cart->rom_bank & 0x1F, val);
      log_cnt++;
    }
    return;
    }
  
  if (addy >= 0x4000 && addy <= 0x5FFF) {
    cart->ram_bank = (val & 0x03);
    static int log_cnt = 0;
    if (log_cnt < 32) {
      fprintf(stderr, "[MBC1] RAM/ROM high bits set -> %u (val=%02X)\n",
              cart->ram_bank & 0x03, val);
      log_cnt++;
    }
    return;
  }

  if (addy >= 0x6000 && addy <= 0x7FFF) {
    cart->mode = (val & 0x01);
    static int log_cnt = 0;
    if (log_cnt < 16) {
      fprintf(stderr, "[MBC1] MODE set -> %u (val=%02X)\n", cart->mode, val);
      log_cnt++;
    }
    return;
  }

  if (addy >= 0xA000 && addy <= 0xBFFF) {
    if (!cart->ram || !cart->ram_enable) 
      return;

    uint32_t bank = (cart->mode == 1) ? (uint32_t)(cart->ram_bank & 0x03) : 0;
    bank %= (cart->ram_banks ? cart->ram_banks : 1);

    size_t off = bank * 0x2000u + (addy - 0xA000);
    if (off < cart->ram_size) cart->ram[off] = val;
  }
}

/* -------------- MBC3 ------------------- */
uint8_t read_mbc3(Cartridge_t* cart, uint16_t addy) {
  if (addy < 0x4000) {
    return read_mbc_bytes(cart, 0, addy);
  } else if (addy >= 0x4000 && addy <= 0x7FFF) {
    uint32_t bank = cart->rom_bank & 0x7F;
    if (cart->rom_banks) {
      bank %= cart->rom_banks;
      if (bank == 0 && cart->rom_banks > 1) bank = 1;
    }
    return read_mbc_bytes(cart, bank, addy);
  } else if (addy >= 0xA000 && addy <= 0xBFFF) {
    if (!cart->ram_enable)
      return 0xFF;

    if (cart->ram_bank <= 3 && cart->ram) {
      uint8_t bank = cart->ram_bank;
      uint16_t effective = cart->ram_banks ? cart->ram_banks : 1;
      bank %= effective;
      size_t off = ((size_t)bank * 0x2000u) + (addy - 0xA000);
      if (off < cart->ram_size)
	return cart->ram[off];
      return 0xFF;
    } else if (cart->ram_bank >= 0x08 && cart->ram_bank <= 0x0C) {
      uint8_t index = (uint8_t)(cart->ram_bank - 0x08);
      if (!cart->rtc_latched) {
	mbc3_rtc_tick(cart);
	return cart->rtc_regs[index];
      }
      return cart->rtc_latched_regs[index];
    }
  }
  return 0xFF;
} 

void write_mbc3(Cartridge_t *cart, uint16_t addy, uint8_t val) {
  if (addy < 0x2000) {
    cart->ram_enable = ((val & 0x0F) == 0x0A);
    return;
  }
  if (addy >= 0x2000 && addy < 0x3FFF) {
    uint16_t bank = (uint16_t)(val & 0x7F);
    if (bank == 0) bank = 1;
    if (cart->rom_banks) {
      bank %= cart->rom_banks;
      if (bank == 0 && cart->rom_banks > 1) bank = 1;
      if (cart->rom_banks == 1) bank = 0;
    }
    cart->rom_bank = (uint8_t)bank;
    return;
  }

  if (addy >= 0x4000 && addy <= 0x5FFF) {
    cart->ram_bank = val;
    return;
  }
  if (addy >= 0x6000 && addy <= 0x7FFF) {
    uint8_t latch_val = val & 0x01u;
    if (cart->rtc_latch_prev == 0 && latch_val == 1) {
      mbc3_rtc_tick(cart);
      memcpy(cart->rtc_latched_regs, cart->rtc_regs, sizeof(cart->rtc_regs));
      cart->rtc_latched = true;
    }
    if (latch_val == 0) {
      cart->rtc_latched = false;
    }
    cart->rtc_latch_prev = latch_val;
    return;
  }

  if (addy >= 0xA000 && addy <= 0xBFFF && cart->ram_enable) {
    if (cart->ram_bank <= 3 && cart->ram) {
      uint8_t bank = cart->ram_bank;
      uint16_t effective = cart->ram_banks ? cart->ram_banks : 1;
      bank %= effective;
      size_t off = ((size_t)bank * 0x2000u) + (addy - 0xA000);
      if (off < cart->ram_size)
	cart->ram[off] = val;
    } else if (cart->ram_bank >= 0x08 && cart->ram_bank <= 0x0C) {
      mbc3_rtc_tick(cart);

      uint16_t days;
      uint8_t hours, minutes, seconds;
      mbc3_rtc_get_components(cart, &days, &hours, &minutes, &seconds);

      switch (cart->ram_bank) {
	case 0x08:
	  seconds = (uint8_t)(val % 60u);
	  break;
	case 0x09:
	  minutes = (uint8_t)(val % 60u);
	  break;
	case 0x0A:
	  hours = (uint8_t)(val % 24u);
	  break;
	case 0x0B:
	  days = (uint16_t)((days & 0x100u) | val);
	  days %= MBC3_DAY_MAX;
	  break;
	case 0x0C: {
	  uint16_t new_days = (uint16_t)(((uint16_t)(val & 0x01u) << 8) | (days & 0xFFu));
	  days = new_days % MBC3_DAY_MAX;

	  bool halt = (val & 0x40u) != 0;
	  if (cart->rtc_halt != halt) {
	    cart->rtc_last_update = time(NULL);
	    if (cart->rtc_last_update == (time_t)-1) cart->rtc_last_update = 0;
	  }
	  cart->rtc_halt = halt;
	  cart->rtc_day_carry = (val & 0x80u) != 0;
	  break;
	}
	default:
	  break;
      }

      mbc3_rtc_set_components(cart, days, hours, minutes, seconds);
    }
  }
}

uint8_t read_mbc5(Cartridge_t *cart, uint16_t addy) {
  if (addy < 0x4000) {
    return read_mbc_bytes(cart, 0, addy);
  }

  if (addy >= 0x4000 && addy <= 0x7FFF) {
    uint16_t bank = (uint16_t)cart->rom_bank | ((uint16_t)(cart->mode & 0x01) << 8);

    if (bank >= cart->rom_banks) 
      bank %= cart->rom_banks;

    return read_mbc_bytes(cart, bank, addy);
  }

  if (addy >= 0xA000 && addy <= 0xBFFF) {
    if (!cart->ram || !cart->ram_enable) 
      return 0xFF;

    uint32_t bank = (uint32_t)(cart->ram_bank & 0x0F);
    
    bank %= (cart->ram_banks ? cart->ram_banks : 1);
    
    size_t off = bank * 0x2000u + (addy - 0xA000);
    return (off < cart->ram_size) ? cart->ram[off] : 0xFF;
  }

  return 0xFF;
}

void write_mbc5(Cartridge_t *cart, uint16_t addy, uint8_t val) {
  if (addy < 0x2000) {
    cart->ram_enable = ((val & 0x0F) == 0x0A);
    return;
  }

  if (addy >= 0x2000 && addy <= 0x2FFF) {
    cart->rom_bank = val;
    return;
  }

  if (addy >= 0x3000 && addy <= 0x3FFF) {
    cart->mode = val & 0x01;
    return;
  }

  if (addy >= 0x4000 && addy <= 0x5FFF) {
    cart->ram_bank = val & 0x0F;
    return;
  }

  if (addy >= 0xA000 && addy <= 0xBFFF) {
    if (!cart->ram || !cart->ram_enable) return;

    uint32_t bank = (uint32_t)(cart->ram_bank & 0x0F);
    bank %= (cart->ram_banks ? cart->ram_banks : 1);

    size_t off = bank * 0x2000u + (addy - 0xA000);
    if (off < cart->ram_size) cart->ram[off] = val;
  }
}

uint8_t cart_read(Cartridge_t *cart, uint16_t addy) {
  switch (cart->type) {
    case MBC_0:
      return read_mbc0(cart, addy);
    case MBC_1:
      return read_mbc1(cart, addy);
    case MBC_3:
      return read_mbc3(cart, addy);
    case MBC_5:
      return read_mbc5(cart, addy);
    default:
      return read_mbc0(cart, addy);
  }
}

void cart_write(Cartridge_t *cart, uint16_t addy, uint8_t val) {
  switch (cart->type) {
    case MBC_0:
      return write_mbc0(cart, addy, val);
    case MBC_1:
      return write_mbc1(cart, addy, val);
    case MBC_3:
      return write_mbc3(cart, addy, val);
    case MBC_5:
      return write_mbc5(cart, addy, val);
    default:
      return write_mbc0(cart, addy, val);
  }
}


