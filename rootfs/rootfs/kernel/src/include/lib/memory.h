#ifndef MEMORY_H
#define MEMORY_H


#include "types.h"
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

static kernel_heap_t kheap;



typedef struct memory_block {
    uint32_t len;              // total block size including header
    struct memory_block* next; // unused for now, kept for compatibility
} memory_block_t;




void* kmalloc(size_t size);
void* kzalloc(size_t size);

void kfree(void*ptr);

void memory_init(struct multiboot_info* mbinfo);



#endif