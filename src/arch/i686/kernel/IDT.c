#include "types.h"
#include "IDT.h"
#include "GDT.h"
#include "debug.h"
#include "io.h"
#include "stdio.h"
#include "task.h"
#include "ps2_key.h"
#include "ps2_mouse.h"
#include "ufs.h"
#include "syscall.h"
#include "task.h"
#include "lpcspeak.h"

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

// syscall
extern void isr128();

// thread exit
extern void isr129();

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



int task_schedule_pending = 0;

idt_pointer IDT_pointer;
idt_entry IDT[256];
void *IrqHandlers[256] = {0};


volatile int in_interrupt = 0;
volatile int inSyscall = -1;
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


void Exception_Handler(cpu_regs_with_int_code_t *regs) // 0-31 CPU isr
{   
    uint8_t ring = regs->cs & 0x3;
    bool asSupervisor = (ring == 0);
    bool asSyscall = (inSyscall > -1);


    switch (regs->intNum)
    {
    case 0:
        // divide by 0
        if(asSupervisor){
            KeBugCheck(7,0,regs); // division by zero
        }else{
            serial_print("USER DIVIDE BY ZERO\n");
            speaker_error();
            process_exit(current_process);
            trigger_schedule();
        }
        break;
    case 6:
        if(asSupervisor){
            KeBugCheck(9,0,regs); // invalid optcode
        }else{
            serial_print("USER INVALID OPTCODE\n");
            speaker_error();
            process_exit(current_process);
            trigger_schedule();
        }
        break;
    case 13:
        // gpf
        if(asSupervisor){
            KeBugCheck(8,0,regs); // gpf
        }else{
            speaker_error();
            serial_print("USER_GPF eip=0x%x err=0x%x\n", regs->eip, regs->errCode);
            process_exit(current_process);
            trigger_schedule();
        }
        break;
    case 14:
        if(asSupervisor && !asSyscall){
            // kernel page fault
            uint32_t virtAddress;
            asm volatile("mov %%cr2, %0" : "=r"(virtAddress));
            if (virtAddress < supervisor_vm_start){
                KeBugCheck(0,"SUPERVISOR_PAGE_FAULT_IN_USER_REGION",regs);
                break;
            }
            serial_print("SPV #PF: 0x%x task: %d\n", virtAddress, current_task->tid);
            int attmpt = assignMissingPage(virtAddress, 0);
            if(attmpt == -1){
                KeBugCheck(10,0,regs);
            }
            serial_print("fixed SPV page fault\n");
            break;
        }else if(asSupervisor && asSyscall){
            // kernel page fault in syscall context
            KeBugCheck(10,0,regs);
        }else{
            // user page fault
            uint32_t virtAddress;
            asm volatile("mov %%cr2, %0" : "=r"(virtAddress));

            // user in sprv area
            if(virtAddress >= supervisor_vm_start){
                serial_print("USER ACCESS VIOLATION : PROC TERMINATED\n");
                speaker_error();
                process_exit(current_process);
                trigger_schedule();
            }

            if (regs->errCode & 1U) {
                serial_print("USER PROTECTION FAULT: addr=0x%x eip=0x%x err=0x%x\n",
                    virtAddress, regs->eip, regs->errCode);
                process_exit(current_process);
                trigger_schedule();
            }

            // userspace

            vm_env_t* user_vm_env = current_process->vm_env;
            int attmpt = assignMissingPage(virtAddress, user_vm_env);

            if (attmpt == -1){
                serial_print("UNFIXABLE USER PF : PROC TERMINATED\n");
                speaker_error();
                process_exit(current_process);
                trigger_schedule();
            }

            break;

            // try to alloc page 
        }
        break;
    default:
        KeBugCheck(1, 0, regs);
        break;
    }
    
}

void syscall_handler(cpu_regs_syscall* regs){
    if(regs->eax != 1){
        serial_print("SYS: %d\n",regs->eax);
    }

    uint32_t syscall = regs->eax;
    inSyscall = (int)syscall;

    int result = dispatchSyscall(syscall, &regs->eax, regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi, regs->ebp);
    if (result != 0)
        regs->eax = (uint32_t)result;
    inSyscall = -1;
    send_eoi(regs->errCode);
}


void IRQ_common_Handler(cpu_regs_with_int_code_t *regs) {
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
    IDT[number].selector = KERNEL_CODE_SELECTOR;
    IDT[number].zero = 0;
    IDT[number].type = type;
    IDT[number].offset_2 = (handler >> 16) & 0xFFFF;
}

void offsetIDTBase(uint32_t offset){
    IDT_pointer.base = (uint32_t)&IDT + offset;
    loadIDT(&IDT_pointer);
    return;
}

bool IDT_install(){
    IDT_pointer.limit = sizeof(idt_entry) * 256 - 1;
    IDT_pointer.base = (uint32_t)&IDT;

    loadIDT(&IDT_pointer);

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

    IDT_SET(32, (uint32_t)irq0, 0xEE);
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

    // syscall gate, callable from ring 3
    IDT_SET(0x80, (uint32_t)isr128, 0xEE);

    // thread exit gate
    IDT_SET(0x81, (uint32_t)isr129, 0xEF);
    
    asm volatile ("sti");
    return true;
}