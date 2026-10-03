// kernel/kernel.h
#ifndef KERNEL_H
#define KERNEL_H

#include "types.h"
#include "pmm.h" // for multiboot_info
#include "boot.h"


void kernel_main();
void kernel_bootstrap(uint32_t magic, struct multiboot_info* mbinfo);
extern struct inode* root_inode;



#endif