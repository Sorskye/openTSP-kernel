#ifndef KERROR_H
#define KERROR_H

#include "IDT.h"
void panic(char* panic_msg, panic_registers *regs);
void halt();

extern Char *interruptMessages[];

extern Char *irqMessages[];

#endif