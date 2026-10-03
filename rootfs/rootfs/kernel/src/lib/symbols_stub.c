#include "symbols.h"

// Weak empty table used for the FIRST PASS only.
// These symbols get replaced in the second pass when symbol.c is generated.

__attribute__((weak)) symbol_t kernel_symbols[] = { {0, 0} };
__attribute__((weak)) int kernel_symbol_count = 0;
