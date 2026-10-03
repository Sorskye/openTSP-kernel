#include "types.h"
#include "stdio.h"
#include "string.h"
#include "pmm.h"
#include "vmm.h"

#include "serial.h"


#include "string.h"
#include "debug.h"

#define MAX_RESERVED_REGIONS 64
pmm_reserved_region_t reserved_list[MAX_RESERVED_REGIONS];
uint32_t reserved_region_count = 0;

memory_region_t bootstrap_memory_region = {0};

static uint8_t phys_bitmap[MAX_PHYS_PAGES/8];
static uint32_t total_phys_pages = 0;
static uint32_t free_phys_pages = 0;

uint64_t pmm_end_address = 0;



uint32_t bootstrap_page_table_count = 0;




// ---- align
static inline uint32_t align_up(uint32_t value, uint32_t align) {
    return (value + align - 1) & ~(align - 1);
}


// ---- bootstrap
void validate_bootstrap_page_directory(void) {

    serial_print("Validating bootstrap page directory: ");
    if ((uintptr_t)&bootstrap_page_directory_start % PAGE_SIZE != 0) {
        KeBugCheck(0, "BOOTSTRAP_PAGE_DIRECTORY_NOT_ALIGNED", 0);
    }
    if ((uintptr_t)&bootstrap_page_directory_end - (uintptr_t)&bootstrap_page_directory_start != PAGE_SIZE) {
        KeBugCheck(0, "BOOTSTRAP_PAGE_DIRECTORY_SIZE_INVALID", 0);
    }

    serial_print("OK\n");
    return;
}

void validate_bootstrap_memory_values(){

    serial_print("Validating bootstrap memory values: ");

    if(MAX_PHYS_MEM == 0){
        KeBugCheck(0, "DEV_SET_VALUE_INVALID : MAX_PHYS_MEM_ZERO", 0);
    }
    if(MAX_PHYS_PAGES == 0){
        KeBugCheck(0, "DEV_SET_VALUE_INVALID : MAX_PHYS_PAGES_ZERO", 0);
    }

    

    uint32_t expected_page_directory_size = 4096;
    uint32_t expected_page_table_size =  EXPECTED_PAGE_TABLES * PAGE_SIZE;

    uint32_t page_directory_size = (uintptr_t)&bootstrap_page_directory_end - (uintptr_t)&bootstrap_page_directory_start;
    uint32_t page_table_size = (uintptr_t)&bootstrap_page_tables_end - (uintptr_t)&bootstrap_page_tables_start;
    uint32_t page_table_count = page_table_size / PAGE_SIZE;

    if(page_table_count != EXPECTED_PAGE_TABLES) {
        serial_print("bootstrap page table count: %d, expected: %d\n", page_table_count, EXPECTED_PAGE_TABLES);
        KeBugCheck(0, "BOOTSTRAP_PAGE_TABLE_COUNT_NOT_EQUAL_TO_EXPECTED", 0);
    }
    // set global variable after validation
    bootstrap_page_table_count = page_table_count;

    if(page_directory_size != expected_page_directory_size) {
        serial_print("bootstrap page directory size: %d, expected: %d\n", page_directory_size, expected_page_directory_size);
        KeBugCheck(0, "BOOTSTRAP_PAGE_DIRECTORY_SIZE_MISMATCH", 0);
    }

    if(page_table_size != expected_page_table_size) {
        serial_print("bootstrap page table size: %d, expected: %d\n", page_table_size, expected_page_table_size);
        KeBugCheck(0, "BOOTSTRAP_PAGE_TABLE_SIZE_MISMATCH", 0);
    }

    if (bootstrap_page_table_count == 0) {
        serial_print("bootstrap page table count: %d\n", bootstrap_page_table_count);
        KeBugCheck(0, "BOOTSTRAP_PAGE_TABLE_COUNT_ZERO", 0);
    }
    if (bootstrap_page_table_count < MINIMUM_BOOTSTRAP_PAGE_TABLES) {
        KeBugCheck(0, "BOOTSTRAP_PAGE_TABLE_COUNT_TOO_LOW", 0);
    }
    
    serial_print("OK\n");
}

// ---- API


