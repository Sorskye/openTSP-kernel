#include "types.h"
#include "stdio.h"
#include "string.h"
#include "vga-textmode.h"
#include "serial.h"

#include "kerror.h"
#include "memory.h"
#include "string.h"
#include "kerror.h"



#define PAGE_SIZE           4096U
#define PAGE_PRESENT        0x001
#define PAGE_WRITABLE       0x002
#define PAGE_USER           0x004
#define PAGE_WRITE_THROUGH  0x008
#define PAGE_CACHE_DISABLE  0x010
#define PAGE_ACCESSED       0x020
#define PAGE_DIRTY          0x040
#define PAGE_4MB            0x080
#define PAGE_GLOBAL         0x100
#define PAGE_FRAME_MASK 0xFFFFF000U

#define MAX_PHYS_MEM        (64 * 1024 * 1024)
#define MAX_PHYS_PAGES      (MAX_PHYS_MEM / PAGE_SIZE)

#define ALLOC_START         0x00800000  // 8 MiB
#define KERNEL_HEAP_START   0x02000000;
#define KERNEL_HEAP_INITIAL (4 * PAGE_SIZE)
#define KERNEL_HEAP_MAX     0x04000000  // 64 MiB
 
#define USED_FLAG 0x1
#define ALIGN4(x) (((x) + 3) & ~3U)
#define ALIGN_PAGE(x) (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

#define MEM_USABLE          1 // multiboot

#define PDE_INDEX(v) (((v) >> 22) & 0x3FF) 
#define PTE_INDEX(v) (((v) >> 12) & 0x3FF)
extern uint32_t page_directory[1024];
extern uint8_t page_directory_start;
extern uint8_t page_directory_end;
extern uint8_t page_tables_start;
extern uint8_t page_tables_end; 

static uint32_t *kernel_page_directory = page_directory;
void set_kernel_page_directory(void) {
    kernel_page_directory = page_directory;
}

extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];
extern uint8_t stack_bottom[];
extern uint8_t stack_top[];


static uint8_t phys_bitmap[MAX_PHYS_PAGES / 8];
static uint32_t total_phys_pages = 0;
static uint32_t free_phys_pages = 0;

// align
static inline uint32_t align_up(uint32_t value, uint32_t align) {
    return (value + align - 1) & ~(align - 1);
}



//bitmap helpers
static inline void bitmap_set(uint32_t page) {
    phys_bitmap[page / 8] |= (1 << (page % 8));
}

static inline void bitmap_clear(uint32_t page) {
    phys_bitmap[page / 8] &= ~(1 << (page % 8));
}

static inline int bitmap_test(uint32_t page) {
    return phys_bitmap[page / 8] & (1 << (page % 8));
}

static void phys_mark_all_used(void) {
    for (uint32_t i = 0; i < sizeof(phys_bitmap); i++) {
        phys_bitmap[i] = 0xFF;
    }
    total_phys_pages = MAX_PHYS_PAGES;
    free_phys_pages = 0;
}

void reserve_physical_range(uint32_t start, uint32_t end) {
    uint32_t start_page = start / PAGE_SIZE;
    uint32_t end_page   = (end + PAGE_SIZE - 1) / PAGE_SIZE;

    if (end > MAX_PHYS_MEM)
        end = MAX_PHYS_MEM;

    for (uint32_t page = start_page; page < end_page && page < MAX_PHYS_PAGES; page++) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            free_phys_pages--;
        }
    }
}



// pmm
uint32_t phys_alloc_page(void) {
    for (uint32_t page = 1; page < MAX_PHYS_PAGES; page++) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            free_phys_pages--;
            return page * PAGE_SIZE;
        }
    }

    return 0; // out of memory
}

void phys_free_page(uint32_t addr) {
    uint32_t page = addr / PAGE_SIZE;
    if (page >= MAX_PHYS_PAGES)
        return;

    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_phys_pages++;
    }
}


