#ifndef IDT_H
#define IDT_H

#include "types.h"

typedef struct {
    uint16_t offset_1;
    uint16_t selector;
    uint8_t zero;
    uint8_t type;
    uint16_t offset_2;
} __attribute__((packed)) IDT_ENTRY;

typedef struct __attribute__((packed))
{
    uint16_t limit;
    uint32_t base;
} IDT_POINTER;

typedef struct panic_registers {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t gs, fs, es, ds;
    uint32_t intNum, errCode;
    uint32_t eip, cs, eflags;
}  panic_registers;

typedef struct Registers {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t gs, fs, es, ds;
}  Registers;




extern volatile int in_interrupt;
extern const char *current_irq_name;

extern int task_schedule_pending;

bool IDT_install();
void send_eoi(uint8_t irq);

void cli();
void sti();

#endif