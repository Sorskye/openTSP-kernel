#include "TSS.h"
#include "GDT.h"
#include "string.h"
#include "serial.h"
#include "debug.h"

extern void tss_flush(void);

static tss_entry_t tss_entry;
extern uint8_t stack_top[];

void tss_init() {
    uint32_t kernel_ss = KERNEL_DATA_SELECTOR;
    uint32_t kernel_esp = (uint32_t)stack_top;
    memset(&tss_entry, 0, sizeof(tss_entry));

    tss_entry.ss0 = kernel_ss;
    tss_entry.esp0 = kernel_esp;

    // Set the segment selectors that would be loaded if hardware task switching were used.
    // Not strictly needed for software switching, but standard to initialize.
    tss_entry.cs = USER_CODE_SELECTOR;
    tss_entry.ss = USER_DATA_SELECTOR;
    tss_entry.ds = USER_DATA_SELECTOR;
    tss_entry.es = USER_DATA_SELECTOR;
    tss_entry.fs = USER_DATA_SELECTOR;
    tss_entry.gs = USER_DATA_SELECTOR;

    // Disable I/O bitmap by placing it beyond the TSS limit
    tss_entry.iomap_base = sizeof(tss_entry);

    gdt_set_tss(5, (uint32_t)&tss_entry, sizeof(tss_entry) - 1);

    tss_flush();
    serial_print("TSS initialized\n");
}

void tss_set_kernel_stack(uint32_t kernel_esp) {
    tss_entry.esp0 = kernel_esp;
}

void switch_page_directory(uint32_t *pd_addr) {
    uint32_t pd_virtual = (uint32_t)(uintptr_t)pd_addr;
    uint32_t pd_phys = pd_virtual;

    if (pd_virtual >= 0xE0000000U) {
        pd_phys -= 0xE0000000U;
    }

    if (pd_phys & 0xFFF) {
        KeBugCheck(0, "PD_NOT_ALIGNED_WHILE_SWITCHING_PD", 0);
    }
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pd_phys) : "memory");
}

uint32_t get_page_directory(void) {
    uint32_t phys_addr;

    __asm__ volatile (
        "mov %%cr3, %0"
        : "=r"(phys_addr)
        :
        : "memory"
    );

    return phys_addr;
}