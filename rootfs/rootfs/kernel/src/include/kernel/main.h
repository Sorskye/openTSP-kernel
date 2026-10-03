// kernel/kernel.h
#ifndef KERNEL_H
#define KERNEL_H

#include "types.h"
#include "memory.h"



void kernel_main(uint32_t magic, struct multiboot_info* mbinfo);
extern struct inode* root_inode;

#endif