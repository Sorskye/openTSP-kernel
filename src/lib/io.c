#include "types.h"
#include "io.h"

void outb( uint16_t port, uint8_t val )
{
	asm volatile("outb %0, %1" : : "a"(val), "Nd"(port) );
}

uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}




BOOT void outb_bs(uint16_t port, uint8_t val){
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port) );
}

BOOT uint8_t inb_bs(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}