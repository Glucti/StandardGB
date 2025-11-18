#pragma once
#include <stdint.h> 
#include <stdbool.h> 

#define GB_WIDTH 160
#define GB_HEIGHT 144
#define OAM_SIZE 160

#define LCDC_ENABLE 0x80

typedef struct Bus Bus_t;

enum {
  LCDC=0xFF40, STAT=0xFF41, SCY=0xFF42, SCX=0xFF43, LY=0xFF44, LYC=0xFF45,
  DMA =0xFF46, BGP=0xFF47, OBP0=0xFF48, OBP1=0xFF49, WY=0xFF4A, WX=0xFF4B,
};

typedef struct Ppu {
  uint8_t LCDC, LY, LYC, STAT, SCY, SCX, BGP, OBP0, OBP1, WY, WX;

  uint32_t *framebuffer;
  uint32_t *scaled_framebuffer;
  uint32_t *background_buffer;

  uint32_t pallete[4];


  //gbc
  uint8_t bg_pallete[64];
  uint8_t obj_pallete[64];
  uint8_t BCPS;
  uint8_t OCPS;

  int cycles_in_line;
  int mode;

  // CGB VRAM DMA (HDMA) state
  uint8_t HDMA1, HDMA2, HDMA3, HDMA4, HDMA5;
  bool hdma_active;
  uint16_t hdma_src;
  uint16_t hdma_dst;
  uint16_t hdma_remaining; // bytes remaining

  Bus_t *bus;
  uint8_t DMA; 
  bool dma_pending;
  bool dma_active;
  uint8_t dma_counter;
  uint16_t dma_source;
  bool frame_ready;
} Ppu_t;

void start_display(Ppu_t *display, Bus_t *bus, int scale);
void display_cycle(Ppu_t *d, Bus_t *b, int cycles);
uint8_t ppu_vram_read(Ppu_t *ppu, uint16_t addr);
void ppu_vram_write(Ppu_t *ppu, uint16_t addr, uint8_t byte);
bool ppu_is_mode2(Ppu_t *ppu);
