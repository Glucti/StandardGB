#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "ppu.h"
#include "memory.h"
#include "mbc.h"
#include "logging.h"


uint32_t bw_palette[4] = {
    0xC4CFA1, 0x8B956D, 0x4D533C, 0x1F1F1F
};

uint32_t sgb_palette[4] = {
    0xFFFFFF, 
    0xFFB3D9, 
    0xFFFFFF,
    0x000000 
};

static inline uint8_t read_vram_bank(Bus_t *bus, uint16_t addr, uint8_t bank) {
  uint16_t offset = addr - 0x8000;
  uint16_t real_addr = offset + (bank * 0x2000);
  if (real_addr >= 0x4000) return 0xFF;
  return bus->vram[real_addr];
}

static inline uint32_t cgb_2_rgb(uint8_t *pallete, uint8_t pallete_num, uint8_t color_id) {
  int offset = (pallete_num * 8) + (color_id * 2);
  
  uint16_t rgb555 = pallete[offset] | (pallete[offset + 1] << 8);
  
  
  uint8_t r = (rgb555 & 0x1F);           
  uint8_t g = (rgb555 >> 5) & 0x1F;     
  uint8_t b = (rgb555 >> 10) & 0x1F;     
  
  r = (r << 3) | (r >> 2);
  g = (g << 3) | (g >> 2);
  b = (b << 3) | (b >> 2);
  
  return (r << 16) | (g << 8) | b;
}

static void init_default_palettes(Ppu_t *display) {
  uint16_t default_bg_colors[4] = {
    0x7FFF,
    0x56B5,
    0x294A,
    0x0000
  };

  uint16_t default_obj_colors[8] = {
    0x7FFF,
    0x7E10,
    0x48E7,
    0x0000,
    0x7FFF,
    0x3FE6,
    0x0200,
    0x0000
  };

  for (int pal = 0; pal < 8; pal++) {
    for (int col = 0; col < 4; col++) {
      int offset = (pal * 8) + (col * 2);
      uint16_t color = default_bg_colors[col];
      display->bg_pallete[offset] = color & 0xFF;
      display->bg_pallete[offset + 1] = (color >> 8) & 0xFF;
    }
  }

  for (int pal = 0; pal < 8; pal++) {
    for (int col = 0; col < 4; col++) {
      int offset = (pal * 8) + (col * 2);
      uint16_t color = (pal < 2) ? default_obj_colors[col + (pal * 4)] : default_obj_colors[col];
      display->obj_pallete[offset] = color & 0xFF;
      display->obj_pallete[offset + 1] = (color >> 8) & 0xFF;
    }
  }
}

void start_display(Ppu_t *display, Bus_t *bus, int scale) {
  memset(display, 0, sizeof(Ppu_t));
  display->bus = bus;

  display->LCDC = 0x91;
  display->SCY = 0;
  display->SCX = 0;
  display->LY = 0;
  display->LYC = 0;
  display->BGP = 0xFC;
  display->OBP0 = 0xFF;
  display->OBP1 = 0xFF;
  display->WY = 0;
  display->WX = 0;

  // CGB HDMA defaults
  display->HDMA1 = display->HDMA2 = display->HDMA3 = display->HDMA4 = display->HDMA5 = 0xFF;
  display->hdma_active = false;
  display->hdma_src = display->hdma_dst = 0;
  display->hdma_remaining = 0;
  
  init_default_palettes(display);
  
  display->BCPS = 0;
  display->OCPS = 0;

  // Use SGB palette for SGB-enhanced games, otherwise use DMG palette
  uint32_t *palette_to_use = (bus->cartridge && bus->cartridge->is_sgb && !bus->cartridge->is_cgb) 
                              ? sgb_palette : bw_palette;
  
  for (int i = 0; i <= 3; i++) {
    display->pallete[i] = palette_to_use[i];
  }

  display->framebuffer = (uint32_t*)calloc(GB_WIDTH*GB_HEIGHT, 4);
  display->background_buffer = (uint32_t*)calloc(256*256, 4);
  if (scale <= 1) {
    display->scaled_framebuffer = display->framebuffer;
  } else {
    display->scaled_framebuffer = (uint32_t*)calloc(GB_WIDTH*GB_HEIGHT, 4*scale*scale);
  }
}

