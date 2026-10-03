#pragma once
#ifndef SERIAL_H
#define SERIAL_H
#include "types.h"
void serial_print(const char *fmt, ...);
void serial_write(char c);
void serial_print_hex(uint32_t value, int width);
#endif