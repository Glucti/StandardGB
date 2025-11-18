#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"
#include "ppu.h"
#include "memory.h"
#include <SDL2/SDL.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s rom.gb [bootrom.bin]\n", argv[0]);
    return 1;
  }

  Bus_t *bus = malloc(sizeof(Bus_t));
  init_bus(bus);

  if (bus_load_rom(bus, argv[1]) != 0)
    return 1;

  if (!bus->cartridge) {
    fprintf(stderr, "[ROM] failed to load '%s'\n", argv[1]);
  }

  Ppu_t *ppu = malloc(sizeof(Ppu_t));
  start_display(ppu, bus, 4);

  bus->ppu = ppu;

  registers_t cpu;
  RESET_CPU(&cpu);
  cpu.bus = bus;
  cpu.ppu = ppu;

  if (bus->bootrom_enabled && bus->bootrom) {
    cpu.PC = 0x0000;
    cpu.IME = 0;
  } else {
    if (bus->is_cgb) {
      cpu.A = 0x11;
      cpu.BC = 0x0013;
      cpu.DE = 0x00D8;
      cpu.HL = 0x014D;
      cpu.F.Z = 1;
      cpu.F.N = 0;
      cpu.F.H = 1;
      cpu.F.C = 0;
    } else {
      cpu.A = 0x01;
      cpu.BC = 0x0013;
      cpu.DE = 0x00D8;
      cpu.HL = 0x014D;
      cpu.F.Z = 1;
      cpu.F.N = 0;
      cpu.F.H = 1;
      cpu.F.C = 1;
    }
    cpu.PC = 0x0100;
    cpu.SP = 0xFFFE;
  }

  // sdl
  int scale = 4;
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *win = SDL_CreateWindow("Game Boy", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, GB_WIDTH * scale,
                                     GB_HEIGHT * scale, 0);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                        SDL_TEXTUREACCESS_STREAMING, GB_WIDTH, GB_HEIGHT);

  bool running = true;

  bus->buttons_dir = 0x0F;   
  bus->buttons_action = 0x0F; 

  uint32_t last_frame_time = SDL_GetTicks();
  const uint32_t frame_duration = 20; 

  while (running) {
    while (!ppu->frame_ready && running) {
      helper(&cpu);
    }
    
    SDL_UpdateTexture(tex, NULL, ppu->framebuffer,
                      GB_WIDTH * sizeof(uint32_t));
    
    ppu->frame_ready = false;
    
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      
      if (e.type == SDL_QUIT)
        running = false;
      
      if (e.type == SDL_KEYDOWN) {
        if (!e.key.repeat) {
          uint8_t old_dir = bus->buttons_dir;
          uint8_t old_action = bus->buttons_action;
        
        // Direction buttons (0=pressed, 1=released)
        if (e.key.keysym.sym == SDLK_RIGHT) {
          bus->buttons_dir &= ~0x01;
        }
        if (e.key.keysym.sym == SDLK_LEFT) {
          bus->buttons_dir &= ~0x02;
        }
        if (e.key.keysym.sym == SDLK_UP) {
          bus->buttons_dir &= ~0x04;
        }
        if (e.key.keysym.sym == SDLK_DOWN) {
          bus->buttons_dir &= ~0x08;
        }
        
        if (e.key.keysym.sym == SDLK_x) {
          bus->buttons_action &= ~0x01;
        }
        if (e.key.keysym.sym == SDLK_z) {
          bus->buttons_action &= ~0x02;
        }
        if (e.key.keysym.sym == SDLK_RSHIFT) {
          bus->buttons_action &= ~0x04;
        }
        if (e.key.keysym.sym == SDLK_RETURN) {
          bus->buttons_action &= ~0x08;
        }

        if ((bus->buttons_dir != old_dir) || (bus->buttons_action != old_action)) {
          bus->IF |= 0x10; // JOYP interrupt
          }
        }
      }

      if (e.type == SDL_KEYUP) {
        
        if (e.key.keysym.sym == SDLK_RIGHT) {
          bus->buttons_dir |= 0x01;
        }
        if (e.key.keysym.sym == SDLK_LEFT) {
          bus->buttons_dir |= 0x02;
        }
        if (e.key.keysym.sym == SDLK_UP) {
          bus->buttons_dir |= 0x04;
        }
        if (e.key.keysym.sym == SDLK_DOWN) {
          bus->buttons_dir |= 0x08;
        }
        if (e.key.keysym.sym == SDLK_x) {
          bus->buttons_action |= 0x01;
        }
        if (e.key.keysym.sym == SDLK_z) {
          bus->buttons_action |= 0x02;
        }
        if (e.key.keysym.sym == SDLK_RSHIFT) {
          bus->buttons_action |= 0x04;
        }
        if (e.key.keysym.sym == SDLK_RETURN) {
          bus->buttons_action |= 0x08;
        }
      }
    }

    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);

    //throttle
    uint32_t now = SDL_GetTicks();
    uint32_t elapsed = now - last_frame_time;
    if (elapsed < frame_duration) {
      SDL_Delay(frame_duration - elapsed);
    }
    last_frame_time = now;
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();

    return 0;
}
