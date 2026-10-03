
#ifndef IO_H
#define IO_H
#include "types.h"

#include "boot.h"

void outb(uint16_t port, uint8_t value);
uint8_t inb(uint16_t port);
void outl(uint16_t port, uint32_t value);
uint32_t inl(uint16_t port);


BOOT void outb_bs(uint16_t port, uint8_t val);
BOOT uint8_t inb_bs(uint16_t port);

#endif