uint64_t get_pmm_end_address(){
    return pmm_end_address;
}

memory_region_t get_bootstrap_memory(){
    return bootstrap_memory_region;
}





// ---- bitmap

// (mark as used)
static inline void bitmap_set(uint32_t page) {
    phys_bitmap[page / 8] |= (1 << (page % 8));
}

// (mark as free)
static inline void bitmap_clear(uint32_t page) {
    phys_bitmap[page / 8] &= ~(1 << (page % 8));
}

// returns 1 (used) or 0 (free)
static inline int bitmap_test(uint32_t page) {
    return phys_bitmap[page / 8] & (1 << (page % 8));
}


// ---- pages

// mark all physical pages as used
static void phys_mark_all_used(void) {
    for (uint32_t i = 0; i < sizeof(phys_bitmap); i++) {
        phys_bitmap[i] = 0xFF;
    }
    total_phys_pages = MAX_PHYS_PAGES;
    free_phys_pages = 0;
}

uint32_t* phys_alloc_page(void) {
    for (uint32_t page = 1; page < MAX_PHYS_PAGES; page++) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            free_phys_pages--;
            return (uint32_t *)(uintptr_t)(page * PAGE_SIZE);
        }
    }
    serial_print("phys_alloc_page: out of memory\n");
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


// reserves a range of physical pages as used
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

// return size of kernel image
uint32_t GetKernelPhysicalEnd(){
    return (uint32_t)&__kernel_phys_end;
}

// return size of stack
uint32_t GetStackEnd(){
    return (uint32_t)&stack_top;
}

// return the size of the bootstrap page directory
uint32_t GetBootstrapPDEnd(){
    return (uint32_t)&bootstrap_page_tables_end;
}

// put reserved memory based on bootstrap info inside list & prepare bitmap for heap
void reserve_kernel_memory() {

    // Reserve first page (null pointer protection / BIOS structures safety)
    reserve_physical_range(0x00000000, 0x00001000);

    // Reserve kernel image
    reserve_physical_range((uint32_t)&__kernel_phys_start, (uint32_t)&__kernel_phys_end);
    serial_print("kernel physical range 0x%x > 0x%x\n", __kernel_phys_start, __kernel_phys_end);

    // Reserve kernel stack
    reserve_physical_range((uint32_t)&stack_bottom, (uint32_t)&stack_top);
    

    // Reserve bootstrap paging structures 
    reserve_physical_range((uint32_t)&bootstrap_page_directory_start, (uint32_t)&bootstrap_page_directory_end);
    reserve_physical_range((uint32_t)&bootstrap_page_tables_start, (uint32_t)&bootstrap_page_tables_end);
}

