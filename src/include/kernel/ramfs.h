#ifndef RAMFS_H
#define RAMFS_H
#include "types.h"




struct inode* set_tmpfs_from_fsimg(void* image_start, size_t image_size);
struct inode* tmpfs_create_empty_root();
#endif