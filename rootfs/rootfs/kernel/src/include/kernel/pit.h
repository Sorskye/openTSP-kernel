#ifndef PIT_H
#define PIT_H

#include "types.h"


void pit_init(uint32_t frequency);
uint32_t get_pit_frequency();

#endif