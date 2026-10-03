#ifndef IDT_H
#define IDT_H

#include "types.h"


typedef struct {
    uint16_t offset_1;
    uint16_t selector;
    uint8_t zero;
    uint8_t type;
    uint16_t offset_2;
} __attribute__((packed)) idt_entry;

typedef struct __attribute__((packed))
{
    uint16_t limit;
    uint32_t base;
} idt_pointer;

typedef struct cpu_regs_with_int_code {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t gs, fs, es, ds;
    uint32_t intNum, errCode;
    uint32_t eip, cs, eflags;
} cpu_regs_with_int_code_t;

typedef struct cpu_regs_syscall {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t intNum, errCode;
    uint32_t eip, cs, eflags, useresp, ss;
} cpu_regs_syscall;

typedef struct cpu_regs {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t gs, fs, es, ds;
}  cpu_regs_t;

extern volatile int inSyscall;

void offsetIDTBase(uint32_t offset);
extern void loadIDT(idt_pointer *IDTPtr);

extern volatile int in_interrupt;
extern const char *current_irq_name;

extern int task_schedule_pending;

bool IDT_install();
void send_eoi(uint8_t irq);

void cli();
void sti();

#endif