static uint8_t bg_tile_attrs[GB_WIDTH];
static uint8_t bg_color_ids[GB_WIDTH];

static void hdma_transfer1(Ppu_t *d, Bus_t *b) {
  if (!d->hdma_active || !b || !b->cartridge) return;
  if (d->hdma_remaining == 0) {
    d->hdma_active = false;
    d->HDMA5 = 0xFF;
    return;
  }

  uint16_t src = d->hdma_src;
  uint16_t dst = d->hdma_dst;

  for (int i = 0; i < 0x10; i++) {
    uint8_t val = read_byte_bus(b, src++);
    // Destination is always in VRAM region 0x8000-0x9FF0
    if (dst >= 0x8000 && dst < 0xA000) {
      write_byte_bus(b, dst, val);
    }
    dst++;
  }

  d->hdma_src = src;
  d->hdma_dst = dst;

  if (d->hdma_remaining <= 0x10) {
    d->hdma_remaining = 0;
    d->hdma_active = false;
    d->HDMA5 = 0xFF;
  } else {
    d->hdma_remaining -= 0x10;
    uint8_t blocks = (uint8_t)(d->hdma_remaining / 0x10);
    if (blocks) blocks -= 1;
    d->HDMA5 = blocks & 0x7F;
  }
}

static void render_bg_scanline(Ppu_t *d) {
  if (!d->bus->is_cgb && !(d->LCDC & 0x01))
    return;

  uint16_t tile_data_addr = (d->LCDC & 0x10) ? 0x8000 : 0x8800;
  uint16_t bg_map_addr = (d->LCDC & 0x08) ? 0x9C00 : 0x9800;

  int y = (d->SCY + d->LY) & 0xFF;

  for (int x = 0; x < GB_WIDTH; x++) {
    int scx = (d->SCX + x) & 0xFF;
    uint16_t map_index = bg_map_addr + ((y / 8) * 32) + (scx / 8);
    uint8_t tile_num = read_byte_bus(d->bus, map_index);

    uint8_t tile_attr = 0;
    if (d->bus->is_cgb) {
      tile_attr = read_vram_bank(d->bus, map_index, 1);
    }

    uint16_t tile_addr;
    if (d->LCDC & 0x10)
      tile_addr = tile_data_addr + (tile_num * 16);
    else
      tile_addr = tile_data_addr + ((int8_t)tile_num + 128) * 16;

    int line = y % 8;
    if (d->bus->is_cgb && (tile_attr & 0x40)) {
      line = 7 - line;
    }

    uint8_t low, high;
    if (d->bus->is_cgb && (tile_attr & 0x08)) {
      low = read_vram_bank(d->bus, tile_addr + (line * 2), 1);
      high = read_vram_bank(d->bus, tile_addr + (line * 2) + 1, 1);
    } else {
      low = read_vram_bank(d->bus, tile_addr + (line * 2), 0);
      high = read_vram_bank(d->bus, tile_addr + (line * 2) + 1, 0);
    }

    int bit = 7 - (scx % 8);
    if (d->bus->is_cgb && (tile_attr & 0x20)) {
      bit = scx % 8;
    }

    int color_id = ((high >> bit) & 1) << 1 | ((low >> bit) & 1);

    bg_tile_attrs[x] = tile_attr;
    bg_color_ids[x] = color_id;

    uint32_t color;
    if (d->bus->is_cgb) {
      uint8_t palette_num = tile_attr & 0x07;
      color = cgb_2_rgb(d->bg_pallete, palette_num, color_id);
    } else {
      color = d->pallete[(d->BGP >> (color_id * 2)) & 3];
    }
    d->framebuffer[d->LY * GB_WIDTH + x] = 0xFF000000 | color;
  }
}

