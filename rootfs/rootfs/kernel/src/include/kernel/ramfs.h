#ifndef RAMFS_H
#define RAMFS_H

#include "fs.h"

struct inode* ramfs_create_root(void* image_start, size_t image_size);

#endif