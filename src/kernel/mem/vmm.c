#include "types.h"
#include "stdio.h"
#include "string.h"
#include "pmm.h"
#include "vmm.h"

#include "serial.h"
#include "task.h"
#include "string.h"
#include "debug.h"
#include "main.h"

// kernel heap (available at bootstrap and higher half)
// gets redefined to higher half in vmm_init
vheap_t kheap;
vm_env_t kheap_vm_env;

vm_env_t *master_vm_env = NULL;
static vm_env_t master_vm_env_storage;

#define MAX_VM_ENVS 256
static vm_env_t *user_vm_envs[MAX_VM_ENVS];
static uint32_t user_vm_env_count = 0;

static uint32_t KERNEL_VM_START = 0;
static uint32_t KERNEL_VM_END = 0;

volatile uint32_t supervisor_vm_start = 0xC0000000;
#define BOOT_DATA_VIRT       0xC0800000U
#define BOOT_MODULES_VIRT    0xC1000000U
#define BOOT_FRAMEBUFFER_VIRT 0xC2000000U
#define PHYS_MAP_BASE        0xE0000000U

static uint32_t physical_map_enabled = 0;

extern struct multiboot_info *MBinfo;

static void *physical_access(uint32_t phys_addr)
{
    if (physical_map_enabled) {
        return (void *)(uintptr_t)(PHYS_MAP_BASE + phys_addr);
    }
    return (void *)(uintptr_t)phys_addr;
}

extern void vmm_switch_and_jump(uint32_t* new_cr3, uint32_t higher_half_target, uint32_t new_esp);
extern void user_task_return(void);



//flushed tlb
static inline void invlpg(void *addr) {
    
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

// load pd into cr3
static inline void load_pd(uint32_t phys_addr){
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys_addr) : "memory");
}

static inline uint32_t current_pd_addr(void){
    uint32_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & 0xFFFFF000;   // Mask off PWT/PCD/other low bits
}



// allocates a page table at a given pde index
void alloc_page_table(uint32_t *pd_addr, uint32_t pde_index, uint32_t flags) {
    
    uint32_t *phys = phys_alloc_page();

    if (phys == 0) {
        KeBugCheck(5,0,0);
    }

    uintptr_t phys_addr = (uintptr_t)phys;
    if ((phys_addr & 0xFFF) != 0) {
        KeBugCheck(4,0,0);
    }

    memset(physical_access((uint32_t)(uintptr_t)phys), 0, PAGE_SIZE);

    uint32_t pde_flags = PAGE_PRESENT | PAGE_WRITABLE;
    if (flags & PAGE_USER) pde_flags |= PAGE_USER;
    
    pd_addr[pde_index] = (uint32_t)(phys_addr & 0xFFFFF000) | pde_flags;
    
   // serial_print("created PDE[%x] = %x\n", pde_index, pd_addr[pde_index]);
}

// 
void create_page_table_entry(uint32_t *pd_addr, uint32_t pde_index, uint32_t pte_index, uint32_t phys_addr, uint32_t flags) {
    
    
    if ((pd_addr[pde_index] & PAGE_PRESENT) == 0) {
        alloc_page_table(pd_addr, pde_index, flags);
    }

    if (flags & PAGE_USER) {
        pd_addr[pde_index] |= PAGE_USER;
    }

   
    uint32_t *pt = (uint32_t *)physical_access(pd_addr[pde_index] & PAGE_FRAME_MASK);
     
    pt[pte_index] = (phys_addr & PAGE_FRAME_MASK) | (flags & 0xFFF) | PAGE_PRESENT;
      
}


void vm_map_page(vm_env_t *vm_env, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags) {
    uint32_t *pd_addr = vm_env->pd_addr;

   // serial_print("vm_map_page: virt=%x phys=%x flags=%x\n", virt_addr, phys_addr, flags);
    uint32_t pde_index = (virt_addr >> 22) & 0x3FF;
    uint32_t pte_index = (virt_addr >> 12) & 0x3FF;

    create_page_table_entry(pd_addr, pde_index, pte_index, phys_addr, flags);
    
    // always map same page inside boostrap page directory when supervisor does.
    if (vm_env->priv == SUPERVISOR && !physical_map_enabled){
        create_page_table_entry(bootstrap_page_directory, pde_index, pte_index, phys_addr, flags);
    }

    if (vm_env->priv == SUPERVISOR) {
        for (uint32_t i = 0; i < user_vm_env_count; i++) {
            user_vm_envs[i]->pd_addr[pde_index] = pd_addr[pde_index];
        }
    }
}