static void render_window_scanline(Ppu_t *d) {
  if (!(d->LCDC & 0x20))
    return;

  if (d->WY > d->LY)
    return;

  uint16_t tile_data_addr = (d->LCDC & 0x10) ? 0x8000 : 0x8800;
  uint16_t win_map_addr = (d->LCDC & 0x40) ? 0x9C00 : 0x9800;
  int win_y = d->LY - d->WY;

  for (int x = 0; x < GB_WIDTH; x++) {
    int win_x = x - (d->WX - 7);

    if (win_x < 0)
      continue;

    uint16_t map_index = win_map_addr + ((win_y / 8) * 32) + (win_x / 8);
    uint8_t tile_num = read_byte_bus(d->bus, map_index);

    uint8_t tile_attr = 0;
    if (d->bus->is_cgb) {
      tile_attr = read_vram_bank(d->bus, map_index, 1);
    }

    uint16_t tile_addr;
    if (d->LCDC & 0x10)
      tile_addr = tile_data_addr + (tile_num * 16);
    else
      tile_addr = tile_data_addr + ((int8_t)tile_num + 128) * 16;

    int line = win_y % 8;
    if (d->bus->is_cgb && (tile_attr & 0x40)) {
      line = 7 - line;
    }

    uint8_t low, high;
    if (d->bus->is_cgb && (tile_attr & 0x08)) {
      low = read_vram_bank(d->bus, tile_addr + (line * 2), 1);
      high = read_vram_bank(d->bus, tile_addr + (line * 2) + 1, 1);
    } else {
      low = read_vram_bank(d->bus, tile_addr + (line * 2), 0);
      high = read_vram_bank(d->bus, tile_addr + (line * 2) + 1, 0);
    }

    int bit = 7 - (win_x % 8);
    if (d->bus->is_cgb && (tile_attr & 0x20)) {
      bit = win_x % 8;
    }

    int color_id = ((high >> bit) & 1) << 1 | ((low >> bit) & 1);

    bg_tile_attrs[x] = tile_attr;
    bg_color_ids[x] = color_id;

    uint32_t color;
    if (d->bus->is_cgb) {
      uint8_t palette_num = tile_attr & 0x07;
      color = cgb_2_rgb(d->bg_pallete, palette_num, color_id);
    } else {
      color = d->pallete[(d->BGP >> (color_id * 2)) & 3];
    }
    d->framebuffer[d->LY * GB_WIDTH + x] = 0xFF000000 | color;
  }
}

