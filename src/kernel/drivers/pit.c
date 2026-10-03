#include "io.h"
#include "types.h"
#include "pit.h"
#define PIT_CMD 0x43
#define PIT_CH0 0x40

static uint32_t PIT_FREQUENCY = 100;

uint32_t get_pit_frequency(){
    return PIT_FREQUENCY;
}

void pit_init(uint32_t frequency) {
    uint32_t divisor = 1193182 / frequency;

    // set frequency
    outb(PIT_CMD, 0x34);
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);

    PIT_FREQUENCY = frequency;
}