// allocate a page size in physical memory (bootstrap only!)
uint32_t *alloc_page_dir_bootstrap(void){
     uint32_t *new_pd = phys_alloc_page();

    if (new_pd == 0) {
        KeBugCheck(0, "NO_MEM_FOR_PD_ALLOC", 0);
    }

    // check alignment
    if (((uintptr_t)new_pd & 0xFFF) != 0) {
        KeBugCheck(0, "ALLOCATED_PAGE_DIR_NOT_ALIGNED", 0);
    }

    // zero directory
    memset(physical_access((uint32_t)(uintptr_t)new_pd), 0, PAGE_SIZE);

    return new_pd;
}

// allocate a page size in kernel heap
uint32_t *alloc_page_dir(void)
{
    uint32_t *new_pd_phys = phys_alloc_page();
    if (!new_pd_phys) {
        serial_print("page directory allocation failed ! \n");
        return 0;
    }

    uint32_t *new_pd = (uint32_t *)physical_access((uint32_t)(uintptr_t)new_pd_phys);
    memset(new_pd, 0, PAGE_SIZE);

    // check alignment
    if (((uintptr_t)new_pd & 0xFFF) != 0) {
        KeBugCheck(0, "ALLOCATED_PAGE_DIR_NOT_ALIGNED", 0);
    }

    return new_pd;
}


vm_env_t *getSupervisorVmEnv(){
    
    return master_vm_env;
}