static void render_sprites_scanline(Ppu_t *d) {
  if (!(d->LCDC & 0x02))
    return;

  int sprite_height = (d->LCDC & 0x04) ? 16 : 8;
  int sprites_drawn = 0;
  
  static int pixels_drawn = 0;
  static int pixels_skipped_priority = 0;
  static int pixels_skipped_bg = 0;
  
  for (int i = 39; i >= 0; i--) {
    int oam_addr = i * 4;
    uint8_t oam_y = d->bus->oam[oam_addr];
    uint8_t oam_x = d->bus->oam[oam_addr + 1];
    uint8_t tile_num = d->bus->oam[oam_addr + 2];
    uint8_t attributes = d->bus->oam[oam_addr + 3];

    if (oam_y == 0 || oam_y >= 160) continue;
    
    int sprite_y = oam_y - 16;
    int sprite_x = oam_x - 8;

    int sprite_top = sprite_y;
    int sprite_bottom = sprite_y + sprite_height;
    
    if (d->LY < sprite_top || d->LY >= sprite_bottom)
      continue;

    if (sprites_drawn >= 10)
      continue;
    
    
    sprites_drawn++;

    int palette = (attributes & 0x10) ? d->OBP1 : d->OBP0;
    int flip_x = attributes & 0x20;
    int flip_y = attributes & 0x40;
    int priority = attributes & 0x80;
    
    uint8_t sprite_palette = 0;
    if (d->bus->is_cgb) {
      sprite_palette = attributes & 0x07;
    }

    int line = d->LY - sprite_y;
    
    if (line < 0 || line >= sprite_height) {
      continue;
    }
    
    if (flip_y)
      line = sprite_height - 1 - line;

    if (sprite_height == 16) {
      tile_num &= 0xFE; 
      if (line >= 8) {
        tile_num |= 0x01;
        line -= 8;
      }
    }

    uint16_t tile_addr = 0x8000 + (tile_num * 16);
    
    uint8_t low, high;
    if (d->bus->is_cgb && (attributes & 0x08)) {
      low = read_vram_bank(d->bus, tile_addr + (line * 2), 1);
      high = read_vram_bank(d->bus, tile_addr + (line * 2) + 1, 1);
    } else {
      low = read_vram_bank(d->bus, tile_addr + (line * 2), 0);
      high = read_vram_bank(d->bus, tile_addr + (line * 2) + 1, 0);
    }

    for (int px = 0; px < 8; px++) {
      int screen_x = sprite_x + px;

      if (screen_x < 0 || screen_x >= GB_WIDTH)
        continue;

      int bit = flip_x ? px : (7 - px);
      int color_id = ((high >> bit) & 1) << 1 | ((low >> bit) & 1);

      if (color_id == 0)
        continue;

      int fb_index = d->LY * GB_WIDTH + screen_x;
      
      if (d->bus->is_cgb) {
        uint8_t bg_tile_attr = bg_tile_attrs[screen_x];
        uint8_t bg_color_id = bg_color_ids[screen_x];
        
        bool bg_priority = (bg_tile_attr & 0x80) != 0;
        bool lcdc_bg_enable = (d->LCDC & 0x01) != 0;
        
        if (lcdc_bg_enable && bg_priority && bg_color_id != 0) {
          if (d->LY == 80) pixels_skipped_bg++;
          continue;
        }
        
        if (lcdc_bg_enable && priority && bg_color_id != 0) {
          if (d->LY == 80) pixels_skipped_priority++;
          continue;
        }
        
        if (d->LY == 80) pixels_drawn++;
        
        uint32_t color = cgb_2_rgb(d->obj_pallete, sprite_palette, color_id);
        d->framebuffer[fb_index] = 0xFF000000 | color;
      } else {
        if (priority) {
          uint32_t bg_pixel = d->framebuffer[fb_index];
          uint32_t bg_color = bg_pixel & 0x00FFFFFF;
          uint32_t bg_color0 = d->pallete[0] & 0x00FFFFFF;
          if (bg_color != bg_color0) {
            continue;
          }
        }
        int shade = (palette >> (color_id * 2)) & 0x03;
        uint32_t color = d->pallete[shade];
        d->framebuffer[fb_index] = 0xFF000000 | color;
      }
    }
  }
}