// put reserved memory from multiboot info inside list & prepare bitmap for heap
void reserve_multiboot_memory(struct multiboot_info* mbi)
{
    serial_print("Parsing memory map.. \n");

    if (!(mbi->flags & (1 << 6))){
        return;
    }
       

    uint32_t ptr = mbi->mmap_addr;
    uint32_t end = mbi->mmap_addr + mbi->mmap_length;
    
    struct multiboot_mmap_entry mmap[MAX_MMAP_ENTRIES];
    uint32_t mmap_entry_count = 0;

    
    // Parse GRUB memory map entries
    struct multiboot_mmap_entry* last_mmap_entry = {0};
    while (ptr < end) {
        struct multiboot_mmap_entry* entry = (struct multiboot_mmap_entry*)ptr;

        mmap[mmap_entry_count].addr   = entry->addr;
        mmap[mmap_entry_count].len = entry->len;
        mmap[mmap_entry_count].type   = entry->type;

        
        
        mmap_entry_count++;

        ptr += entry->size + sizeof(entry->size);
      
    }

    // Mark everything used by default
    phys_mark_all_used();
    
    // Free usable RAM regions
    for (uint32_t i = 0; i < mmap_entry_count; i++) {
        if (mmap[i].type != MEM_USABLE){
            serial_print("mmap entry %d not usable (%d)\n", i, mmap[i].type);
            continue;
        }

        // Skip regions below 1MB
        if(mmap[i].addr < 0x100000){
            serial_print("mmap entry %d below 1 MB \n", i);
            continue;
        }



        uint64_t region_start = mmap[i].addr;
        uint64_t region_end   = mmap[i].addr + mmap[i].len;

        // notate free area for bootstrap allocator
        bootstrap_memory_region.addr = region_start + 0x100000;
        bootstrap_memory_region.len = (region_end - region_start) - 0x100000;

        if (region_start >= MAX_PHYS_MEM){
            serial_print("mmap entry %d addres above 32bit limit (%llu Mib)\n", i, region_start / (1024*1024));
            continue;
        }
        if (region_end > MAX_PHYS_MEM){
            serial_print("mmap entry %d end region clamped to 32bit limit \n", i);
            region_end = MAX_PHYS_MEM;
        }
        serial_print("Freeing usable RAM region: 0x%llx - 0x%llx ( %d MiB)\n", region_start, region_end, (uint32_t)(mmap[i].len) / (1024 * 1024));

        uint32_t start_page = (region_start + PAGE_SIZE - 1) / PAGE_SIZE;
        uint32_t end_page   = region_end / PAGE_SIZE;

        for (uint32_t page = start_page; page < end_page; page++) {
            if (bitmap_test(page)) {
                bitmap_clear(page);

                free_phys_pages++;
            }
        }

        serial_print("end addr at %d MiB\n", (region_end) / (1024*1024));
        last_mmap_entry = &mmap[i];
    }

    // Mark GRUB modules as used
    if (mbi->flags & (1 << 3)) {
        multiboot_module_t* mods =( multiboot_module_t*)mbi->mods_addr;

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

    // TODO: map framebuffer to new PD
    // identity map and reserve framebuffer
    if (mbi->framebuffer_addr > 0xFFFFFFFF){
        KeBugCheck(0, "FRAMEBUFFER_ABOVE_INT_LIMIT",0);
    }

    // todo: map fb
    uint32_t framebuffer_addr = mbi->framebuffer_addr;
    uint32_t framebuffer_size = mbi->framebuffer_pitch * mbi->framebuffer_height;
    serial_print("fb address: 0x%x, size: %d bytes\n", framebuffer_addr, framebuffer_size);
   
    //vmm_map_region(framebuffer_addr, framebuffer_addr, framebuffer_size, 0x3);
    //reserve_physical_range(framebuffer_addr, framebuffer_size);
    //add_region_to_list(framebuffer_addr, framebuffer_size);
    //flush_tlb();

    serial_print("mapped fb from %0x to %0x\n", framebuffer_addr, framebuffer_addr + mbi->framebuffer_pitch * mbi->framebuffer_height);
    serial_print("Memory map parsing complete. Free physical pages: %u available memory: %d MiB\n", free_phys_pages, (free_phys_pages * PAGE_SIZE) / (1024 * 1024));

    
    
    pmm_end_address = (last_mmap_entry->addr + last_mmap_entry->len) ;
     
    serial_print("last physcal address: %llu\n", pmm_end_address);
}



// parses all reserved memory areas and sets up physical page bitmap
void pmm_init(struct multiboot_info* mbinfo) {

    // reserved list should use memory_region_t instead of pmm_reserved_region

    serial_print("== PMM init ==\n");
    serial_print("kernel stack bottom - 0x%x, kernel stack top: 0x%x (size: %d Kib)\n",stack_bottom, stack_top, (stack_top - stack_bottom) / 1024);
    serial_print("max phys ram: %llu MiB\n", MAX_PHYS_MEM / (1024*1024));
    serial_print("max phys pages: %d (%d MiB)\n", MAX_PHYS_PAGES, (MAX_PHYS_PAGES * PAGE_SIZE) / (1024*1024));
    // should go together (do the same thing)
    validate_bootstrap_page_directory();
    validate_bootstrap_memory_values();

    
    //fill reserved memory list and set bitmap.
    //TODO: fix TOTAL_PHYS_PAGES value.
    reserve_multiboot_memory(mbinfo);
    reserve_kernel_memory();

    return;
}




// flush cr3 to reload page directory and flush TLB
static inline void invlpg(void *addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

static inline void flush_tlb(void){
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}
