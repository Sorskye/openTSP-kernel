#pragma once
#include "types.h"

#ifndef VMM_H
#define VMM_H

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

#define SUPERVISOR_MIN_VHEAP_SIZE (1024*1024) // 1MiB
#define SUPERVISOR_MAX_VHEAP_SIZE (0x10000000 * 4) // 1   GiB

#define KHEAP_MIN_SIZE (0x10000000 / 8) // 16 Mib

#define USER_STACK_TOP 0x50000000
#define USER_STACK_BASE  (USER_STACK_TOP - PAGE_SIZE)
extern volatile uint32_t supervisor_vm_start;

// TODO implement pte into vmm.c
typedef struct pte{
    uint32_t *addr;
    bool valid;
    bool present;
    bool writable;
}pte_t;

typedef enum{
    SUPERVISOR,
    USER,
} vm_priv_lvl;


typedef struct vheap{
    uint32_t start;

    // end of currently allocated heap
    uint32_t end;
    // max heap expand address
    uint32_t max;

    uint32_t current;
}vheap_t;

// virtual memory envoirnment. contains PD, vheap and priv level
typedef struct vm_env{
    uint32_t *pd_addr;
    vheap_t vheap;
    vm_priv_lvl priv;
}vm_env_t;

void vmm_init(struct multiboot_info* mbinfo);

void vm_map_page(vm_env_t *vm_env, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);
void vm_map_region(vm_env_t *vm_env, uint32_t virt, uint32_t phys, uint32_t size, uint32_t flags);
void *vmm_map_temp(uint32_t phys_addr);
void     vmm_unmap_temp(void);
uint32_t vmm_get_phys(vm_env_t *vm_env, uint32_t virt_addr);

// return supervisor vmenv pageing 0 only
vm_env_t *getSupervisorVmEnv();

vm_env_t *allocUserVmEnv();

// heap management
int heap_expand(uint32_t size);
int heap_init(uint32_t kernel_vm_index);

void* kmalloc(uint32_t size) ;
void* kzalloc(size_t size);

void kfree(void *ptr);

void* malloc(uint32_t size);
void free(uint32_t *ptr);

int assignMissingPage(uint32_t fault_address, vm_env_t *vm_env);




#endif