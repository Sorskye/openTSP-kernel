#pragma once

#ifndef PMM_H
#define PMM_H

#include "types.h"

// multiboot

#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MAX_MMAP_ENTRIES 64


struct multiboot_mmap_entry
{
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;

    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;

    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;

    union {
        struct {
            uint32_t framebuffer_palette_addr;
            uint16_t framebuffer_palette_num_colors;
        };
        struct {
            uint8_t framebuffer_red_field_position;
            uint8_t framebuffer_red_mask_size;
            uint8_t framebuffer_green_field_position;
            uint8_t framebuffer_green_mask_size;
            uint8_t framebuffer_blue_field_position;
            uint8_t framebuffer_blue_mask_size;
        };
    };
} __attribute__((packed));
struct memory_entry{
    uint32_t addr;
    uint32_t len;
};


typedef struct multiboot_module {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} multiboot_module_t;


typedef struct {
    uint64_t start;   // inclusive
    uint64_t end;     // exclusive
} phys_range_t;

typedef struct {
    uint32_t start;
    uint32_t current;
    uint32_t end;
    uint32_t max;
} kernel_heap_t;



typedef struct memory_block {
    uint32_t len;              // total block size including header
    struct memory_block* next; // unused for now, kept for compatibility
} memory_block_t;

typedef struct memory_region{
    uint32_t addr;
    uint32_t len;
}memory_region_t;

typedef struct pmm_reserved_region{
    uint32_t addr;
    uint32_t len;
    struct pmm_reserved_region *next;
    struct pmm_reserved_region *tail;
}pmm_reserved_region_t;



#define MINIMUM_BOOTSTRAP_PAGE_TABLES 2 // (8 MiB)
#define EXPECTED_PAGE_TABLES 16 // (64 MiB)

#define MEM_USABLE          1 // multiboot

#define MAX_PHYS_MEM        UINT32_MAX
#define MAX_PHYS_PAGES      (MAX_PHYS_MEM / PAGE_SIZE)


 
#define USED_FLAG 0x1
#define ALIGN4(x) (((x) + 3) & ~3U)
#define ALIGN_PAGE(x) (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))


#define PDE_INDEX(v) (((v) >> 22) & 0x3FF) 
#define PTE_INDEX(v) (((v) >> 12) & 0x3FF)


extern uint32_t bootstrap_page_directory[1024];
extern uint8_t bootstrap_page_directory_start;
extern uint8_t bootstrap_page_directory_end;
extern uint8_t bootstrap_page_tables_start;
extern uint8_t bootstrap_page_tables_end; 

extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];
extern uint8_t __kernel_phys_start[];
extern uint8_t __kernel_phys_end[];
extern uint32_t stack_bottom[];
extern uint32_t stack_top[];

static inline void invlpg(void *addr);
static inline void flush_tlb(void);

// ---- methods

// Initializes the physical memory manager and sets up the free/used page bitmap
void pmm_init(struct multiboot_info* mbinfo);

// ---- API

// returns address of allocated page, returns 0 if out of memory
uint32_t* phys_alloc_page(void);

// returns last address of physical memory
uint64_t get_pmm_end_address();



uint32_t GetKernelPhysicalEnd();

uint32_t GetStackEnd();

uint32_t GetBootstrapPDEnd();

// returns area of free memory for the bootstrap allocator
memory_region_t get_bootstrap_memory();


// free a physical page
void phys_free_page(uint32_t addr);

static inline void invlpg(void *addr);
static inline void flush_tlb(void);

#endif