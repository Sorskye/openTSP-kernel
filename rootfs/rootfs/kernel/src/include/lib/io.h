
#ifndef IO_H
#define IO_H
#include "types.h"
static inline void outb( uint16_t port, uint8_t value);
static inline uint8_t inb(uint16_t port);
static inline void outl(uint16_t port, uint32_t value);
static inline uint32_t inl(uint16_t port);
#endif