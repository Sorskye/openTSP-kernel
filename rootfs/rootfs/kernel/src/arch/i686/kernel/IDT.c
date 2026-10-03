#include "types.h"
#include "IDT.h"
#include "GDT.h"
#include "kerror.h"
#include "io.h"
#include "stdio.h"
#include "task.h"
#include "ps2_key.h"
#include "ps2_mouse.h"

#include "serial.h"

#include "string.h"

extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15(); // 'Unknown interrupt'
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

extern void irq60();
extern void irq69();

extern void loadIDT(IDT_POINTER *IDTPtr);

int task_schedule_pending = 0;

IDT_POINTER IDTPtr;
IDT_ENTRY IDT[256];
void *IrqHandlers[256] = {0};


 volatile int in_interrupt = 0;
const char *current_irq_name = NULL;

#define INTERRUPT_GATE 0x8E
#define TRAP_GATE 0x8F

void cli(){
    __asm__ __volatile__("cli");
}

void sti(){
    __asm__ __volatile__("sti");
}

void send_eoi(uint8_t irq)
{
	if(irq >= 8)
		outb(0xA0,0x20);
	
	outb(0x20,0x20);
    return;
}


void Exception_Handler(panic_registers *regs) // 0-31 CPU isr
{
    switch (regs->intNum)
    {
        
    default:
        panic("EXCEPTION",regs);
        break;
    }
    
}


void IRQ_common_Handler(panic_registers *regs) {
    switch (regs->errCode)
    {
    case 0:
        break;
    case 1:
        ps2keyboard_irq();
        break;
        
    case 12:
        mouse_irq_handler();
        break;
    default:
    
        break;
    }

    send_eoi(regs->errCode);

}

#define IDT_SET_GATE(n, h) IDT_SET(n, (uint32_t)h, INTERRUPT_GATE)
void IDT_SET(uint8_t number, uint32_t handler, uint8_t type)
{
    IDT[number].offset_1 = handler & 0xFFFF;
    IDT[number].selector = 0x08;
    IDT[number].zero = 0;
    IDT[number].type = type;
    IDT[number].offset_2 = (handler >> 16) & 0xFFFF;
}

bool IDT_install(){
    IDTPtr.limit = sizeof(IDT_ENTRY) * 256 - 1;
    IDTPtr.base = (uint32_t)&IDT;

    loadIDT(&IDTPtr);

    IDT_SET_GATE(0, isr0);
    IDT_SET_GATE(1, isr1);
    IDT_SET_GATE(2, isr2);
    IDT_SET_GATE(3, isr3);
    IDT_SET_GATE(4, isr4);
    IDT_SET_GATE(5, isr5);
    IDT_SET_GATE(6, isr6);
    IDT_SET_GATE(7, isr7);
    IDT_SET_GATE(8, isr8);
    IDT_SET_GATE(9, isr9);
    IDT_SET_GATE(10, isr10);
    IDT_SET_GATE(11, isr11);
    IDT_SET_GATE(12, isr12);
    IDT_SET_GATE(13, isr13);
    IDT_SET_GATE(14, isr14);
    IDT_SET_GATE(15, isr15);
    IDT_SET_GATE(16, isr16);
    IDT_SET_GATE(17, isr17);
    IDT_SET_GATE(18, isr18);
    IDT_SET_GATE(19, isr19);
    IDT_SET_GATE(20, isr20);
    IDT_SET_GATE(21, isr21);
    IDT_SET_GATE(22, isr22);
    IDT_SET_GATE(23, isr23);
    IDT_SET_GATE(24, isr24);
    IDT_SET_GATE(25, isr25);
    IDT_SET_GATE(26, isr26);
    IDT_SET_GATE(27, isr27);
    IDT_SET_GATE(28, isr28);
    IDT_SET_GATE(29, isr29);
    IDT_SET_GATE(30, isr30);
    IDT_SET_GATE(31, isr31);

    // remap PIC to avoid overlap between isr and irq
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 40);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0x0);
    outb(0xA1, 0x0);

    IDT_SET_GATE(32, irq0);
    IDT_SET_GATE(33, irq1);
    IDT_SET_GATE(34, irq2);
    IDT_SET_GATE(35, irq3);
    IDT_SET_GATE(36, irq4);
    IDT_SET_GATE(37, irq5);
    IDT_SET_GATE(38, irq6);
    IDT_SET_GATE(39, irq7);
    IDT_SET_GATE(40, irq8);
    IDT_SET_GATE(41, irq9);
    IDT_SET_GATE(42, irq10);
    IDT_SET_GATE(43, irq11);
    IDT_SET_GATE(44, irq12);
    IDT_SET_GATE(45, irq13);
    IDT_SET_GATE(46, irq14);
    IDT_SET_GATE(47, irq15);

    IDT_SET_GATE(60, irq60);
    IDT_SET_GATE(69, irq69);
    
    asm volatile ("sti");
    return true;
}