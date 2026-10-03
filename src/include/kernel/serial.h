#pragma once
#ifndef SERIAL_H
#define SERIAL_H
#include "types.h"
#include "boot.h"

void serial_print(const char *fmt, ...);
void serial_write(char c);
void serial_print_hex(uint32_t value, int width);
void serial_mark();

void init_serial();

BOOT void serial_print_bs(const char *fmt, ...);
#endif