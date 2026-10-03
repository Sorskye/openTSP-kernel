#ifndef KERROR_H
#define KERROR_H

#include "IDT.h"
void init_debug(const char* font_path);
void KeBugCheck(uint32_t code, char* caller_msg, cpu_regs_with_int_code_t *regs);
void DebugPutChar(char c);
void KeHalt();

extern Char *interruptMessages[];

extern Char *irqMessages[];

extern Char *BugCheckMessages[];
#endif