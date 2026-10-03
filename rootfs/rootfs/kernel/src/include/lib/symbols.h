#pragma once

#include "types.h"

typedef struct {
    uint32_t addr;
    const char *name;
} symbol_t;

extern symbol_t kernel_symbols[];
extern int kernel_symbol_count;
