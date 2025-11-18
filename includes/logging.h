#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cpu.h"
#include "memory.h"

void write_log(const char *format, ...);
void set_log_file(const char *filename);
void close_log_file(void);
void dump_cpu(const registers_t *cpu, const char *filename);
void dump_vram(const Bus_t *bus, const char *filename);
void dump_wram(const Bus_t *bus, const char *filename);
void dump_hram(const Bus_t *bus, const char *filename);
void dump_oam(const Bus_t *bus, const char *filename);
void dump_rom(const Bus_t *bus, const char *filename);
void dump_memory_range(const Bus_t *bus, uint16_t start, uint16_t end, const char *filename);
void dump_all(const registers_t *cpu, const Bus_t *bus);

