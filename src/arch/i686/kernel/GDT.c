#include "GDT.h"
#include "types.h"
#include "serial.h"

#include "debug.h"

static struct gdt_entry gdt_entries[6];
static struct gdt_ptr gdt_pointer;

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    gdt_entries[num].granularity |= (gran & 0xF0);

    gdt_entries[num].access = access;
}

void gdt_set_tss(int num, uint32_t base, uint32_t limit) {
    gdt_entries[num].base_low = base & 0xFFFF;
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low = limit & 0xFFFF;
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    // 32-bit TSS, byte granularity
    gdt_entries[num].granularity |= 0x40;
    gdt_entries[num].access = 0x89;
}

void offsetGDTBase(uint32_t offset){

    serial_print("offsetting GDT base\n");
    KeHalt();
    struct gdt_ptr *virtual_gdt_pointer = (struct gdt_ptr *)((uintptr_t)&gdt_pointer + offset);
    struct gdt_entry *virtual_gdt_entries = (struct gdt_entry *)((uintptr_t)&gdt_entries + offset);
   
    serial_print("new gdt addr: 0x%x\n", virtual_gdt_pointer);

    KeHalt();

    gdt_flush((uint32_t*)virtual_gdt_pointer);
    return;
}

void GDT_install(void) {
    gdt_pointer.limit = sizeof(gdt_entries) - 1;
    gdt_pointer.base = (uint32_t)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0);               // null
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF);   // kernel code
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);   // kernel data
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xCF);   // user code
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xCF);   // user data

    // TSS entry will be filled by tss_init()
    gdt_set_gate(5, 0, 0, 0, 0);

    gdt_flush((uint32_t*)&gdt_pointer);
}

