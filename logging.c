#include "logging.h"
#include <string.h>
#include <stdlib.h>

static FILE *log_file = NULL;
static char log_filename[256] = "log.txt";

static void init_log_file(void) {
  if (log_file == NULL) {
    log_file = fopen(log_filename, "w");
    if (log_file == NULL) {
      fprintf(stderr, "[LOG] Warning: Failed to open %s, logging to stderr\n",
              log_filename);
      log_file = stderr;
    } else {
      fprintf(stderr, "[LOG] Logging to %s\n", log_filename);
    }
  }
}

void set_log_file(const char *filename) {
  if (log_file != NULL && log_file != stderr) {
    fclose(log_file);
    log_file = NULL;
  }
  strncpy(log_filename, filename, sizeof(log_filename) - 1);
  log_filename[sizeof(log_filename) - 1] = '\0';
}

void close_log_file(void) {
    if (log_file != NULL && log_file != stderr) {
        fclose(log_file);
        log_file = NULL;
    }
}

void write_log(const char *format, ...) {
  init_log_file();

  va_list args;
  va_start(args, format);
  vfprintf(log_file, format, args);
  va_end(args);
  fflush(log_file);
}

static int write_binary_file(const void *data, size_t size,
                             const char *filename) {
  FILE *f = fopen(filename, "wb");
  if (!f) {
    write_log("[LOG] Failed to open %s for writing\n", filename);
    return -1;
  }

  size_t written = fwrite(data, 1, size, f);
  fclose(f);

  if (written != size) {
    write_log("[LOG] Failed to write all data to %s (wrote %zu/%zu bytes)\n",
              filename, written, size);
    return -1;
  }

  write_log("[LOG] Dumped %zu bytes to %s\n", size, filename);
  return 0;
}

void dump_cpu(const registers_t *cpu, const char *filename) {
  FILE *f = fopen(filename, "wb");
  if (!f) {
    write_log("[LOG] Failed to open %s for writing\n", filename);
    return;
  }

  fwrite(&cpu->A, sizeof(uint8_t), 1, f);
  fwrite(&cpu->BC, sizeof(uint16_t), 1, f);
  fwrite(&cpu->DE, sizeof(uint16_t), 1, f);
  fwrite(&cpu->HL, sizeof(uint16_t), 1, f);
  fwrite(&cpu->SP, sizeof(uint16_t), 1, f);
  fwrite(&cpu->PC, sizeof(uint16_t), 1, f);
  fwrite(&cpu->F, sizeof(cpu->F), 1, f);
  fwrite(&cpu->cycle, sizeof(unsigned long), 1, f);
  fwrite(&cpu->stopped, sizeof(bool), 1, f);
  fwrite(&cpu->halt, sizeof(bool), 1, f);
  fwrite(&cpu->halt_bug, sizeof(bool), 1, f);
  fwrite(&cpu->IME, sizeof(bool), 1, f);
  fwrite(&cpu->ime_pending, sizeof(bool), 1, f);

  fclose(f);
  write_log("[LOG] Dumped CPU state to %s\n", filename);
}

void dump_vram(const Bus_t *bus, const char *filename) {
  write_binary_file(bus->vram, sizeof(bus->vram), filename);
}

void dump_wram(const Bus_t *bus, const char *filename) {
  write_binary_file(bus->wram, sizeof(bus->wram), filename);
}

void dump_hram(const Bus_t *bus, const char *filename) {
  write_binary_file(bus->hram, sizeof(bus->hram), filename);
}

void dump_oam(const Bus_t *bus, const char *filename) {
  write_binary_file(bus->oam, sizeof(bus->oam), filename);
}

void dump_rom(const Bus_t *bus, const char *filename) {
  write_binary_file(bus->rom, sizeof(bus->rom), filename);
}

void dump_memory_range(const Bus_t *bus, uint16_t start, uint16_t end, const char *filename) {
  if (start > end) {
    write_log("[LOG] Invalid memory range: start (0x%04X) > end (0x%04X)\n",
              start, end);
    return;
  }

  size_t size = (end - start) + 1;
  uint8_t *buffer = malloc(size);
  if (!buffer) {
    write_log("[LOG] Failed to allocate memory for dump\n");
    return;
  }

  for (uint16_t addr = start; addr <= end; addr++) {
    buffer[addr - start] = read_byte_bus((Bus_t *)bus, addr);
  }

  int result = write_binary_file(buffer, size, filename);
  free(buffer);

  if (result == 0) {
    write_log("[LOG] Dumped memory range 0x%04X-0x%04X (%zu bytes) to %s\n",
              start, end, size, filename);
  }
}

void dump_all(const registers_t *cpu, const Bus_t *bus) {
  write_log("[LOG] Dumping all memory and CPU state...\n");
  dump_cpu(cpu, "cpu.bin");
  dump_vram(bus, "vram.bin");
  dump_wram(bus, "wram.bin");
  dump_hram(bus, "hram.bin");
  dump_oam(bus, "oam.bin");
  dump_rom(bus, "rom.bin");

  dump_memory_range(bus, 0x0000, 0xFFFF, "memory.bin");
  write_log("[LOG] All dumps complete\n");
}
