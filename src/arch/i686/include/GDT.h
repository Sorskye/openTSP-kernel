#ifndef GDT_H
#define GDT_H

#include "types.h"


struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

void offsetGDTBase(uint32_t offset);
void GDT_install(void);
void gdt_set_tss(int num, uint32_t base, uint32_t limit);

extern void gdt_flush(uint32_t *GDTptr);
extern void tss_flush(void);

#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define USER_CODE_SELECTOR   0x1B
#define USER_DATA_SELECTOR   0x23
#define TSS_SELECTOR         0x28

#endif