// paging 
static inline void invlpg(void *addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

static inline void flush_tlb(void)
{
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

void create_page_table(uint32_t pde_index) {
    uint32_t phys = phys_alloc_page();
    serial_print("create_page_table: pde=%x phys=%x\n", pde_index, phys);

    if ((phys & 0xFFF) != 0) {
        panic("phys_alloc_page returned non-page-aligned address",0);
    }

    if (phys == 0) {
        panic("phys_alloc_page returned 0 for page table",0);
    }

    uint32_t *pt = (uint32_t *)phys; // identity mapped
    memset(pt, 0, PAGE_SIZE);

    kernel_page_directory[pde_index] =
        (phys & 0xFFFFF000) | PAGE_PRESENT | PAGE_WRITABLE;

    serial_print("PDE[%x] = %x\n", pde_index, kernel_page_directory[pde_index]);
}

void create_page_table_entry(uint32_t pde_index, uint32_t pte_index, uint32_t phys_addr, uint32_t flags) {
    if ((kernel_page_directory[pde_index] & PAGE_PRESENT) == 0) {
        create_page_table(pde_index);
    }

    uint32_t *pt = (uint32_t*)(kernel_page_directory[pde_index] & PAGE_FRAME_MASK);
    pt[pte_index] = (phys_addr & PAGE_FRAME_MASK) | (flags & 0xFFF) | PAGE_PRESENT;
    invlpg((void*)((pde_index << 22) | (pte_index << 12)));
}

void map_page(uint32_t virt_addr, uint32_t phys_addr, uint32_t flags) {
    uint32_t pde_index = (virt_addr >> 22) & 0x3FF;
    uint32_t pte_index = (virt_addr >> 12) & 0x3FF;

    create_page_table_entry(pde_index, pte_index, phys_addr, flags);
}


void debug_page(uint32_t addr) {
    uint32_t pde_index = (addr >> 22) & 0x3FF;
    uint32_t pte_index = (addr >> 12) & 0x3FF;

    uint32_t pde = kernel_page_directory[pde_index];
    serial_print("debug_page addr=%x pde_idx=%x pde=%x\n", addr, pde_index, pde);

    if (!(pde & 1)) {
        serial_print("  PDE not present\n");
        return;
    }

    uint32_t *pt = (uint32_t *)(pde & 0xFFFFF000);
    uint32_t pte = pt[pte_index];
    serial_print("  pte_idx=%x pte=%x\n", pte_index, pte);
}

void map_region(uint32_t virt, uint32_t phys, uint32_t size, uint32_t flags)
{
    uint32_t start_virt = virt & 0xFFFFF000;
    uint32_t start_phys = phys & 0xFFFFF000;
    uint32_t end_virt   = (virt + size + PAGE_SIZE - 1) & 0xFFFFF000;

    for (uint32_t v = start_virt, p = start_phys; v < end_virt; v += PAGE_SIZE, p += PAGE_SIZE) {
        map_page(v, p, flags);
    }

    debug_page(phys);
}

// setup



void reserve_kernel_memory(void) {
    // Reserve first page (null pointer protection / BIOS structures safety)
    reserve_physical_range(0x00000000, 0x00001000);

    // Reserve kernel image
    reserve_physical_range((uint32_t)&__kernel_start, (uint32_t)&__kernel_end);

    // Reserve kernel stack
    reserve_physical_range((uint32_t)&stack_bottom, (uint32_t)&stack_top);

    // Reserve paging structures (commented out because part of kernel range for now)
    reserve_physical_range((uint32_t)&page_directory_start, (uint32_t)&page_directory_end);
    reserve_physical_range((uint32_t)&page_tables_start, (uint32_t)&page_tables_end);
}


void parse_memory_map(struct multiboot_info* mbi)
{
    if (!(mbi->flags & (1 << 6)))
        return;

    uint32_t ptr = mbi->mmap_addr;
    uint32_t end = mbi->mmap_addr + mbi->mmap_length;

    
    struct multiboot_mmap_entry mmap[MAX_MMAP_ENTRIES];
    uint32_t mmap_entry_count = 0;

    // -----------------------------
    // Parse GRUB memory map entries
    // -----------------------------
    while (ptr < end) {
        struct multiboot_mmap_entry* entry =
            (struct multiboot_mmap_entry*)ptr;

        mmap[mmap_entry_count].addr   = entry->addr;
        mmap[mmap_entry_count].len = entry->len;
        mmap[mmap_entry_count].type   = entry->type;
        mmap_entry_count++;

        ptr += entry->size + sizeof(entry->size);
    }

    // Mark everything used by default
    phys_mark_all_used();

    // -----------------------------
    // Free usable RAM regions
    // -----------------------------
    for (uint32_t i = 0; i < mmap_entry_count; i++) {
        if (mmap[i].type != MEM_USABLE)
            continue;

        uint64_t region_start = mmap[i].addr;
        uint64_t region_end   = mmap[i].addr + mmap[i].len;

        if (region_start >= MAX_PHYS_MEM)
            continue;

        if (region_end > MAX_PHYS_MEM)
            region_end = MAX_PHYS_MEM;

        uint32_t start_page = (region_start + PAGE_SIZE - 1) / PAGE_SIZE;
        uint32_t end_page   = region_end / PAGE_SIZE;

        for (uint32_t page = start_page; page < end_page; page++) {
            if (bitmap_test(page)) {
                bitmap_clear(page);
                free_phys_pages++;
            }
        }
    }

    // -----------------------------
    // Mark GRUB modules as used
    // -----------------------------
    if (mbi->flags & (1 << 3)) {
        multiboot_module_t* mods =
            ( multiboot_module_t*)mbi->mods_addr;

        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            uint32_t mod_start = mods[i].mod_start;
            uint32_t mod_end   = mods[i].mod_end;

            uint32_t start_page = mod_start / PAGE_SIZE;
            uint32_t end_page   = (mod_end + PAGE_SIZE - 1) / PAGE_SIZE;

            for (uint32_t page = start_page; page < end_page; page++) {
                bitmap_set(page);
            }
        }
    }

    // map framebuffer
    uint32_t framebuffer_addr = mbi->framebuffer_addr;
    serial_print("mapped fb from %0x to %0x\n", framebuffer_addr, framebuffer_addr + mbi->framebuffer_pitch * mbi->framebuffer_height);
    uint32_t framebuffer_size = mbi->framebuffer_pitch * mbi->framebuffer_height;
    map_region(framebuffer_addr-(sizeof(uint32_t)), framebuffer_addr-(sizeof(uint32_t)), framebuffer_size+(2*(sizeof(uint32_t))), 0x3);
     reserve_physical_range(framebuffer_addr, framebuffer_size);
    flush_tlb();
}






// heap management
int heap_expand(uint32_t size) {
    
    uint32_t new_end = kheap.end;

    while (new_end < kheap.current + size) {
        if (new_end + PAGE_SIZE > kheap.max)
            return 0;

        uint32_t phys_page = phys_alloc_page();
        if (!phys_page)
            return 0;

        map_page(new_end, phys_page, PAGE_PRESENT | PAGE_WRITABLE);
        new_end += PAGE_SIZE;
    }

    kheap.end = new_end;
    return 1;
}

int heap_init(void) {
    kheap.start = KERNEL_HEAP_START;
    kheap.current = KERNEL_HEAP_START;
    kheap.end = KERNEL_HEAP_START;
    kheap.max = KERNEL_HEAP_MAX;

    return heap_expand(KERNEL_HEAP_INITIAL);
}



static inline uint32_t block_size(memory_block_t* b) {
    return b->len & ~USED_FLAG;
}

void* kmalloc(uint32_t size) {
    if (!size) {
        serial_print("ALLOC FAIL! (invalid size)\n");
        return NULL;
    }

    uint32_t user_size = ALIGN4(size);
    uint32_t needed = sizeof(memory_block_t) + user_size;

    if (needed > PAGE_SIZE) {
        // Make large allocs page-aligned like the old allocator behavior implied
        kheap.current = ALIGN_PAGE(kheap.current);

        uint32_t total_size = ALIGN_PAGE(needed);

        if (kheap.current + total_size > kheap.end) {
            if (!heap_expand(total_size)) {
                serial_print("ALLOC FAIL! (heap didn't expand, while size was greater than a page : %d)\n", total_size);
                return NULL;
            }
        }

        memory_block_t* block = (memory_block_t*)(uintptr_t)kheap.current;
        block->len = total_size | USED_FLAG;
        block->next = NULL;

        kheap.current += total_size;

        void* user_ptr = (uint8_t*)block + sizeof(memory_block_t);
        return user_ptr;
    }

    if (kheap.current + needed > kheap.end) {
        if (!heap_expand(needed)) {
            serial_print("ALLOC FAIL! (heap didn't expand)\n");
            return NULL;
        }
    }

    memory_block_t* block = (memory_block_t*)(uintptr_t)kheap.current;
    memset(block, 0 , sizeof(memory_block_t));
    block->len = needed | USED_FLAG;
    
    block->next = NULL;

    kheap.current += needed;

    void* user_ptr = (uint8_t*)block + sizeof(memory_block_t);
    if ((uint32_t)__builtin_frame_address(0) < (uint32_t)stack_bottom ||
    (uint32_t)__builtin_frame_address(0) > (uint32_t)stack_top) {
   
  //  for(;;);
}
    return user_ptr;
}

void* kzalloc(size_t size) {
    uint8_t* ptr = (uint8_t*)kmalloc(size);
    if (!ptr)
        return NULL;

    for (size_t i = 0; i < size; i++)
        ptr[i] = 0;

    return ptr;
}

void kfree(void *ptr) {

}


// init




void memory_init(struct multiboot_info* mbinfo) {
    serial_print("stack bottom - 0x%x, stack top: 0x%x\n",stack_bottom, stack_top);
    set_kernel_page_directory();
    // 1. Build free/used physical memory map
    parse_memory_map(mbinfo);

    // 2. Remove kernel-owned memory from free pool
    reserve_kernel_memory();
    reserve_physical_range(ALLOC_START, ALLOC_START + (1024 * PAGE_SIZE));

    // 3. Initialize kernel heap
    if (!heap_init()) {
        // replace with your panic function
        panic("heap init failed",0);
    }
}