void display_cycle(Ppu_t *d, Bus_t *b, int cycles) {
  if (!(d->LCDC & LCDC_ENABLE))
    return;
  // fprintf(stderr, "[LCDC=%02X SCX=%02X SCY=%02X]\n", d->LCDC, d->SCX,
  // d->SCY);

  d->cycles_in_line += cycles;

  if (d->dma_pending) {
    d->dma_pending = false;
    d->dma_active = true;
    d->dma_counter = 0;
    d->dma_source = ((uint16_t)d->DMA) << 8;
  }
  static int dma_cycle_counter = 0;
  
  if (d->dma_active) {
    dma_cycle_counter += cycles;
    
    while (dma_cycle_counter >= 4 && d->dma_counter < 160) {
      dma_cycle_counter -= 4;
      
      uint16_t src_addr = d->dma_source + d->dma_counter;
      uint8_t byte;
      
      if (src_addr < 0x8000) {
        byte = cart_read(b->cartridge, src_addr);
      } else if (src_addr >= 0x8000 && src_addr < 0xA000) {
        byte = read_byte_bus(b, src_addr);
      } else if (src_addr >= 0xA000 && src_addr < 0xC000) {
        // cart
        byte = cart_read(b->cartridge, src_addr);
      } else if (src_addr >= 0xC000 && src_addr < 0xE000) {
        byte = b->wram[src_addr - 0xC000];
      } else if (src_addr >= 0xE000 && src_addr < 0xFE00) {
        byte = b->wram[src_addr - 0xE000];
      } else if (src_addr >= 0xFF80 && src_addr <= 0xFFFE) {
        byte = b->hram[src_addr - 0xFF80];
      } else {
        byte = 0xFF;
      }
      
      b->oam[d->dma_counter] = byte;
      d->dma_counter++;
    }
    
    if (d->dma_counter >= 160) {
      d->dma_active = false;
      d->dma_counter = 0;
      dma_cycle_counter = 0;
    }
  } else {
    dma_cycle_counter = 0;
  }

  if (d->cycles_in_line >= 456) {
    d->cycles_in_line -= 456;

    d->LY++;

    if (d->LY > 153) {
      d->LY = 0;
    }

    if (d->LY == 144) {
      d->STAT = (d->STAT & ~0x03) | 1; // mode 1 = VBlank
      b->IF |= 0x01;                   // request VBlank interrupt

      if (d->STAT & 0x10) // STAT bit 4 = VBlank interrupt enable
        b->IF |= 0x02;
      d->frame_ready = true;
    } else if (d->LY < 144) {
      d->STAT = (d->STAT & ~0x03) | 2;
      if (d->STAT & 0x20)
        b->IF |= 0x02;
    }

    if (d->LY == d->LYC) {
      d->STAT |= 0x04;
      if (d->STAT & 0x40)
        b->IF |= 0x02;
    } else {
      d->STAT &= ~0x04;
    }

    // render the newly-started scanline.
    if (d->LY < 144) {
      uint32_t bg_color = d->pallete[0];
      for (int x = 0; x < GB_WIDTH; x++) {
        d->framebuffer[d->LY * GB_WIDTH + x] = 0xFF000000 | bg_color;
        bg_tile_attrs[x] = 0;
        bg_color_ids[x] = 0;
      }
      render_bg_scanline(d);
      render_window_scanline(d);
      render_sprites_scanline(d);
    }
  }

  if (d->LY < 144) {
    if (d->cycles_in_line < 80) {
      // mode 2: OAM scan
      d->STAT = (d->STAT & ~0x03) | 2;
    } else if (d->cycles_in_line < 252) {
      // mode 3: drawing (OAM+VRAM)
      d->STAT = (d->STAT & ~0x03) | 3;
    } else {
      // mode 0: HBlank
      if ((d->STAT & 0x03) != 0) {
        // CGB HBlank HDMA transfer
        if (d->hdma_active) {
          hdma_transfer1(d, b);
        }
        if (d->STAT & 0x08) 
          b->IF |= 0x02;
      }
      d->STAT = (d->STAT & ~0x03) | 0;
    }
  }
}

bool ppu_is_mode2(Ppu_t *ppu) {
  if (!ppu) return false;
  // Mode 2 = OAM scan (STAT bits 0-1 == 2)
  return (ppu->STAT & 0x03) == 2;
}

uint8_t ppu_vram_read(Ppu_t *ppu, uint16_t addr) {
  if (!ppu || !ppu->bus)
    return 0xFF;

  if (addr >= 0x4000u)
    return 0xFF;

  return ppu->bus->vram[addr];
}

void ppu_vram_write(Ppu_t *ppu, uint16_t addr, uint8_t byte) {
  if (!ppu || !ppu->bus)
    return;
  if (addr >= 0x4000u)
    return;
  ppu->bus->vram[addr] = byte;
}