// allocates a page directory based on the master PD with user priv
vm_env_t *allocUserVmEnv(){
    
    vm_env_t *new_vm_env = kzalloc(sizeof(vm_env_t));
    uint32_t *new_pd = alloc_page_dir();
    if (!new_vm_env || !new_pd) {
        return NULL;
    }
    serial_print("new pd at 0x%x\n", new_pd);
    serial_print("master pd at 0x%x\n", master_vm_env->pd_addr);

    // clone supervisor pages to new pd
    for (uint32_t i = (supervisor_vm_start >> 22); i < 1024; i ++){
        new_pd[i] = master_vm_env->pd_addr[i];

        if (new_pd[i] & PAGE_PRESENT) {
            new_pd[i] |= PAGE_USER;
            uint32_t *page_table = (uint32_t *)physical_access(new_pd[i] & PAGE_FRAME_MASK);
            for (uint32_t j = 0; j < 1024; j++) {
                page_table[j] |= PAGE_USER;
            }
        }
    }

    new_vm_env->priv = USER;
    new_vm_env->pd_addr = new_pd;

    for (uint32_t page = 0; page < 8; page++) {
        uint32_t stack_page = USER_STACK_BASE - (page * PAGE_SIZE);
        uint32_t *physical_page = phys_alloc_page();
        if (!physical_page) {
            return NULL;
        }
        vm_map_page(new_vm_env, stack_page, (uint32_t)(uintptr_t)physical_page,
            PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        memset(physical_access((uint32_t)(uintptr_t)physical_page), 0, PAGE_SIZE);

        if (page == 0) {
            uint32_t *stack_return = (uint32_t *)((uintptr_t)physical_access(
                (uint32_t)(uintptr_t)physical_page) + PAGE_SIZE - sizeof(uint32_t));
            *stack_return = (uint32_t)(uintptr_t)user_task_return;
        }
    }

    if (user_vm_env_count >= MAX_VM_ENVS) {
        serial_print("user VM environment limit reached\n");
        return NULL;
    }
    user_vm_envs[user_vm_env_count++] = new_vm_env;

    serial_print("[new vm] allocated vm env at : 0x%x\n", new_vm_env);
    serial_print("[enw vm] page directory at : 0x%x\n", &new_vm_env->pd_addr);

    return new_vm_env;

}

// attempts to allocate a new page for the missing pte. -1: fail, 0: new page allocated, 1: page was already there
int assignMissingPage(uint32_t fault_address, vm_env_t *vm_env)
{
    if (vm_env == 0)
        vm_env = master_vm_env;

    uint32_t *phys_page = phys_alloc_page();

    if (!phys_page) {
        serial_print("[!!!] failed to alloc physical missing page\n");
        return -1;
    }

    memset(physical_access((uint32_t)(uintptr_t)phys_page), 0, PAGE_SIZE);

    uint32_t flags = PAGE_WRITABLE;

    if (vm_env->priv != SUPERVISOR)
        flags |= PAGE_USER;

    vm_map_page(
        vm_env,
        fault_address & PAGE_FRAME_MASK,
        (uint32_t)phys_page,
        flags
    );

    invlpg((void *)fault_address);

    return 0;
}

void vm_map_region(vm_env_t *vm_env, uint32_t virt, uint32_t phys, uint32_t size, uint32_t flags)
{
    uint32_t offset = virt & (PAGE_SIZE - 1);

    virt -= offset;
    phys -= offset;
    size += offset;

    uint64_t end = (uint64_t)virt + size;

    if (end > 0x100000000ULL)
        KeBugCheck(0, "VIRTUAL_ADDRESS_OVERFLOW", 0);

    uint32_t end_virt = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    uint32_t pages_to_map = (end_virt - virt) / PAGE_SIZE;

    for (uint32_t v = virt, p = phys; v < end_virt; v += PAGE_SIZE, p += PAGE_SIZE)
    {
        vm_map_page(vm_env, v, p, flags);
    }


}

static uint32_t map_boot_data(vm_env_t *vm_env, uint32_t phys, uint32_t size, uint32_t virt)
{
    uint32_t offset = phys & (PAGE_SIZE - 1);
    uint32_t mapped_virt = virt + offset;

    vm_map_region(vm_env, virt, phys - offset, size + offset, PAGE_PRESENT | PAGE_WRITABLE);


    return mapped_virt;
}

static uint32_t boot_string_length(uint32_t phys)
{
    const char *string = (const char *)(uintptr_t)phys;
    uint32_t length = 0;

    while (length < PAGE_SIZE - 1 && string[length] != '\0') {
        length++;
    }
    return length + 1;
}

static void map_multiboot_data(vm_env_t *vm_env, struct multiboot_info *mbinfo)
{

    if (((uintptr_t)BOOT_MODULES_VIRT & 0xFFF) != 0){
        KeBugCheck(0, "BOOT MODULE VIRTUAL ADDRESS NOT PAGE ALIGNED\n",0);
    }

    uint32_t next_data_virt = BOOT_DATA_VIRT;
    uint32_t modules_virt = 0;

    struct multiboot_info *mapped_info = (struct multiboot_info *)(uintptr_t)
    map_boot_data(vm_env, (uint32_t)(uintptr_t)mbinfo, sizeof(*mbinfo), next_data_virt);
    next_data_virt += PAGE_SIZE;

    if (mbinfo->flags & (1 << 6)) {
        uint32_t mmap_virt = map_boot_data(vm_env, mbinfo->mmap_addr, mbinfo->mmap_length, next_data_virt);
        mapped_info->mmap_addr = mmap_virt;
        next_data_virt += ALIGN_PAGE(mbinfo->mmap_length);
    }

    if (mbinfo->flags & (1 << 3)) {
        uint32_t modules_size = mbinfo->mods_count * sizeof(multiboot_module_t);
        modules_virt = map_boot_data(vm_env, mbinfo->mods_addr, modules_size, next_data_virt);
        mapped_info->mods_addr = modules_virt;
        next_data_virt += ALIGN_PAGE(modules_size);

        multiboot_module_t *source_modules = (multiboot_module_t *)(uintptr_t)mbinfo->mods_addr;
        multiboot_module_t *mapped_modules = (multiboot_module_t *)(uintptr_t)modules_virt;

        uint32_t next_module_virt = BOOT_MODULES_VIRT;
        for (uint32_t i = 0; i < mbinfo->mods_count; i++) {
            uint32_t phys = source_modules[i].mod_start;
            uint32_t size = source_modules[i].mod_end - source_modules[i].mod_start;
            uint32_t string_phys = source_modules[i].string;

            uint32_t offset = phys & (PAGE_SIZE - 1);

            serial_print("module:\n");
            mapped_modules[i].mod_start = map_boot_data(vm_env, phys, size, next_module_virt);
            mapped_modules[i].mod_end = mapped_modules[i].mod_start + size;

            if (string_phys != 0) {
                uint32_t string_size = boot_string_length(string_phys);
                mapped_modules[i].string = map_boot_data(vm_env, string_phys, string_size, next_data_virt);
                next_data_virt += ALIGN_PAGE(string_size);
            }

            next_module_virt += ALIGN_PAGE(size + offset);
            

        }
        

    }

    if (mapped_info->cmdline != 0) {
        mapped_info->cmdline = map_boot_data(vm_env, mbinfo->cmdline, boot_string_length(mbinfo->cmdline), next_data_virt);
        next_data_virt += PAGE_SIZE;
    }

    if (mapped_info->framebuffer_addr != 0) {
        uint32_t framebuffer_size = mapped_info->framebuffer_pitch * mapped_info->framebuffer_height;
        mapped_info->framebuffer_addr = map_boot_data(vm_env, (uint32_t)mapped_info->framebuffer_addr, framebuffer_size, BOOT_FRAMEBUFFER_VIRT);
    }

    MBinfo = mapped_info;
}






uint32_t nearest_power_of_two(uint32_t v){
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

// setup a vm_env's vheap based on USER / SUPERVISOR priveleges
void init_vm_heap(vm_env_t *vm_env, uint32_t supervisor_heap_start, uint64_t supervisor_vheap_size){
    
    uint32_t supervisor_heap_max = (supervisor_heap_start + supervisor_vheap_size);

    KERNEL_VM_START = supervisor_heap_start;
    KERNEL_VM_END = supervisor_vheap_size;

    serial_print("vm env pointer: 0x%x\n", vm_env);
    serial_print("supervisor heap start: 0x%x\n", supervisor_heap_start);
    serial_print("supervisor heap size: %d\n", supervisor_vheap_size);

    switch(vm_env->priv){
        case SUPERVISOR:

            vm_env->vheap.start = supervisor_heap_start;
            vm_env->vheap.max = supervisor_heap_max;
            
            break;
        case USER:
            // TODO: rework
            vm_env->vheap.start = (0x0 + PAGE_SIZE); // first page is for overflow detection
            vm_env->vheap.max = (supervisor_heap_start - PAGE_SIZE); // page sized buffer 

            break;
        default:
    }
    
    vm_env->vheap.current = vm_env->vheap.start;
    vm_env->vheap.end = vm_env->vheap.start;


    serial_print("vm heap setup (priv: %d, start: 0x%x, max: 0x%x, size: %d MiB)\n",vm_env->priv, vm_env->vheap.start, vm_env->vheap.max, (vm_env->vheap.max - vm_env->vheap.start ) / (1024*1024));
    
    return;
}

// expands vheap of given vm_env by size and maps it
int vm_heap_expand(vm_env_t *vm_env, uint32_t size){
    vheap_t *vheap = &vm_env->vheap;
    uint32_t *pd_addr = vm_env->pd_addr;
    vm_priv_lvl priv = vm_env->priv;

    uint32_t flags = 0;
    switch(priv){
        case SUPERVISOR:
            flags = PAGE_PRESENT | PAGE_WRITABLE;
            break;
        case USER:
            flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
            break;
        default:
            KeBugCheck(0,"INVALID_PD_PRIVILEGE",0);
    }

    // TODO: check for overflow from size
    uint32_t new_end = vheap->end;
    while (new_end < vheap->current + size) {
        
        if (new_end + PAGE_SIZE > vheap->max)
            return 0;

        uint32_t *phys_page = phys_alloc_page();
        if (!phys_page)
            return 0;


        vm_map_page(vm_env, new_end, (uintptr_t)phys_page, flags);
        
        new_end += PAGE_SIZE;
    }
    
    vheap->end = new_end;

    return 1;
}

// returns pointer to memory block inside vm space
void* vm_alloc(vm_env_t *vm_env, uint32_t size){
    

    uint32_t *pd_addr = vm_env->pd_addr;
    vheap_t *vheap = &vm_env->vheap;
    vm_priv_lvl priv = vm_env->priv;

    if (!size){
        serial_print("vm_alloc fail: invalid size\n");
        return NULL;
    }

    if(pd_addr == 0){
        serial_print("vm_alloc fail: invalid pd addr\n");
        return NULL;
    }

    if (vheap->start == 0 | vheap->max > 0xFFFFFFFFULL){
        serial_print("vm_alloc fail: invalid vheap\n");
        return NULL;
    }


    uint32_t size_aligned = ALIGN4(size);
    // memory_block_t as header needs to be included
    uint32_t size_needed = sizeof(memory_block_t) + size_aligned;

    if (size_needed > PAGE_SIZE){
        vheap->current = ALIGN_PAGE(vheap->current);
        uint32_t size_needed = ALIGN_PAGE(size_needed);

        if (vheap->current + size_needed > vheap->end) {
            if (!vm_heap_expand(vm_env, size_needed)) {
                
                serial_print("vm_alloc fail (vm_heap didn't expand, requested size greater than page : %d)\n", size_needed);
                return NULL;
            }
            
        }
      

        memory_block_t* block = (memory_block_t*)(uintptr_t)vheap->current;
        block->len = size_needed | USED_FLAG;
        block->next = NULL;

        vheap->current += size_needed;
          

        // addres of memory begins after header
        return (uint8_t*)block + sizeof(memory_block_t);
    }

    serial_print("current: %d, end: %d c+n: %d\n", vheap->current, vheap->end, (vheap->current + size_needed));
    if (vheap->current + size_needed > vheap->end) {
        if (!vm_heap_expand(vm_env, size_needed)) {
            serial_print("ALLOC FAIL! (heap didn't expand)\n");
            return NULL;
        }
    }

    memory_block_t* block = (memory_block_t*)(uintptr_t)vheap->current;
    memset(block, 0 , sizeof(memory_block_t));
    block->len = size_needed | USED_FLAG;
    
    block->next = NULL;

    vheap->current += size_needed;

    return (uint8_t*)block + sizeof(memory_block_t);
}



// allocates memory using the kernel vm env (kheap)
void* kmalloc(uint32_t size) {
    if (!size) {
        serial_print("ALLOC FAIL! (invalid size)\n");
        return NULL;
    }

    uint32_t user_size = ALIGN4(size);
    uint32_t needed = sizeof(memory_block_t) + user_size;
    

    if (needed > PAGE_SIZE) {
        // Make large allocs page-aligned like the old allocator behavior implied
        kheap_vm_env.vheap.current = ALIGN_PAGE(kheap_vm_env.vheap.current);

        uint32_t total_size = ALIGN_PAGE(needed);

        

        if (kheap_vm_env.vheap.current + total_size > kheap_vm_env.vheap.end) {
            if (!vm_heap_expand(&kheap_vm_env, total_size)) {
                serial_print("ALLOC FAIL! (heap didn't expand, while size was greater than a page : %d)\n", total_size);
                return NULL;
            }

        }

        memory_block_t* block = (memory_block_t*)(uintptr_t)kheap_vm_env.vheap.current;
        block->len = total_size | USED_FLAG;
        block->next = NULL;

        kheap_vm_env.vheap.current += total_size;

        void* user_ptr = (uint8_t*)block + sizeof(memory_block_t);
        return user_ptr;
    }

    if (kheap_vm_env.vheap.current + needed > kheap_vm_env.vheap.end) {
        if (!vm_heap_expand(&kheap_vm_env, needed)) {
            
            serial_print("ALLOC FAIL! (heap didn't expand)\n");
            return NULL;
        }
        
    }

    memory_block_t* block = (memory_block_t*)(uintptr_t)kheap_vm_env.vheap.current;
    
    memset(block, 0 , sizeof(memory_block_t));
     
    block->len = needed | USED_FLAG;
    
    block->next = NULL;

    kheap_vm_env.vheap.current += needed;

    return (uint8_t*)block + sizeof(memory_block_t);
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

// allocates memory using the current processes vm 
void* malloc(uint32_t size){
    if (!size) {
        serial_print("MALLOC FAIL! (invalid size)\n");
        return NULL;
    }

    if(!current_task){
        serial_print("MALLOC FAIL! (no user)\n");
    }

    uint32_t user_size = ALIGN4(size);
    uint32_t needed = sizeof(memory_block_t) + user_size;

    vm_env_t user_vm_env = *current_process->vm_env;



    

    if (needed > PAGE_SIZE) {
        // Make large allocs page-aligned like the old allocator behavior implied
        user_vm_env.vheap.current = ALIGN_PAGE(user_vm_env.vheap.current);

        uint32_t total_size = ALIGN_PAGE(needed);

        

        if (user_vm_env.vheap.current + total_size > user_vm_env.vheap.end) {
            if (!vm_heap_expand(&user_vm_env, total_size)) {
                serial_print("MALLOC FAIL! (heap didn't expand, while size was greater than a page : %d)\n", total_size);
                return NULL;
            }

        }

        memory_block_t* block = (memory_block_t*)(uintptr_t)user_vm_env.vheap.current;
        block->len = total_size | USED_FLAG;
        block->next = NULL;

        user_vm_env.vheap.current += total_size;

        void* user_ptr = (uint8_t*)block + sizeof(memory_block_t);
        return user_ptr;
    }

    if (user_vm_env.vheap.current + needed > user_vm_env.vheap.end) {
        if (!vm_heap_expand(&user_vm_env, needed)) {
            
            serial_print("ALLOC FAIL! (heap didn't expand)\n");
            return NULL;
        }
        
    }

    memory_block_t* block = (memory_block_t*)(uintptr_t)user_vm_env.vheap.current;
    
    memset(block, 0 , sizeof(memory_block_t));
     
    block->len = needed | USED_FLAG;
    
    block->next = NULL;

    user_vm_env.vheap.current += needed;

    return (uint8_t*)block + sizeof(memory_block_t);
}

void free(uint32_t* ptr){

}

// setup master vm_env, and map kernel to higher memory
void vmm_init(struct multiboot_info* mbinfo){


    // allocate 
    serial_print("allocating 1 PAGE for bootstrap heap\n");
    uint32_t *bootstrap_heap_page = phys_alloc_page();
    uint32_t bootstrap_heap_start = (uint32_t)(uintptr_t)bootstrap_heap_page;
    uint32_t bootstrap_heap_len = PAGE_SIZE;
    uint32_t bootstrap_heap_end = bootstrap_heap_start + PAGE_SIZE;
    
    // setup bootstrap kheap envoirnemt
    kheap_vm_env.pd_addr = bootstrap_page_directory;
    kheap_vm_env.priv = SUPERVISOR;

    serial_print("bootstrap heap pd addr: 0x%x\n", kheap_vm_env.pd_addr);
    serial_print("hootstrap heap priv: %d (0 = supervisor)\n", kheap_vm_env.priv);

    // vheap region set to first free memory area (bootstrap_memory_region)
    kheap_vm_env.vheap.start = bootstrap_heap_start;
    kheap_vm_env.vheap.max = (bootstrap_heap_start + bootstrap_heap_len);
    kheap_vm_env.vheap.current = kheap_vm_env.vheap.start;
    kheap_vm_env.vheap.end = kheap_vm_env.vheap.start;

    serial_print("bootstrap heap initialized (start: 0x%x max: 0x%x size: %d KiB)\n", kheap_vm_env.vheap.start, kheap_vm_env.vheap.max, (kheap_vm_env.vheap.max - kheap_vm_env.vheap.start) / (1024));

    // set up master vm envoirnment
    master_vm_env = &master_vm_env_storage;
    memset(master_vm_env, 0, sizeof(*master_vm_env));
    serial_print("master vm env allocated\n");
    
    master_vm_env->pd_addr = alloc_page_dir_bootstrap();
    master_vm_env->priv = SUPERVISOR;

    serial_print("master pd addr: 0x%x\n", master_vm_env->pd_addr);
    serial_print("master priv: %d (0 = supervisor)\n", master_vm_env->priv);
   

    uint32_t supervisor_vm_size = (0xFFFFFFFF - supervisor_vm_start);    

    serial_print("supervisor vm start: 0x%x\n", supervisor_vm_start);
    serial_print("supervisor vm size: %d MiB\n", supervisor_vm_size / (1024*1024));

    map_multiboot_data(master_vm_env, mbinfo);
    
    // map kernel and kheap to higher half (supervisor region)
   
    uint32_t kernel_phys_end = 0;
    kernel_phys_end = max(kernel_phys_end, GetKernelPhysicalEnd());
    kernel_phys_end = max(kernel_phys_end, GetStackEnd());
    kernel_phys_end = max(kernel_phys_end, GetBootstrapPDEnd());
    kernel_phys_end = max(kernel_phys_end, bootstrap_heap_end);
    serial_print("kernel physical end: 0x%x\n", kernel_phys_end);

    uint32_t total_pages = (kernel_phys_end + PAGE_SIZE - 1) / PAGE_SIZE;
    serial_print("kernel pages to remap: %d\n", total_pages);
    // get kernel size from pmminit
    // calculate the pages that need to be remapped to master vm
    // increment virtual address with supervisor_vm_start
    
    for (uint32_t i = 0; i < total_pages; i++){
        uint32_t page_addr = (i * PAGE_SIZE);
        vm_map_page(master_vm_env, (supervisor_vm_start + page_addr), page_addr, PAGE_PRESENT | PAGE_WRITABLE);
    }

    serial_print("mapped %d pages to supervisor vm region. (%d KiB)\n", total_pages, (total_pages*PAGE_SIZE) / (1024));

    uint32_t usedByKernel = (total_pages * PAGE_SIZE) + PAGE_SIZE;
    uint32_t supervisor_vheap_start = supervisor_vm_start + usedByKernel;
    uint32_t supervisor_vheap_len = supervisor_vm_size - usedByKernel;
    
    init_vm_heap(master_vm_env, supervisor_vheap_start, supervisor_vheap_len);

    uint32_t master_pd_phys = (uint32_t)(uintptr_t)master_vm_env->pd_addr;
    vm_map_region(master_vm_env, PHYS_MAP_BASE, 0, EXPECTED_PAGE_TABLES * 1024 * PAGE_SIZE, PAGE_PRESENT | PAGE_WRITABLE);
    master_vm_env->pd_addr = (uint32_t *)(uintptr_t)(PHYS_MAP_BASE + master_pd_phys);

    physical_map_enabled = 1;

    kheap_vm_env = *master_vm_env;

    uint32_t continue_addr = (uint32_t)&kernel_main;
    uint32_t new_esp = (uint32_t)stack_top + supervisor_vm_start;
    
    serial_print("new EIP: 0x%x\n", continue_addr);
    serial_print("new ESP: 0x%x\n", new_esp);


    // switch to master PD (jmphh.asm)
    vmm_switch_and_jump((uint32_t *)(uintptr_t)master_pd_phys, continue_addr, new_esp);
       
}

//returns the physical frame mapped at virt_addr
uint32_t vmm_get_phys(vm_env_t *vm_env, uint32_t virt_addr)
{
    uint32_t *pd_addr = vm_env->pd_addr;

    uint32_t pde_index = (virt_addr >> 22) & 0x3FF;
    uint32_t pte_index = (virt_addr >> 12) & 0x3FF;

    if ((pd_addr[pde_index] & PAGE_PRESENT) == 0)
        return 0;

    uint32_t *pt = (uint32_t *)physical_access(pd_addr[pde_index] & PAGE_FRAME_MASK);

    if ((pt[pte_index] & PAGE_PRESENT) == 0)
        return 0;

    return pt[pte_index] & PAGE_FRAME_MASK;
}

// maps a physical frame so the kernel can read/write it directly regardless of which vm_env is currently loaded in cr3
void *vmm_map_temp(uint32_t phys_addr)
{
    return physical_access(phys_addr);
}

// vmm_map_temp doesn't borrow a scratch slot, so there's nothing to release
// kept so users don't have to use physical_access_directly and keeps it clean.
void vmm_unmap_temp(void)
{
    return;
}